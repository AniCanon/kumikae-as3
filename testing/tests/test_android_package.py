"""Inspect a debug export before installing it on the selected emulator."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

path = Path(__file__).resolve().parents[1] / "android_package.py"
spec = importlib.util.spec_from_file_location("android_package", path)
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class AndroidPackageTest(unittest.TestCase):
    def test_requires_the_as3_extension_for_arm64(self):
        with tempfile.TemporaryDirectory() as tmp:
            apk = Path(tmp) / "game.apk"
            apk.write_bytes(b"incomplete")
            with self.assertRaisesRegex(ValueError, "APK"):
                package.verify_runtime(apk)
            with zipfile.ZipFile(apk, "w") as archive:
                archive.writestr("lib/arm64-v8a/libkas3_program.so", b"")
            with self.assertRaisesRegex(ValueError, "ARM64"):
                package.verify_runtime(apk)
            with zipfile.ZipFile(apk, "a") as archive:
                archive.writestr("lib/arm64-v8a/libkas3_godot.so", b"extension")
            package.verify_runtime(apk)
