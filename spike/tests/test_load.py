"""The extension loads in Godot 4.7 and says so."""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import godot_bin  # noqa: E402


class LoadTest(unittest.TestCase):
    def test_version_is_4_7(self):
        out = subprocess.run([godot_bin.godot_path(), "--version"], capture_output=True, text=True).stdout
        self.assertTrue(out.strip().startswith("4.7."), out)
        if os.environ.get("KAS3_REQUIRE_DOTNET") == "1":
            self.assertIn("mono", out)

    def test_extension_loads(self):
        godot_bin.ensure_imported()
        result = subprocess.run(
            [godot_bin.godot_path(), "--headless", "--path", os.path.join(ROOT, "project"), "--quit-after", "600",
             "--", "--bench=empty", "--lang=gd", "--mode=verify"],
            capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("kas3_spike loaded", result.stdout)

    def test_missing_binary_message(self):
        env = dict(os.environ, KAS3_GODOT="/nonexistent")
        result = subprocess.run(
            [sys.executable, "-c", "import godot_bin; godot_bin.godot_path()"],
            capture_output=True, text=True, env=env, cwd=os.path.join(ROOT, "tools"))
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(result.stderr.strip().endswith("Godot 4.7 not found: set KAS3_GODOT"), result.stderr)


if __name__ == "__main__":
    unittest.main()
