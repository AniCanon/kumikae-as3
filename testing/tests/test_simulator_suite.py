from importlib.util import spec_from_file_location, module_from_spec
from pathlib import Path
import unittest


class SuiteExportTest(unittest.TestCase):
    def test_custom_preset_derives_an_isolated_test_application(self):
        spec = spec_from_file_location("suite", Path(__file__).resolve().parents[1] / "simulator_suite.py")
        suite = module_from_spec(spec)
        spec.loader.exec_module(suite)
        game = '[application]\nconfig/name="Other Game"\nrun/main_scene="uid://abc"\n'
        exports = '[preset.3]\nname="QA"\nplatform="iOS"\n[preset.3.options]\napplication/bundle_identifier="org.test.new-id"\n'
        main, cfg, section, identifier = suite.prepare(game, exports, "QA", "res://tests/full.tscn")
        self.assertIn('config/name="Other Game Tests"', main)
        self.assertIn('run/main_scene="res://tests/full.tscn"', main)
        self.assertEqual(("preset.3", "org.test.new-id.tests"), (section, identifier))
        self.assertIn('application/bundle_identifier="org.test.new-id.tests"', cfg)
