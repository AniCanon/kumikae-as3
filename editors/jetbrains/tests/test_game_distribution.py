"""Check the public identity and the supported entry points of the shipped plugin."""
import importlib.util
from pathlib import Path
import tempfile
from unittest.mock import patch
import unittest
import xml.etree.ElementTree as ET


class GameDistributionTest(unittest.TestCase):
    root = Path(__file__).resolve().parents[1]

    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("game_export", cls.root / "tools/game_export.py")
        cls.exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.exporter)

    def test_unified_run_type_is_registered(self):
        plugin = ET.parse(self.root / "src/main/resources/META-INF/plugin.xml")
        names = [node.get("implementation", "").split(".")[-1] for node in plugin.iter("configurationType")]
        self.assertIn("KumikaeRunType", names)

    def test_repository_and_plugin_use_the_product_mark(self):
        logo = self.root / "src/main/resources/META-INF/pluginIcon.svg"
        self.assertIn("Kumikae AS3", logo.read_text())
        self.assertIn("META-INF/pluginIcon.svg", (self.root.parents[1] / "README.md").read_text())

    def test_export_stages_the_runtime_and_leaves_the_game_alone(self):
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
            self.exporter.stage_game(game, stage, runtime, android=True)
            self.assertEqual("[libraries]\n", (game / "kas3.gdextension").read_text())
            self.assertEqual("project credentials", (stage / ".godot/export_credentials.cfg").read_text())
            self.assertIn("android.arm64", (stage / "kas3.gdextension").read_text())
            self.assertEqual(b"runtime", (stage / "bin/android/arm64-v8a/libkas3_godot.so").read_bytes())

    def test_ios_export_updates_the_staged_exporter_and_device_runtime_together(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            game, native = folder / "game", folder / "native"
            host = Path("libkas3_godot.dylib")
            device = Path("ios/kas3_godot.xcframework/slice")
            for directory, content in ((game / "bin", "old"), (native, "new")):
                (directory / device.parent).mkdir(parents=True)
                (directory / host).write_text(content)
                (directory / device).write_text(content)

            def check_staged(command, **kwargs):
                stage = Path(command[command.index("--path") + 1])
                for library in (host, device):
                    self.assertEqual("new", (stage / "bin" / library).read_text())
                    self.assertEqual("old", (game / "bin" / library).read_text())
                raise StopIteration

            with patch.object(self.exporter.subprocess, "run", side_effect=check_staged), self.assertRaises(StopIteration):
                self.exporter.export(game, folder, "godot", native, [{"platform": "iOS"}])

    def test_project_only_export_reports_native_compile_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            game = folder / "game"
            (game / "bin/ios/kas3_godot.xcframework").mkdir(parents=True)
            godot = folder / "godot"
            godot.write_text("""#!/bin/sh
while [ "$#" -gt 0 ]; do
    if [ "$1" = "--export-release" ]; then
        mkdir -p "${3%.ipa}.xcodeproj"
        echo "ERROR: Native AS3: res://Player.as:4: error: unknown type"
        exit 0
    fi
    shift
done
""")
            godot.chmod(0o755)
            with self.assertRaisesRegex(ValueError, "Player.as"):
                self.exporter.export(game, folder, godot, folder / "native",
                              [{"name": "Device", "platform": "iOS", "filename": "Game.ipa"}])
