"""Installer behavior at disposable Wine and interactive login boundaries."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DebianFirstLoginTests(unittest.TestCase):
    def test_new_debian_cursor_default_preserves_explicit_choices_and_ubuntu(self):
        source = (ROOT / 'install.sh').read_text()
        assignment = next(line for line in source.splitlines()
                          if line.startswith('cursor_guard='))
        start = source.index('if [[ -z "$input_route" ]]; then')
        end = source.index('if [[ "$input_route" != legacy', start)
        cases = [('debian', '', '', 'on'), ('ubuntu', '', '', 'off'),
                 ('debian', 'off', '', 'off'), ('debian', 'on', 'off', 'off')]
        for distribution, saved, explicit, expected in cases:
            with self.subTest(distribution=distribution, saved=saved, explicit=explicit):
                environment = {k: v for k, v in os.environ.items()
                               if k != 'UURB_CURSOR_GUARD'}
                if explicit:
                    environment['UURB_CURSOR_GUARD'] = explicit
                result = subprocess.run([
                    'bash', '-Eeuo', 'pipefail', '-c',
                    'ID="$1"\nsaved_cursor_guard="$2"\ninput_route=\ndesktop_relay=rdp\n' +
                    assignment + '\n' + source[start:end] + '\nprintf "%s\\n" "$cursor_guard"',
                    'cursor-default-test', distribution, saved],
                    env=environment, capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)

    def test_fresh_debian_configures_only_the_manager_renderer_before_installer(self):
        source = (ROOT / 'install.sh').read_text()
        start = source.index('if [[ ! -f "$uu_dir/GameViewer.exe" || "$upgrade_existing" == true ]]; then')
        end = source.index('if [[ ! -f "$server_exe"', start)
        for distribution in ('debian', 'ubuntu'):
            with self.subTest(distribution=distribution), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                installer = directory / 'installer.exe'
                installer.write_bytes(b'disposable installer boundary')
                wine = directory / 'wine'
                wine.write_text('''#!/usr/bin/python3
import json, os, sys
from pathlib import Path
p = Path(os.environ['WINEPREFIX']) / 'wine-commands.json'
calls = json.loads(p.read_text()) if p.exists() else []
args = sys.argv[1:]
calls.append(args)
p.write_text(json.dumps(calls))
if args[-1:] == ['/S'] and os.environ['TEST_DISTRIBUTION'] == 'debian':
    expected = ['reg', 'add', r'HKCU\\Software\\Wine\\AppDefaults\\GameViewer.exe\\Direct3D',
                '/v', 'renderer', '/t', 'REG_SZ', '/d', 'gdi', '/f']
    if expected not in calls[:-1]:
        print('The fresh manager can launch before its GDI configuration', file=sys.stderr)
        sys.exit(72)
''')
                wine.chmod(0o755)
                prelude = '''
repo_dir="$1"
wine_prefix="$2/prefix"
uu_dir="$wine_prefix/drive_c/Program Files/Netease/GameViewer"
wine_bin="$2/wine"
python_bin=/usr/bin/python3
uu_installer="$2/installer.exe"
uu_installer_sha256="$3"
ID="$4"
upgrade_existing=false
fresh_install=false
export WINEPREFIX="$wine_prefix"
stop_wine_prefix() { :; }
'''
                result = subprocess.run([
                    'bash', '-Eeuo', 'pipefail', '-c', prelude + source[start:end],
                    'bootstrap-test', str(ROOT), str(directory),
                    hashlib.sha256(installer.read_bytes()).hexdigest(), distribution],
                    env=dict(os.environ, TEST_DISTRIBUTION=distribution),
                    capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                calls = json.loads((directory / 'prefix/wine-commands.json').read_text())
                if distribution == 'ubuntu':
                    self.assertFalse(any(call[:2] == ['reg', 'add'] for call in calls))

    def run_account_boundary(self, confirmation):
        source = (ROOT / 'install.sh').read_text()
        start = source.index('if [[ "$fresh_install" == true && "$skip_account_login" == false ]]; then')
        end = source.index('if [[ "$start_service" == true ]]; then', start)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            compat = directory / 'compat'
            compat.mkdir()
            helper = compat / 'winlogon.exe'
            helper.write_text('#!/bin/sh\nprintf "%s" "$$" > "$TEST_DIR/helper.pid"\n'
                              'while :; do /bin/sleep 1; done\n')
            helper.chmod(0o755)
            wine = directory / 'wine'
            wine.write_text('#!/bin/sh\n: > "$TEST_DIR/launcher-returned"\nexit 0\n')
            wine.chmod(0o755)
            prelude = '''
fresh_install=true
skip_account_login=false
ID=debian
uu_dir="$1"
wine_prefix="$1"
wine_bin="$1/wine"
stop_wine_prefix() {
    : > "$TEST_DIR/stopped"
    if [[ -f "$TEST_DIR/helper.pid" ]]; then
        kill "$(cat "$TEST_DIR/helper.pid")" 2>/dev/null || true
    fi
}
'''
            with (directory / 'output').open('w') as output:
                process = subprocess.Popen([
                    'bash', '-Eeuo', 'pipefail', '-c', prelude + source[start:end],
                    'login-test', str(directory)], stdin=subprocess.PIPE,
                    stdout=output, stderr=output, text=True,
                    env=dict(os.environ, TEST_DIR=str(directory)))
                try:
                    deadline = time.monotonic() + 5
                    while not (directory / 'launcher-returned').exists():
                        if process.poll() is not None or time.monotonic() >= deadline:
                            break
                        time.sleep(0.01)
                    time.sleep(0.1)
                    self.assertTrue((directory / 'launcher-returned').exists())
                    self.assertIsNone(process.poll(),
                                      'A returning launcher must not terminate the login session')
                    self.assertFalse((directory / 'stopped').exists())
                    process.communicate(confirmation, timeout=5)
                    self.assertEqual(process.returncode, 0 if confirmation else 1,
                                     (directory / 'output').read_text())
                    self.assertTrue((directory / 'stopped').exists())
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=5)
                    process.stdin.close()
                    pid_file = directory / 'helper.pid'
                    if pid_file.exists():
                        try:
                            os.kill(int(pid_file.read_text()), signal.SIGTERM)
                        except ProcessLookupError:
                            pass

    def test_account_login_waits_for_user_confirmation_after_launcher_returns(self):
        self.run_account_boundary('\n')

    def test_account_login_eof_cleans_up_without_claiming_success(self):
        self.run_account_boundary('')


if __name__ == '__main__':
    unittest.main()
