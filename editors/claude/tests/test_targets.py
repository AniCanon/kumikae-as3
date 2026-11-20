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


class TargetsTest(unittest.TestCase):
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
