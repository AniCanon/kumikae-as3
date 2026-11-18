"""Android deployment chooses a connected device by serial and a launch activity from the APK."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import unittest


class AndroidRunTest(unittest.TestCase):
    def test_package_and_activity_come_from_the_built_apk(self):
        spec = spec_from_file_location("android_run", Path(__file__).resolve().parents[1] / "tools/android_run.py")
        tool = module_from_spec(spec)
        spec.loader.exec_module(tool)
        self.assertEqual(("org.example.other", "org.godot.Game"), tool.launch_activity(
            "package: name='org.example.other' versionCode='3'\nlaunchable-activity: name='org.godot.Game' label=''"))
        self.assertEqual(("org.example.other", None), tool.launch_activity("package: name='org.example.other'"))
        with self.assertRaises(ValueError):
            tool.launch_activity("")

    def test_install_waits_for_the_selected_device_after_godot_exits(self):
        from unittest.mock import patch
        spec = spec_from_file_location("android_run", Path(__file__).resolve().parents[1] / "tools/android_run.py")
        tool = module_from_spec(spec)
        spec.loader.exec_module(tool)
        with patch.object(tool.subprocess, "run") as run:
            tool.install(["adb", "-s", "emulator-5554"], "game.apk")
            self.assertEqual(["adb", "-s", "emulator-5554", "wait-for-device"], run.call_args_list[0].args[0])
            self.assertEqual(["adb", "-s", "emulator-5554", "install", "-r", "game.apk"], run.call_args_list[1].args[0])
