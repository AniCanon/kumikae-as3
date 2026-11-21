"""Regressions for the normal Web build and preview entry points."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import web_game


class WebGameTest(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "exercises POSIX preview termination")
    def test_preview_cleans_its_export_after_stop_and_exports_in_one_engine_process(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = root / "game"
            project.mkdir()
            (project / "kas3.gdextension").write_text("[libraries]\n")
            (project / "export_presets.cfg").write_text('[preset.0]\nname="Web"\nplatform="Web"\n'
                                                        '[preset.0.options]\nvariant/extensions_support=true\n'
                                                        'variant/thread_support=false\n')
            library = root / "kit/web/libkas3_godot.wasm"
            library.parent.mkdir(parents=True)
            library.touch()
            godot = root / "godot"
            godot.write_text('#!/bin/sh\necho "$@" >> "$WEB_TEST_LOG"\nfor arg do product=$arg; done\n'
                             'case "$product" in *.html) echo game > "$product"; touch "$(dirname "$product")/libkas3_godot.wasm";; esac\n')
            godot.chmod(0o755)
            with patch.dict(os.environ, WEB_TEST_LOG=str(root / "calls"), BROWSER="/usr/bin/true %s", TMPDIR=str(root)):
                with subprocess.Popen([sys.executable, "-I", "-u", TOOLS / "web_game.py", project, root / "kit",
                                       godot, "Web"], stdout=subprocess.PIPE, text=True) as preview:
                    try:
                        self.assertTrue(preview.stdout.readline().startswith("http://127.0.0.1:"))
                    finally:
                        preview.terminate()
                        preview.wait(timeout=10)
            self.assertEqual([], list(root.glob("kumikae-preview-*")))
            self.assertEqual([], list(project.glob(".godot/kumikae-builds/web-*")))
            self.assertEqual(1, len((root / "calls").read_text().splitlines()))
            with patch.dict(os.environ, WEB_TEST_LOG=str(root / "calls")):
                product = web_game.build(project, root / "kit", str(godot), "Web")
            self.assertTrue(product.is_file())
            self.assertEqual(2, len((root / "calls").read_text().splitlines()))
            job = {"platform": "Web", "name": "Web", "filename": "game.html"}
            with patch.dict(os.environ, WEB_TEST_LOG=str(root / "calls")):
                products = web_game.game_export.export(project, root, str(godot), root / "kit", [job])
            self.assertTrue(Path(products[0]).is_file())
            self.assertEqual(3, len((root / "calls").read_text().splitlines()))

    def test_preview_handler_ignores_non_integer_log_codes(self):
        handler = object.__new__(web_game.PreviewHandler)
        handler.log_request("-", "-")
        handler.log_request("invalid", "-")
