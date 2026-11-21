"""Preset selection, device availability and build failures before installation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / directory) for directory in ("editors/claude/bin", "testing", "editors/jetbrains/tools")]
import game
import verification


class TargetsTest(unittest.TestCase):
    def test_web_uses_its_own_preset_without_a_device(self):
        self.assertEqual("Web", game.preset_for(ROOT / "examples/coin-dash", "web", None))
        self.assertEqual("", game.device_for("web", None))

    def test_duplicate_requested_web_preset_is_ambiguous(self):
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder)
            config = '[preset.0]\nname="Browser"\nplatform="Web"\n'
            (project / "export_presets.cfg").write_text(config + config.replace("preset.0", "preset.1"))
            with self.assertRaisesRegex(ValueError, "Choose one Web"):
                game.preset_for(project, "web", "Browser")

    def test_unknown_preset_reports_available_names(self):
        project = ROOT / "examples/coin-dash"
        self.assertEqual("Desktop", game.preset_for(project, "desktop", None))
        with self.assertRaisesRegex(ValueError, "Desktop"):
            game.preset_for(project, "desktop", "Missing")

    def test_unavailable_explicit_ios_device_is_reported_before_build(self):
        with patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0, '{"devices":{}}', '')):
            with self.assertRaisesRegex(ValueError, "booted"):
                game.device_for("ios-simulator", "absent")

    def test_build_with_incomplete_kit_leaves_no_product(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "kit is missing"):
                game.build(root, root, "ios-simulator", "iOS", "godot")
            self.assertFalse((root / ".godot").exists())

    def test_ios_simulator_build_passes_section_and_expected_bundle(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            kit = root / "kit"
            (kit / "ios/kas3_godot.xcframework").mkdir(parents=True)
            (kit / "ios-with-arm64-simulator.zip").touch()
            (kit / "tools").mkdir()
            project = root / "project"
            project.mkdir()
            (project / "export_presets.cfg").write_text(
                '[preset.2]\nname="QA iOS"\nplatform="iOS"\n'
                '[preset.2.options]\napplication/bundle_identifier="org.example.qa"\n'
            )
            built_app = root / "Demo.app"
            built_app.mkdir()
            captured = []

            def fake_run(cmd, **_kwargs):
                captured.append(cmd)
                return subprocess.CompletedProcess(cmd, 0, f"{built_app}\n", "")

            with patch.object(game.subprocess, "run", side_effect=fake_run):
                product = game.build(kit, project, "ios-simulator", "QA iOS", "godot")
            self.assertEqual(built_app, product)
            self.assertEqual(["QA iOS", "preset.2", "org.example.qa"], captured[0][-3:])

    def test_verification_reports_pass_fail_and_not_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "kas3-tests.json").write_text('{"version":1,"tests":[]}')
            with patch.object(verification, "device_for", side_effect=["", ValueError("no sim")]), \
                    patch.object(verification, "import_project"), \
                    patch.object(verification.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)):
                code = verification.verify(root, root, "godot", ("desktop", "ios-simulator"))
            self.assertEqual(1, code)
            with patch.object(verification, "device_for", return_value=""), \
                    patch.object(verification, "import_project"), \
                    patch.object(verification.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)):
                self.assertEqual(0, verification.verify(root, root, "godot", ("desktop",)))
