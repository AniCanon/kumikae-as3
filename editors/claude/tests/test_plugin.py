"""Release package and CLI contract tests, using only local files."""
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PLUGIN = Path(__file__).resolve().parents[1]


class PluginTest(unittest.TestCase):
    def test_language_server_handles_actionscript(self):
        config = json.loads((PLUGIN / ".lsp.json").read_text())["kumikae"]
        self.assertEqual({".as": "actionscript"}, config["extensionToLanguage"])
        self.assertEqual("lsp", config["args"][-1])

    def test_missing_project_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = subprocess.run([sys.executable, PLUGIN / "bin/kumikae", "build", "desktop"],
                                 cwd=tmp, capture_output=True, text=True)
            self.assertNotEqual(0, run.returncode)
            self.assertIn("project.godot", run.stderr)

    def test_new_preserves_existing_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = subprocess.run([sys.executable, PLUGIN / "bin/kumikae", "new", tmp],
                                 capture_output=True, text=True)
            self.assertNotEqual(0, run.returncode)
            self.assertIn("already exists", run.stderr)

    def test_starter_web_preset_matches_the_runtime(self):
        for has_web in (False, True):
            with self.subTest(has_web=has_web), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                native = root / "native"
                native.mkdir()
                for name in ("kas3c", "libkas3_godot.dylib"):
                    (native / name).touch()
                wasm = Path("web/libkas3_godot.wasm")
                if has_web:
                    (native / wasm.parent).mkdir()
                    (native / wasm).write_bytes(b"test runtime")
                market = root / "market"
                subprocess.run([sys.executable, "-I", PLUGIN / "package.py", native, market], check=True)
                game = root / "game"
                subprocess.run([market / "plugins/kumikae/bin/kumikae", "new", game], check=True, capture_output=True)
                presets = (game / "export_presets.cfg").read_text()
                self.assertIn('platform="Android"', presets)
                self.assertEqual(has_web, 'platform="Web"' in presets)
                self.assertEqual(has_web, "web.wasm32" in (game / "kas3.gdextension").read_text())
                self.assertEqual(has_web, (game / "bin" / wasm).is_file())
                if has_web:
                    self.assertEqual(b"test runtime", (game / "bin" / wasm).read_bytes())

    def test_relocated_package_runs_its_own_compiler_from_a_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            native = root / "native"
            native.mkdir()
            compiler = native / "kas3c"
            compiler.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            compiler.chmod(0o755)
            (native / "libkas3_godot.dylib").touch()
            packaged = root / "original"
            subprocess.run([sys.executable, "-I", PLUGIN / "package.py", native, packaged], check=True)
            relocated = packaged.rename(root / "market place")
            plugin = relocated / "plugins/kumikae"
            tool = plugin / "bin/kumikae"
            game = root / "standalone game"
            subprocess.run([tool, "new", game], check=True, stdout=subprocess.PIPE)
            scene = (game / "main.tscn").read_text()
            for resource in re.findall(r'path="res://([^"]+)"', scene):
                self.assertTrue((game / resource).is_file(), resource)
            rules = game / "src/dev/kumikae/coindash/config/RoundRules.as"
            self.assertIn("package dev.kumikae.coindash.config", rules.read_text())
            self.assertTrue((game / "tests/dev/kumikae/coindash/tests/CoinPickupTest.as").is_file())
            self.assertTrue((game / "tests/framework/app/anicanon/kumikae/testing/TestScene.as").is_file())
            second = root / "another" / game.name
            subprocess.run([tool, "new", second], check=True, stdout=subprocess.PIPE)
            ids = []
            for project in (game, second):
                settings = (project / "export_presets.cfg").read_text()
                values = re.findall(r'(?:application/bundle_identifier|package/unique_name)="([^"]+)"', settings)
                self.assertEqual(3, len(values))
                self.assertEqual(1, len(set(values)))
                self.assertTrue(values[0].startswith("dev.kumikae."))
                ids.append(values[0])
            self.assertNotEqual(*ids)
            config = json.loads((plugin / ".lsp.json").read_text())["kumikae"]
            command = config["command"].replace("${CLAUDE_PLUGIN_ROOT}", str(plugin))
            result = subprocess.run([command, *config["args"]], cwd=game, check=True, capture_output=True, text=True)
            self.assertEqual("lsp\n", result.stdout)
