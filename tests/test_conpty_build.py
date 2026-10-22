"""Compile against the host SDK defaults, including Debian's MinGW headers."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
COMPILER = shutil.which('x86_64-w64-mingw32-gcc')
OBJDUMP = shutil.which('x86_64-w64-mingw32-objdump')


@unittest.skipUnless(COMPILER and OBJDUMP, 'MinGW compiler and objdump required')
class ConptyBuildTests(unittest.TestCase):
    def test_conpty_dll_exposes_pseudoconsole_api_with_default_sdk_settings(self):
        with tempfile.TemporaryDirectory(prefix='uu conpty ') as directory:
            output = Path(directory) / 'conpty.dll'
            result = subprocess.run([
                COMPILER, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Wl,--no-insert-timestamp', '-shared', '-o', str(output),
                str(ROOT / 'src/uu_conpty_compat.c'),
                str(ROOT / 'src/uu_conpty_compat.def'), '-lws2_32', '-lshell32'],
                capture_output=True, text=True, timeout=45)
            self.assertEqual(result.returncode, 0, result.stderr)
            information = subprocess.run([OBJDUMP, '-p', str(output)],
                                         check=True, capture_output=True, text=True)
            export_table = information.stdout.partition('[Ordinal/Name Pointer] Table')[2]
            export_table = export_table.split('\n\n', 1)[0]
            exports = {line.split()[-1] for line in export_table.splitlines()
                       if re.match(r'\s*\[\s*\d+\]', line)}
            for name in ['CreatePseudoConsole', 'ResizePseudoConsole', 'ClosePseudoConsole']:
                self.assertIn(name, exports)

    def test_terminal_wire_probe_compiles_with_default_sdk_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([
                COMPILER, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-municode', '-o', str(Path(directory) / 'wire.exe'),
                str(ROOT / 'tests/probes/uu_terminal_direct_wire_probe.c')],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
