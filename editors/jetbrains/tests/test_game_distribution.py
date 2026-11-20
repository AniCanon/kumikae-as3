"""Check the public identity and the supported entry points of the shipped plugin."""
import contextlib
import io
import importlib.util
import zipfile
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

    def test_only_the_selected_web_preset_needs_single_threaded_extension_support(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            cfg = stage / "export_presets.cfg"
            cfg.write_text('[preset.0]\nname="Other"\nplatform="Web"\n'
                           '[preset.1]\nname="Browser"\nplatform="Web"\n'
                           '[preset.1.options]\nvariant/extensions_support=true\nvariant/thread_support=false\n')
            self.exporter.validate_web_preset(stage, "Browser")
            with self.assertRaisesRegex(ValueError, "Enable GDExtension"):
                self.exporter.validate_web_preset(stage, "Other")
            cfg.write_text(cfg.read_text().replace("thread_support=false", "thread_support=true"))
            with self.assertRaisesRegex(ValueError, "disable threads"):
                self.exporter.validate_web_preset(stage, "Browser")

    def test_web_export_stages_legacy_and_custom_runtimes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = root / "native"
            wasm = native / "web/libkas3_godot.wasm"
            wasm.parent.mkdir(parents=True)
            wasm.write_bytes(b"bundled")
            project = root / "game"
            project.mkdir()
            descriptor = project / "kas3.gdextension"
            descriptor.write_text("[libraries]\n")
            stage = root / "stage"
            self.exporter.stage_game(project, stage, None)
            self.exporter.stage_web_runtime(stage, native)
            self.assertEqual(b"bundled", (stage / "bin/web/libkas3_godot.wasm").read_bytes())
            self.assertIn('web.wasm32 = "res://bin/web/libkas3_godot.wasm"', (stage / descriptor.name).read_text())
            self.assertEqual("[libraries]\n", descriptor.read_text())
            (stage / descriptor.name).write_text('[libraries]\nweb.wasm32 = "res://custom.wasm"\n')
            with self.assertRaisesRegex(ValueError, "custom.wasm"):
                self.exporter.stage_web_runtime(stage, native)
            (stage / "custom.wasm").write_bytes(b"custom")
            self.assertEqual({"custom.wasm"}, self.exporter.stage_web_runtime(stage, native))
            self.assertEqual(b"custom", (stage / "custom.wasm").read_bytes())

    def test_web_library_feature_tags_preserve_debug_and_release_overrides(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            descriptor = stage / "kas3.gdextension"
            original = ('[libraries]\nweb.debug.wasm32 = "res://debug.wasm"\n'
                        'wasm32.web.release = "release.wasm"\n[icons]\nweb = "res://icon.svg"\n')
            descriptor.write_text(original)
            for name in ("debug.wasm", "release.wasm"):
                (stage / name).touch()
            self.assertEqual({"debug.wasm", "release.wasm"}, self.exporter.stage_web_runtime(stage, stage))
            self.assertEqual(original, descriptor.read_text())

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

    def test_selected_upload_key_inside_a_game_is_omitted_from_the_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            key = root / "game" / "credentials" / "release.p12"
            key.parent.mkdir(parents=True)
            key.write_bytes(b"fixture")
            (root / "game" / "assets").mkdir()
            (root / "game/assets/link").symlink_to(key)
            self.exporter.stage_game(root / "game", root / "stage", root / "native", private_files=[key])
            self.assertFalse((root / "stage/credentials/release.p12").exists())
            self.assertFalse((root / "stage/assets/link").exists())
            self.assertTrue(key.exists())

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

    def test_store_build_overrides_only_the_selected_staged_preset(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            presets = stage / "export_presets.cfg"
            presets.write_text('[preset.0.options]\ngradle_build/export_format=0\n'
                               '[preset.1.options]\ngradle_build/export_format=0\npackage/signed=false\n')
            job = {"platform": "Android", "section": "preset.1", "store": True}
            flags = self.exporter.prepare_store(stage, job)
            self.assertIn("--install-android-build-template", flags)
            first, second = presets.read_text().split("[preset.1.options]")
            self.assertIn("gradle_build/export_format=0", first)
            for option in ["gradle_build/use_gradle_build=true", "gradle_build/export_format=1", "package/signed=true"]:
                self.assertIn(option, second)
            (stage / "android/build").mkdir(parents=True)
            self.assertEqual([], self.exporter.prepare_store(stage, job))

    def test_ipa_job_requires_an_ipa_even_when_xcode_project_exists(self):
        with tempfile.TemporaryDirectory() as temporary:
            ipa = Path(temporary) / "Game.ipa"
            ipa.with_suffix(".xcodeproj").mkdir()
            self.assertIsNone(self.exporter.product(ipa, {"platform": "iOS", "projectOnly": False}))
            self.assertEqual(ipa.with_suffix(".xcodeproj"), self.exporter.product(ipa, {"platform": "iOS", "projectOnly": True}))


    def test_android_release_upgrades_the_staged_standard_runtime_and_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game, native = root / "game", root / "native"
            relative = Path("android/arm64-v8a/libkas3_godot.so")
            for folder, content in ((game / "bin", "old"), (native, "new")):
                (folder / relative.parent).mkdir(parents=True)
                (folder / relative).write_text(content)
                (folder / "libkas3_godot.dylib").write_text(content)
            (game / "kas3.gdextension").write_text('[libraries]\nandroid.arm64 = "res://bin/' + relative.as_posix() + '"\n')
            with patch.object(self.exporter.subprocess, "run") as engine:
                def imported(command, **kwargs):
                    stage = Path(command[command.index("--path") + 1])
                    self.assertEqual("new", (stage / "bin" / relative).read_text())
                    self.assertEqual("new", (stage / "bin/libkas3_godot.dylib").read_text())
                    raise StopIteration
                engine.side_effect = imported
                with self.assertRaises(StopIteration):
                    self.exporter.export(game, root, "godot", native, [{"platform": "Android"}])
            self.assertEqual("old", (game / "bin" / relative).read_text())

    def test_unsigned_bundle_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "app.aab"
            with zipfile.ZipFile(path, "w") as bundle:
                bundle.writestr("base/manifest/AndroidManifest.xml", b"manifest")
            with self.assertRaisesRegex(ValueError, "unsigned"):
                self.exporter.verify_play_bundle(path)

    def test_signing_override_is_scoped_and_masked_in_the_console(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            game = folder / "game"
            game.mkdir()
            (game / "kas3.gdextension").write_text('[libraries]\nandroid.arm64 = "res://custom.so"\n')
            engine = folder / "godot"
            engine.write_text("""#!/bin/sh
while [ "$#" -gt 0 ]; do
    if [ "$1" = "--export-release" ]; then
        echo "$GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD"
        if [ "$2" = "Desktop" ] && [ -n "$GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD" ]; then exit 1; fi
        touch "$3"
        exit 0
    fi
    shift
done
""")
            engine.chmod(0o755)
            jobs = [{"platform": "Android", "section": "preset.0", "name": "Phone", "filename": "game.apk"},
                    {"platform": "Linux", "section": "preset.1", "name": "Desktop", "filename": "game.x86_64"}]
            secret = "fixture-secret"
            keys = {"preset.0": {"path": "fixture", "user": "upload", "password": secret}}
            console = io.StringIO()
            with patch.dict(self.exporter.os.environ, {"GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD": ""}), contextlib.redirect_stdout(console):
                products = self.exporter.export(game, folder, engine, folder / "native", jobs, keys)
            self.assertEqual(["game.apk", "game.x86_64"], [Path(product).name for product in products])
            self.assertNotIn(secret, console.getvalue())
            self.assertIn("••••", console.getvalue())
