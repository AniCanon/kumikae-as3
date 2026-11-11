"""The C# benchmarks do the same work as the GDScript ones. Needs the Godot .NET editor (KAS3_GODOT)."""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import godot_bin  # noqa: E402
import run_bench  # noqa: E402


def has_dotnet_godot() -> bool:
    out = subprocess.run([godot_bin.godot_path(), "--version"], capture_output=True, text=True).stdout
    return "mono" in out


@unittest.skipUnless(os.environ.get("KAS3_REQUIRE_DOTNET") == "1" or has_dotnet_godot(),
                     "needs the Godot .NET editor: set KAS3_GODOT")
class ParityCsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        godot_bin.ensure_imported()

    def test_godot_is_dotnet_build(self):
        self.assertTrue(has_dotnet_godot(), "KAS3_GODOT must point at a Godot .NET editor")

    def test_parity_with_gdscript(self):
        for bench in run_bench.BENCHES:
            with self.subTest(bench=bench):
                run_bench.parity(bench, ["gd", "cs"])


if __name__ == "__main__":
    unittest.main()
