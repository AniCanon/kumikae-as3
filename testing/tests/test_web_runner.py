"""Unit tests for headless Chromium Web test capture and cleanup."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


web_runner = _load("kas3_web_runner_test", ROOT / "web_runner.py")
web_game = _load("kas3_web_game_test", ROOT.parent / "editors/jetbrains/tools/web_game.py")


class WebRunnerTest(unittest.TestCase):
    def test_requires_head_element(self):
        with tempfile.TemporaryDirectory() as tmp:
            html = Path(tmp) / "index.html"
            html.write_text("<html><body>no head</body></html>")
            with self.assertRaisesRegex(ValueError, "HTML head"):
                web_runner.run(web_game, html, sys.executable, "DONE")

    def test_streams_browser_logs_and_stops_on_marker(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            html = root / "index.html"
            html.write_text("<html><head><title>Test</title></head><body></body></html>")
            fake_chrome = root / "fake-chrome"
            fake_chrome.write_text(
                f"#!{sys.executable}\n"
                "import sys, time, urllib.request\n"
                "url = sys.argv[-1]\n"
                "body = urllib.request.urlopen(url, timeout=5).read().decode()\n"
                "assert 'KAS3_BROWSER_LOG' in body\n"
                "sys.stderr.write('[INFO] \"KAS3_BROWSER_LOG PASS%20discovered%20suite\", source: ' + url + '\\n')\n"
                "sys.stderr.flush()\n"
                "time.sleep(30)\n"
            )
            fake_chrome.chmod(0o755)
            code = web_runner.run(web_game, html, str(fake_chrome), "PASS discovered suite", timeout=10)
            self.assertEqual(0, code)

    def test_times_out_and_stops_hung_browser(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            html = root / "index.html"
            html.write_text("<html><head></head><body></body></html>")
            fake_chrome = root / "hung-chrome"
            fake_chrome.write_text(f"#!{sys.executable}\nimport time\ntime.sleep(30)\n")
            fake_chrome.chmod(0o755)
            with self.assertRaisesRegex(ValueError, "timed out"):
                web_runner.run(web_game, html, str(fake_chrome), "DONE", timeout=0.3)
