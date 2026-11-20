"""The Web size comparison counts the game pack and nested assets on both sides."""
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class WebBundleTest(unittest.TestCase):
    def test_complete_exports_with_identical_runtimes(self):
        with tempfile.TemporaryDirectory() as folder:
            old, new = (Path(folder) / name for name in ("old", "new"))
            totals = []
            for export, pack in ((old, b"old"), (new, b"new pack content")):
                (export / "assets").mkdir(parents=True)
                files = {"index.html": b"html", "engine.wasm": b"wasm", "assets/game.pck": pack}
                for name, data in files.items():
                    (export / name).write_bytes(data)
                totals.append([sum(map(len, files.values())), sum(len(gzip.compress(data, 9)) for data in files.values())])
            result = subprocess.run([sys.executable, Path(__file__).with_name("web_bundle.py"), new, "--against", old],
                                    capture_output=True, text=True, check=True)
            rows = [list(map(int, row.split("\t")[1:])) for row in result.stdout.splitlines() if row.startswith("TOTAL\t")]
            self.assertEqual(rows, totals)
            self.assertIn("assets/game.pck", result.stdout)
            self.assertIn("Change\t+13", result.stdout)
