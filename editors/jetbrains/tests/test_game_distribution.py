"""Check the public identity and the supported entry points of the shipped plugin."""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


class GameDistributionTest(unittest.TestCase):
    root = Path(__file__).resolve().parents[1]

    def test_unified_run_type_is_registered(self):
        plugin = ET.parse(self.root / "src/main/resources/META-INF/plugin.xml")
        names = [node.get("implementation", "").split(".")[-1] for node in plugin.iter("configurationType")]
        self.assertIn("KumikaeRunType", names)

    def test_repository_and_plugin_use_the_product_mark(self):
        logo = self.root / "src/main/resources/META-INF/pluginIcon.svg"
        self.assertIn("Kumikae AS3", logo.read_text())
        self.assertIn("META-INF/pluginIcon.svg", (self.root.parents[1] / "README.md").read_text())

    def test_export_stages_the_runtime_and_leaves_the_game_alone(self):
        import importlib.util
        import tempfile
        spec = importlib.util.spec_from_file_location("game_export", self.root / "tools/game_export.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            game = folder / "source"
            game.mkdir()
            (game / "kas3.gdextension").write_text("[libraries]\n")
            runtime = folder / "native/android/arm64-v8a/libkas3_godot.so"
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"runtime")
            local = game / ".godot"
            local.mkdir()
            (local / "export_credentials.cfg").write_text("project credentials")
            stage = folder / "stage"
            module.stage_game(game, stage, runtime, android=True)
            self.assertEqual("[libraries]\n", (game / "kas3.gdextension").read_text())
            self.assertEqual("project credentials", (stage / ".godot/export_credentials.cfg").read_text())
            self.assertIn("android.arm64", (stage / "kas3.gdextension").read_text())
            self.assertEqual(b"runtime", (stage / "bin/android/arm64-v8a/libkas3_godot.so").read_bytes())
