import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HostPlatformTests(unittest.TestCase):
    def detect(self, distribution, version, codename, **overrides):
        source = (ROOT / 'install.sh').read_text()
        start = source.index('if [[ "$(uname -m)" != x86_64 ]]')
        end = source.index('if [[ ! "$rdp_port"', start)
        # Execute the real detection block against an isolated os-release fixture.
        detection = source[start:end].replace('source /etc/os-release', 'source "$1"')
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / 'os-release'
            fixture.write_text(f'ID={shlex.quote(distribution)}\n'
                               f'VERSION_ID={shlex.quote(version)}\n'
                               f'VERSION_CODENAME={shlex.quote(codename)}\n')
            environment = dict(os.environ)
            environment.pop('UURB_ALLOW_UNVALIDATED_UBUNTU', None)
            environment.update(overrides)
            return subprocess.run(
                ['bash', '-Eeuo', 'pipefail', '-c', detection +
                 '\nprintf "%s\\n" "${winehq_sources_url:-missing}"',
                 'host-test', str(fixture)], env=environment,
                capture_output=True, text=True, timeout=5)

    def test_debian_13_uses_debian_winehq_source(self):
        result = self.detect('debian', '13', 'trixie')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(),
                         'https://dl.winehq.org/wine-builds/debian/dists/trixie/winehq-trixie.sources')

    def test_supported_ubuntu_sources_are_preserved(self):
        for version, codename in [('24.04', 'noble'), ('26.04', 'resolute')]:
            with self.subTest(version=version):
                result = self.detect('ubuntu', version, codename)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(),
                                 f'https://dl.winehq.org/wine-builds/ubuntu/dists/{codename}/winehq-{codename}.sources')

    def test_other_debian_releases_cannot_use_ubuntu_override(self):
        for version, codename in [('12', 'bookworm'), ('14', 'forky')]:
            result = self.detect('debian', version, codename,
                                 UURB_ALLOW_UNVALIDATED_UBUNTU='1')
            self.assertNotEqual(result.returncode, 0)

    def test_unknown_distributions_and_bad_codenames_fail_closed(self):
        for distribution, version, codename in [
                ('linuxmint', '22', 'wilma'), ('debian', '13', 'bookworm'),
                ('debian', '13', ''), ('ubuntu', '26.04', '../trixie')]:
            with self.subTest(distribution=distribution, codename=codename):
                result = self.detect(distribution, version, codename)
                self.assertNotEqual(result.returncode, 0)

    def test_debian_defaults_to_compatible_input_without_overriding_explicit_choice(self):
        source = (ROOT / 'install.sh').read_text()
        start = source.index('if [[ -z "$input_route" ]]; then')
        end = source.index('if [[ "$input_route" != legacy', start)
        for distribution, requested, expected in [
            ('debian', '', 'legacy'), ('ubuntu', '', 'rdp-public'),
            ('debian', 'rdp-public', 'rdp-public'),
        ]:
            with self.subTest(distribution=distribution, requested=requested):
                result = subprocess.run([
                    'bash', '-Eeuo', 'pipefail', '-c',
                    'ID="$1"\ninput_route="$2"\ndesktop_relay=rdp\n' +
                    source[start:end] + '\nprintf "%s\\n" "$input_route"',
                    'route-test', distribution, requested],
                    capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)


class ReferenceContainerTests(unittest.TestCase):
    def dry_run(self, directory, engine, environment=None):
        return subprocess.run([
            '/usr/bin/python3', str(ROOT / 'scripts/build-reference-relay.py'),
            '--dry-run', '--engine', engine, '--output', str(directory / 'output'),
            '--work-dir', str(directory / 'work')],
            env=environment, capture_output=True, text=True, timeout=5)

    def test_loopback_proxy_is_reachable_in_build_and_runtime_without_logging_credentials(self):
        proxy_names = ('HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY',
                       'http_proxy', 'https_proxy', 'all_proxy')
        for proxy in ['http://127.0.0.1:7890',
                      'http://test-user:private-marker@localhost:7890',
                      'http://[::1]:7890']:
            with self.subTest(proxy=proxy), tempfile.TemporaryDirectory() as directory:
                environment = {k: v for k, v in os.environ.items() if k not in proxy_names}
                environment['HTTPS_PROXY'] = proxy
                result = self.dry_run(Path(directory), 'podman', environment)
                self.assertEqual(result.returncode, 0, result.stderr)
                plan = json.loads(result.stdout)
                self.assertIn('--network=host', plan['build'])
                self.assertIn('--network=host', plan['run'])
                self.assertIn('HTTPS_PROXY', plan['run'])
                self.assertNotIn('private-marker', result.stdout + result.stderr)

    def test_remote_proxy_preserves_private_container_network(self):
        proxy_names = ('HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY',
                       'http_proxy', 'https_proxy', 'all_proxy')
        environment = {k: v for k, v in os.environ.items() if k not in proxy_names}
        environment['HTTPS_PROXY'] = 'http://proxy.example.invalid:7890'
        with tempfile.TemporaryDirectory() as directory:
            result = self.dry_run(Path(directory), 'podman', environment)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertNotIn('--network=host', plan['build'])
            self.assertNotIn('--network=host', plan['run'])

    def test_dry_run_keeps_ubuntu_packages_and_source_writes_off_host(self):
        with tempfile.TemporaryDirectory(prefix='uu reference ') as directory:
            directory = Path(directory)
            result = self.dry_run(directory, 'podman')
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual(plan['build'][0:2], ['podman', 'build'])
            self.assertIn('--userns=keep-id', plan['run'])
            self.assertIn(f'type=bind,src={ROOT},dst=/product,ro', plan['run'])
            self.assertIn(f'type=bind,src={directory}/output,dst=/output', plan['run'])
            self.assertEqual(plan['run'][-2:],
                             ['/product/scripts/build-winpr.sh', '/output'])
            self.assertFalse((directory / 'output').exists())
            self.assertFalse((directory / 'work').exists())

    def test_docker_uses_host_user_without_podman_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.dry_run(Path(directory), 'docker')
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertIn(f'{os.getuid()}:{os.getgid()}', plan['run'])
            self.assertNotIn('--userns=keep-id', plan['run'])

    def test_refuses_mounting_repository_writable_as_output(self):
        for path in [ROOT, ROOT / 'src', ROOT / 'scripts']:
            with self.subTest(path=path):
                result = subprocess.run([
                    '/usr/bin/python3', str(ROOT / 'scripts/build-reference-relay.py'),
                    '--dry-run', '--engine', 'podman', '--output', str(path)],
                    capture_output=True, text=True, timeout=5)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('output', result.stderr.lower())

    def test_container_success_cannot_approve_missing_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            # Only the external container engine is replaced. The product verifier
            # must still reject a successful engine that produces no runtime.
            engine = directory / 'podman'
            engine.write_text('#!/bin/sh\nexit 0\n')
            engine.chmod(0o755)
            result = subprocess.run([
                '/usr/bin/python3', str(ROOT / 'scripts/build-reference-relay.py'),
                '--engine', 'podman', '--output', str(directory / 'output'),
                '--work-dir', str(directory / 'work')],
                env=dict(os.environ, PATH=str(directory) + os.pathsep + os.environ['PATH']),
                capture_output=True, text=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Reference relay build failed', result.stderr)
            self.assertNotIn('built and verified', result.stdout)

    def test_docker_user_namespaces_fail_before_building_or_creating_outputs(self):
        for option in ['name=rootless', 'name=userns']:
            with self.subTest(option=option), tempfile.TemporaryDirectory() as directory:
                directory = Path(directory)
                engine = directory / 'docker'
                engine.write_text('#!/bin/sh\nif [ "$1" = info ]; then\n'
                                  f"printf '%s\\n' '[\"{option}\"]'\nexit 0\nfi\nexit 81\n")
                engine.chmod(0o755)
                result = subprocess.run([
                    '/usr/bin/python3', str(ROOT / 'scripts/build-reference-relay.py'),
                    '--engine', 'docker', '--output', str(directory / 'output'),
                    '--work-dir', str(directory / 'work')],
                    env=dict(os.environ, PATH=str(directory) + os.pathsep + os.environ['PATH']),
                    capture_output=True, text=True, timeout=5)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('user namespace', result.stderr.lower())
                self.assertIn('podman', result.stderr)
                self.assertFalse((directory / 'output').exists())
                self.assertFalse((directory / 'work').exists())


class FreshUserInstallationTests(unittest.TestCase):
    def test_tool_assets_install_into_a_fresh_user_home(self):
        source = (ROOT / 'install.sh').read_text()
        start = source.index('install -m 0755 "$repo_dir/scripts/uu-clipboard-native.py"')
        end = source.index('install -m 0755 "$repo_dir/scripts/uu-keyring-unlock.py"', start)
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            (home / '.local/libexec').mkdir(parents=True)
            result = subprocess.run([
                'bash', '-Eeuo', 'pipefail', '-c',
                'repo_dir="$1"\n' + source[start:end], 'asset-test', str(ROOT)],
                env=dict(os.environ, HOME=str(home)),
                capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            tools = home / '.local/share/uu-remote/tools'
            for name, original in [
                ('VERSION', ROOT / 'VERSION'),
                ('uu-tools-fonts.conf', ROOT / 'desktop/uu-tools-fonts.conf'),
                ('uu-full-ready.py', ROOT / 'scripts/uu-full-ready.py'),
                ('uu-manual-plane.py', ROOT / 'scripts/uu-manual-plane.py'),
            ]:
                self.assertEqual((tools / name).read_bytes(), original.read_bytes())


class DebianWineDependenciesTests(unittest.TestCase):
    def test_cold_debian_build_provisions_fallback_even_with_unsupported_docker(self):
        source = (ROOT / 'install.sh').read_text()
        functions = source[source.index('install_winehq() {'):
                           source.index('stop_wine_prefix() {')]
        for options in ('["name=rootless"]', '["name=userns"]', 'unavailable'):
            with self.subTest(docker=options), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                docker = directory / 'docker'
                docker.write_text('#!/bin/sh\n' +
                                  ('exit 1\n' if options == 'unavailable' else
                                   'printf "%s\\n" ' + shlex.quote(options) + '\n'))
                docker.chmod(0o755)
                boundary = '''
sudo() { printf 'SUDO'; printf ' %s' "$@"; printf '\\n'; }
ID=debian
wine_bin=/usr/bin/true
python_bin=/usr/bin/python3
repo_dir="$1"
freerdp_build="$2"
'''
                result = subprocess.run([
                    'bash', '-Eeuo', 'pipefail', '-c',
                    boundary + functions + '\ninstall_packages',
                    'fallback-test', str(ROOT), str(directory / 'missing-runtime')],
                    env=dict(os.environ, UURB_FREERDP_PREBUILT_DIR='',
                             PATH=str(directory) + os.pathsep + os.environ['PATH']),
                    capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                packages = [argument for line in result.stdout.splitlines()
                            if line.startswith('SUDO apt-get install ')
                            for argument in shlex.split(line)[1:]]
                self.assertIn('podman', packages,
                              'An installed Docker CLI must not suppress a usable build fallback')
                self.assertIn('uidmap', packages)

    def test_fresh_and_existing_wine_receive_matching_development_package(self):
        source = (ROOT / 'install.sh').read_text()
        functions = source[source.index('install_winehq() {'):
                           source.index('stop_wine_prefix() {')]
        # Package-manager, key-download and key-conversion boundaries are replaced;
        # both real installer functions still execute their package-selection flow.
        boundary = '''
sudo() { printf 'SUDO'; printf ' %s' "$@"; printf '\\n'; }
curl() { :; }
gpg() { :; }
ID=debian
VERSION_CODENAME=trixie
winehq_sources_url=https://dl.winehq.org/wine-builds/debian/dists/trixie/winehq-trixie.sources
wine_bin="$1"
UURB_FREERDP_PREBUILT_DIR=/already/verified/elsewhere
'''
        for wine in ['/missing-wine-for-fresh-install', '/usr/bin/true']:
            with self.subTest(existing=wine == '/usr/bin/true'):
                result = subprocess.run([
                    'bash', '-Eeuo', 'pipefail', '-c',
                    boundary + functions + '\ninstall_packages', 'package-test', wine],
                    capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                installs = [shlex.split(line)[1:] for line in result.stdout.splitlines()
                            if line.startswith('SUDO apt-get install ')]
                self.assertTrue(any('wine-stable-dev' in argv for argv in installs),
                                'Wine runtime alone does not provide Winelib headers/libraries')


if __name__ == '__main__':
    unittest.main()
