"""Resize regressions over private loopback sockets and real PTYs; no installed services."""
from contextlib import contextmanager
import os
from pathlib import Path
import re
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOKEN = b"a" * 64


class TerminalResizeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Keep test artifacts in the checkout and never load the user's shell profile.
        (ROOT / "build").mkdir(exist_ok=True)
        cls.build = tempfile.TemporaryDirectory(prefix="terminal-resize-", dir=ROOT / "build")
        cls.path = Path(cls.build.name)
        source_path = Path(os.environ.get("UURB_TERMINAL_TEST_SOURCE", ROOT / "src/uu_terminal_bridge.c"))
        source, replaced = re.subn(
            r"static void run_login_shell\(void\)\n\{.*?\n\}",
            lambda _: 'static void run_login_shell(void)\n{\n'
            '    unsetenv("ENV");\n    unsetenv("BASH_ENV");\n'
            '    execl("/bin/sh", "sh", "-i", (char *)NULL);\n    _exit(127);\n}',
            source_path.read_text(), count=1, flags=re.S)
        if replaced != 1:
            raise AssertionError("test shell replacement did not match exactly once")
        (cls.path / "helper.c").write_text(source)
        # Only the error-path test redirects TIOCSWINSZ to an invalid fd. The
        # kernel returns a real EBADF; all ordinary resizes use the actual PTY.
        (cls.path / "ioctl_failure.c").write_text(
            '#include <stdarg.h>\n#include <stdlib.h>\n#include <sys/ioctl.h>\n'
            'extern int __real_ioctl(int fd, unsigned long request, ...);\n'
            'int __wrap_ioctl(int fd, unsigned long request, ...)\n{\n'
            '    va_list args;\n    void *value;\n'
            '    va_start(args, request);\n    value = va_arg(args, void *);\n    va_end(args);\n'
            '    if (request == TIOCSWINSZ && getenv("UURB_TEST_IOCTL_FAILURE")) fd = -1;\n'
            '    return __real_ioctl(fd, request, value);\n}\n')
        subprocess.run([
            "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
            str(cls.path / "helper.c"), str(cls.path / "ioctl_failure.c"),
            "-Wl,--wrap=ioctl", "-lutil", "-o", str(cls.path / "helper")],
            check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def send_frame(self, connection, kind, payload=b"", reserved=b"\0\0\0"):
        connection.sendall(struct.pack("!B3sI", kind, reserved, len(payload)) + payload)

    def resize(self, connection, columns, rows):
        self.send_frame(connection, 2, struct.pack("!HH", columns, rows))

    def read_until(self, connection, marker):
        output = bytearray()
        deadline = time.monotonic() + 3
        while marker not in output and time.monotonic() < deadline:
            try:
                chunk = connection.recv(8192)
            except socket.timeout:
                continue
            if not chunk:
                self.fail(f"terminal disconnected before {marker!r}; received {bytes(output)!r}")
            output.extend(chunk)
        self.assertIn(marker, output)
        return bytes(output)

    def assert_size(self, connection, rows, columns):
        self.send_frame(connection, 1, b"printf '__SIZE:%s__\\n' \"$(stty size)\"\n")
        self.read_until(connection, f"__SIZE:{rows} {columns}__\r\n".encode())

    def assert_disconnected(self, connection):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                if not connection.recv(8192):
                    return
            except ConnectionResetError:
                return
            except socket.timeout:
                continue
        self.fail("terminal session did not disconnect")

    @contextmanager
    def session(self, *, ioctl_failure=False):
        with tempfile.TemporaryDirectory(prefix="session-", dir=self.path) as directory:
            ready = Path(directory) / "ready"
            log_path = Path(directory) / "log"
            environment = {**os.environ, "UURB_TERMINAL_BRIDGE_TOKEN": TOKEN.decode(),
                           "UURB_TERMINAL_SESSION_MODE": "fresh"}
            environment.pop("UURB_TEST_IOCTL_FAILURE", None)
            if ioctl_failure:
                environment["UURB_TEST_IOCTL_FAILURE"] = "1"
            with log_path.open("w") as log:
                process = subprocess.Popen([str(self.path / "helper"), "--ready-file", str(ready)],
                                           env=environment, stdout=log, stderr=log, start_new_session=True)
                connection = None
                try:
                    deadline = time.monotonic() + 3
                    while not ready.exists() and time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), log_path.read_text())
                        time.sleep(0.01)
                    self.assertTrue(ready.exists(), log_path.read_text())
                    connection = socket.create_connection(("127.0.0.1", int(ready.read_text())), timeout=0.25)
                    connection.sendall(struct.pack("!IHHHH", 0x55555242, 1, 64, 80, 24) + TOKEN)
                    self.assertEqual(connection.recv(1), b"\x06")
                    self.send_frame(connection, 1, b"stty -echo; PS1=; printf '__%s__\\n' READY\n")
                    self.read_until(connection, b"__READY__\r\n")
                    yield connection, log_path
                finally:
                    if connection is not None:
                        connection.close()
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait(timeout=2)
                    self.assertEqual(process.returncode, 0, log_path.read_text())

    def test_pc_transient_resize_then_correction_keeps_same_shell(self):
        with self.session() as (connection, _):
            self.send_frame(connection, 1, b"session_marker=still_here\n")
            self.assert_size(connection, 24, 80)
            self.resize(connection, 120, 9001)
            time.sleep(0.04)
            self.resize(connection, 120, 30)
            self.assert_size(connection, 30, 120)
            self.send_frame(connection, 1, "printf '__CONTINUED:%s:你好__\\n' \"$session_marker\"\n".encode())
            self.read_until(connection, "__CONTINUED:still_here:你好__\r\n".encode())
            self.send_frame(connection, 3)
            self.assert_disconnected(connection)

    def test_invalid_dimensions_preserve_last_size_and_valid_boundaries_apply(self):
        with self.session() as (connection, _):
            self.resize(connection, 132, 43)
            self.assert_size(connection, 43, 132)
            for columns, rows in ((120, 9001), (0, 30), (120, 0), (1001, 30),
                                  (120, 1001), (65535, 30), (120, 65535)):
                with self.subTest(columns=columns, rows=rows):
                    self.resize(connection, columns, rows)
                    self.assert_size(connection, 43, 132)
            for columns, rows in ((1, 1), (1000, 1000), (120, 30)):
                with self.subTest(columns=columns, rows=rows):
                    self.resize(connection, columns, rows)
                    self.assert_size(connection, rows, columns)

    def test_malformed_resize_lengths_disconnect(self):
        for payload in (b"", b"\0", b"\0" * 3, b"\0" * 5):
            with self.subTest(length=len(payload)), self.session() as (connection, _):
                self.send_frame(connection, 2, payload)
                self.assert_disconnected(connection)

    def test_nonzero_reserved_resize_header_disconnects(self):
        with self.session() as (connection, _):
            self.send_frame(connection, 2, struct.pack("!HH", 120, 30), reserved=b"\1\0\0")
            self.assert_disconnected(connection)

    def test_actual_ioctl_failure_is_fatal_but_invalid_size_is_not(self):
        with self.session(ioctl_failure=True) as (connection, log):
            self.resize(connection, 120, 9001)
            self.assert_size(connection, 24, 80)
            self.resize(connection, 120, 30)
            self.assert_disconnected(connection)
            self.assertIn("terminal resize ioctl failed", log.read_text())

    def test_control_c_and_shell_output_after_resizes(self):
        with self.session() as (connection, _):
            self.resize(connection, 120, 9001)
            self.resize(connection, 120, 30)
            self.send_frame(connection, 1, b"printf '__SLEEPING__\\n'; sleep 30\n")
            self.read_until(connection, b"__SLEEPING__\r\n")
            self.send_frame(connection, 1, b"\x03")
            self.send_frame(connection, 1, b"printf '__INTERRUPTED__\\n'\n")
            self.read_until(connection, b"__INTERRUPTED__\r\n")
            self.assert_size(connection, 30, 120)


if __name__ == "__main__":
    unittest.main()
