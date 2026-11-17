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
