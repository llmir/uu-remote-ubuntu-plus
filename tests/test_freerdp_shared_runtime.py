"""Optional real Wine integration against the independently verified relay."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINE = '/opt/wine-stable/bin/wine'
WINESERVER = '/opt/wine-stable/bin/wineserver'


@unittest.skipUnless(os.environ.get('UURB_TEST_FREERDP_WINE') == '1',
                     'set UURB_TEST_FREERDP_WINE=1 for the real relay integration')
class SharedFreeRDPRuntimeTests(unittest.TestCase):
    def test_exports_use_the_colocated_loaded_core_and_reject_a_foreign_directory(self):
        runtime = ROOT / 'build/freerdp'
        verified = subprocess.run([
            'python3', str(ROOT / 'scripts/verify-freerdp-runtime.py'), str(runtime)],
            capture_output=True, text=True, timeout=20)
        self.assertEqual(verified.returncode, 0, verified.stderr)
        with tempfile.TemporaryDirectory(prefix='uu shared core ') as temporary:
            directory = Path(temporary)
            binary_dir = directory / 'runtime'
            binary_dir.mkdir()
            for library in runtime.glob('*.dll'):
                shutil.copy2(library, binary_dir / library.name)
            probe = binary_dir / 'probe.exe'
            compiled = subprocess.run([
                'x86_64-w64-mingw32-gcc', '-std=c11', '-O2', '-Wall', '-Wextra',
                '-Werror', '-D__STDC_NO_THREADS__=1', '-D_WIN32_WINNT=0x0601',
                '-isystem', str(ROOT / 'vendor/freerdp-input-sdk/include'),
                '-I', str(ROOT / 'src'), '-municode', '-o', str(probe),
                str(ROOT / 'tests/probes/uu_freerdp_shared_probe.c'),
                str(ROOT / 'src/freerdp-adapter.c')],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            foreign = binary_dir / 'foreign'
            foreign.mkdir()
            shutil.copy2(runtime / 'libfreerdp3.dll', foreign / 'libfreerdp3.dll')
            environment = dict(os.environ, WINEPREFIX=str(directory / 'prefix'),
                               WINEDEBUG='-all', WINEDLLOVERRIDES='mscoree,mshtml=')
            try:
                for core, expected in [('libfreerdp3.dll', 0),
                                       ('foreign\\libfreerdp3.dll', 1)]:
                    with self.subTest(core=core):
                        result = subprocess.run([WINE, str(probe), core],
                                                cwd=binary_dir, env=environment,
                                                capture_output=True, text=True, timeout=45)
                        self.assertEqual(result.returncode, expected,
                                         result.stdout + result.stderr)
            finally:
                subprocess.run([WINESERVER, '-k'], env=environment,
                               capture_output=True, timeout=10)
                subprocess.run([WINESERVER, '-w'], env=environment,
                               capture_output=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
