"""Check the adopted source identity and reject untrusted runtime/cache inputs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / 'scripts/verify-freerdp-runtime.py'


class FreeRDPProductTests(unittest.TestCase):
    def test_pinned_product_recipe_validates_and_lists_complete_closure(self):
        result = subprocess.run(['/usr/bin/python3', str(VALIDATOR), '--mode', 'profile'],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run(['/usr/bin/python3', str(VALIDATOR), '--mode', 'list'],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        files = result.stdout.splitlines()
        self.assertEqual(len(files), 13)
        self.assertIn('ossl-modules/legacy.dll', files)
        self.assertIn('libfreerdp-client3.dll', files)

    def test_modified_recipe_rejected_before_output_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            for relative in ('scripts/verify-freerdp-runtime.py', 'patches/freerdp-sdl-product.json',
                             'src/winpr_sspi_shim.c', 'scripts/build-compat.sh'):
                target = repo / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / relative, target)
            shutil.copytree(ROOT / 'vendor/freerdp-sdl-build', repo / 'vendor/freerdp-sdl-build')
            with (repo / 'vendor/freerdp-sdl-build/freerdp-sdl-owner-refresh.patch').open('a') as stream:
                stream.write('\nmodified\n')
            result = subprocess.run(['/usr/bin/python3', str(repo / 'scripts/verify-freerdp-runtime.py'),
                                     '--mode', 'profile'], capture_output=True, text=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Recipe identity mismatch', result.stderr)

    def test_generated_manifest_cannot_approve_replacement_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / 'sdl-freerdp.exe').write_bytes(b'MZ unapproved executable')
            (output / '.source-provenance.json').write_text(json.dumps({'approval': 'APPROVED_SOURCE_BUILD_CLOSURE'}))
            (output / '.build-sha256').write_text('self-written hash\n')
            result = subprocess.run(['/usr/bin/python3', str(VALIDATOR), str(output)],
                                    capture_output=True, text=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('PE hash mismatch: sdl-freerdp.exe', result.stderr)

    def test_missing_prebuilt_closure_rejected_without_copy_or_cold_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output, work = root / 'source', root / 'output', root / 'work'
            source.mkdir()
            result = subprocess.run([str(ROOT / 'scripts/build-winpr.sh'), str(output)],
                                    env=dict(os.environ, UURB_FREERDP_PREBUILT_DIR=str(source),
                                             UURB_BUILD_DIR=str(work)),
                                    capture_output=True, text=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Missing or symbolic-link input', result.stderr)
            self.assertFalse(output.exists())
            self.assertFalse(work.exists())


@unittest.skipUnless(shutil.which('x86_64-w64-mingw32-gcc') and
                     shutil.which('x86_64-w64-mingw32-objdump'), 'MinGW required')
class FullInputLoaderTests(unittest.TestCase):
    def test_installer_builds_the_prefixed_addin_as_a_canonical_export_forwarder(self):
        source = (ROOT / 'install.sh').read_text()
        start = source.index('"$repo_dir/scripts/build-compat.sh" "$compat_build"')
        start = source.index('\n', start) + 1
        end = source.index('if [[ "$ID" == debian', start)
        with tempfile.TemporaryDirectory(prefix='uu loader ') as directory:
            result = subprocess.run([
                'bash', '-Eeuo', 'pipefail', '-c',
                'repo_dir="$1"\ncompat_build="$2"\n' + source[start:end],
                'loader-test', str(ROOT), directory],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            loader = Path(directory) / 'libuurb-full-input-client.dll'
            self.assertTrue(loader.is_file(), 'The pinned FreeRDP loader requires the lib prefix')
            information = subprocess.run([
                'x86_64-w64-mingw32-objdump', '-p', str(loader)],
                check=True, capture_output=True, text=True).stdout
            self.assertIn('Forwarder RVA -- uurb-full-input-client.DVCPluginEntry', information)
            self.assertIn('DVCPluginEntry', information.partition('[Ordinal/Name Pointer] Table')[2])


if __name__ == '__main__':
    unittest.main()
