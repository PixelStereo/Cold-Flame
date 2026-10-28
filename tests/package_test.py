import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('release_package', Path(__file__).resolve().parents[1] / 'scripts/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def test_missing_platform_plugin_rejects_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            for name in ('bin/cold-flame.exe', 'bin/Qt6Core.dll'):
                file = stage / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.touch()
            with self.assertRaisesRegex(RuntimeError, 'Incomplete windows package'):
                package.validate_payload(stage, 'windows')
            plugin = stage / 'bin/platforms/qwindows.dll'
            plugin.parent.mkdir()
            plugin.touch()
            self.assertEqual(package.validate_payload(stage, 'windows'), stage / 'bin/cold-flame.exe')

    def test_smoke_launch_cannot_use_build_machine_qt(self):
        with tempfile.TemporaryDirectory() as root:
            qt = Path(root) / 'Qt'
            system = Path(root) / 'system'
            with patch.dict(os.environ, {'QT_ROOT_DIR': str(qt), 'QT_PLUGIN_PATH': str(qt / 'plugins'),
                                         'LD_LIBRARY_PATH': str(qt / 'lib'),
                                         'PATH': os.pathsep.join((str(qt / 'bin'), str(system)))}):
                env = package.smoke_environment()
            self.assertNotIn('QT_PLUGIN_PATH', env)
            self.assertNotIn('LD_LIBRARY_PATH', env)
            self.assertEqual(env['PATH'], str(system))
            self.assertEqual(env['QT_QPA_PLATFORM'], 'offscreen')

    def test_portable_zip_keeps_dlls_next_to_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / 'stage'
            (stage / 'bin').mkdir(parents=True)
            (stage / 'bin/cold-flame.exe').write_bytes(b'app')
            (stage / 'bin/Qt6Core.dll').write_bytes(b'qt')
            destination = root / 'app.zip'
            package.zip_payload(stage, destination, 'ColdFlame-test')
            with zipfile.ZipFile(destination) as archive:
                self.assertEqual(archive.read('ColdFlame-test/bin/cold-flame.exe'), b'app')
                self.assertEqual(archive.read('ColdFlame-test/bin/Qt6Core.dll'), b'qt')


if __name__ == '__main__':
    unittest.main()
