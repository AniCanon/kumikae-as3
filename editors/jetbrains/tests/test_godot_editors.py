from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


class GodotEditorsTest(unittest.TestCase):
    def test_gdscript_and_scene_have_native_editor_providers(self):
        xml = Path(__file__).resolve().parents[1] / 'src/main/resources/META-INF/plugin.xml'
        providers = [node.get('implementation').split('.')[-1] for node in ET.parse(xml).iter('fileEditorProvider')]
        self.assertIn('SceneEditorProvider', providers)
        self.assertIn('GdScriptEditorProvider', providers)

    def test_live_pictures_bypass_composition(self):
        panel = Path(__file__).resolve().parents[1] / 'src/main/kotlin/app/anicanon/kumikae/as3/panel'
        self.assertNotIn('LaunchedEffect(controller.frame)', (panel / 'DevicePage.kt').read_text())

    def test_export_preset_file_uses_the_godot_target_editor(self):
        settings = Path(__file__).resolve().parents[1] / 'src/main/kotlin/app/anicanon/kumikae/as3/settings'
        self.assertIn('"export_presets.cfg"', (settings / 'ProjectSettingsEditorProvider.kt').read_text())

    def test_android_and_ios_use_the_mapped_newest_frame_reader(self):
        panel = Path(__file__).resolve().parents[1] / 'src/main/kotlin/app/anicanon/kumikae/as3/panel'
        self.assertIn('NativeFrameReader', (panel / 'AndroidDisplayFeed.kt').read_text())
