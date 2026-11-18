"""The files offered by New ActionScript are valid inputs to the shipped compiler."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

TEMPLATES = Path(__file__).resolve().parents[1] / "src/main/resources/fileTemplates/internal"


class AuthoringTemplates(unittest.TestCase):
    def test_new_and_reopened_games_install_the_default_test_target(self):
        plugin = TEMPLATES.parents[2] / "kotlin/app/anicanon/kumikae/as3"
        for entry in ("project/As3ProjectWizard.kt", "run/GodotProjectStartup.kt"):
            with self.subTest(entry=entry):
                self.assertIn("ensureDefaultTests(manager)", (plugin / entry).read_text())

    def test_templates_compile_in_the_selected_package(self):
        with tempfile.TemporaryDirectory() as directory:
            for kind in ("Class", "Interface", "File", "Test Case", "Test Suite", "Node Script"):
                with self.subTest(kind=kind):
                    text = (TEMPLATES / ("AS3 " + kind + ".as.ft")).read_text()
                    self.assertIn("${AS3_PACKAGE}", text)
                    file = Path(directory) / "Sample.as"
                    file.write_text(text.replace("${AS3_PACKAGE}", " game.sample").replace("${NAME}", "Sample"))
                    if compiler := os.environ.get("KAS3C"):
                        result = subprocess.run([compiler, "parse", str(file)], capture_output=True, text=True)
                        self.assertEqual(0, result.returncode, result.stderr)
