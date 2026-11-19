"""Physical-device export planning never mutates the game's preset."""
import importlib.util
import sys
from pathlib import Path
import unittest
import tempfile
import base64
import hashlib
import plistlib
from types import SimpleNamespace
from unittest.mock import patch

if sys.platform == "win32":
    raise unittest.SkipTest("iOS device builds require Xcode and POSIX file locks")

SPEC = importlib.util.spec_from_file_location("ios_device", Path(__file__).parents[1] / "tools/ios-device.py")
DEVICE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEVICE)


class DeviceBuildTest(unittest.TestCase):
    def test_team_requires_unambiguous_identity(self):
        self.assertEqual(DEVICE.signing_team("", lambda: ['TESTTEAM01']), "TESTTEAM01")
        with self.assertRaisesRegex(ValueError, "signing team"):
            DEVICE.signing_team("", lambda: [])
        with self.assertRaisesRegex(ValueError, "signing team"):
            DEVICE.signing_team("", lambda: ['TESTTEAM01', 'TESTTEAM02'])

    def test_simulator_placeholder_is_never_used_to_sign(self):
        with self.assertRaisesRegex(ValueError, "signing team"):
            DEVICE.signing_team("ABCDE12345", lambda: [])

    def test_signing_team_uses_certificate_ou_not_common_name(self):
        der = b"test certificate"
        digest = hashlib.sha1(der).hexdigest().upper()
        identities = [digest + ' "Apple Development: Developer (PERSON0001)"']
        pem = "-----BEGIN CERTIFICATE-----\n" + base64.b64encode(der).decode() + "\n-----END CERTIFICATE-----"
        teams = DEVICE.certificate_teams(identities, pem, lambda value: "subject=CN=Apple Development: Developer (PERSON0001),OU=TESTTEAM01,O=Example")
        self.assertEqual(teams, ["TESTTEAM01"])
        self.assertEqual(DEVICE.certificate_teams([], pem, lambda value: "OU=TESTTEAM01"), [])

    def test_ios_only_preset_is_updated(self):
        preset = '[preset.0]\nname="Desktop"\nplatform="iOS"\n[preset.0.options]\ncustom_template/debug="keep"\n[preset.1]\nname="Phone"\nplatform="iOS"\n[preset.1.options]\napplication/app_store_team_id=""\n'
        text = DEVICE.prepare_preset(preset, "preset.1", "/template.zip", "TESTTEAM01", "org.example.game")
        self.assertIn('custom_template/debug="keep"', text)
        self.assertIn('application/app_store_team_id="TESTTEAM01"', text)
        self.assertIn('application/export_project_only=true', text)
        self.assertIn('application/bundle_identifier="org.example.game"', text)

    def test_overlapping_builds_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            with DEVICE.build_lock(Path(directory)):
                with self.assertRaisesRegex(ValueError, "already running"):
                    with DEVICE.build_lock(Path(directory)):
                        self.fail("A second build acquired the same output")

    def test_configured_team_does_not_query_keychain(self):
        self.assertEqual(DEVICE.signing_team("TESTTEAM01", self.fail), "TESTTEAM01")

    def test_prebuilt_skips_build_and_launches_only_after_selected_device_accepts_install(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "Game.app"
            app.mkdir()
            (app / "Info.plist").write_bytes(plistlib.dumps({"CFBundleIdentifier": "org.example.game"}))
            args = SimpleNamespace(project=directory, app=str(app), device="phone", bundle="org.example.game")
            with patch.object(DEVICE, "build", side_effect=AssertionError), patch.object(DEVICE, "run") as run, \
                    patch.object(DEVICE.os, "execv") as launch:
                DEVICE.install_and_launch(args)
                self.assertEqual(run.call_args.args[0][-3:], ["--device", "phone", app.resolve()])
                self.assertEqual(launch.call_args.args[1][-5:],
                                 ["--device", "phone", "--console", "--terminate-existing", args.bundle])
                launch.reset_mock()
                run.side_effect = OSError("install failed")
                with self.assertRaisesRegex(OSError, "install failed"):
                    DEVICE.install_and_launch(args)
                launch.assert_not_called()
                args.bundle = "org.example.other"
                run.reset_mock()
                with self.assertRaisesRegex(ValueError, "bundle identifier"):
                    DEVICE.install_and_launch(args)
                run.assert_not_called()

    def test_missing_options_after_last_line_get_their_own_section(self):
        preset = '[preset.0]\nname="Phone"\nplatform="iOS"'
        text = DEVICE.prepare_preset(preset, "preset.0", "", "TESTTEAM01", "org.example.game")
        self.assertIn('platform="iOS"\n[preset.0.options]\n', text)

    def test_godot_export_diagnostic_fails_even_when_godot_exits_successfully(self):
        command = [sys.executable, "-c", "print('ERROR: Native AS3: Invalid game script')"]
        with self.assertRaisesRegex(ValueError, "Invalid game script"):
            DEVICE.run_export(command)

    def test_missing_ios_preset_fails(self):
        with self.assertRaisesRegex(ValueError, "iOS export preset"):
            DEVICE.prepare_preset('', 'preset.0', '/template.zip', 'TESTTEAM01', 'org.example.game')
