"""The simulator export must build the selected Godot preset in isolation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class PresetBuildTest(unittest.TestCase):
    def test_selected_preset_build_uses_copy_and_preserves_other_presets(self):
        script = Path(__file__).resolve().parents[1] / "tools/build-ios-simulator.sh"
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            game = base / "My Game"
            game.mkdir()
            (game / "project.godot").write_text('[application]\nconfig/name="Example"\n')
            config = ('[preset.0]\nname="Other"\nplatform="iOS"\n[preset.0.options]\n'
                      'custom_template/debug="other.zip"\napplication/app_store_team_id=""\n'
                      '[preset.1]\nname="QA Phone"\nplatform="iOS"\n[preset.1.options]\n'
                      'application/bundle_identifier="org.example.qa"\napplication/app_store_team_id="KEEP123456"\napplication/export_project_only=false\n')
            config = config.replace("[preset.1.options]", "  [preset.1.options]  ")
            (game / "export_presets.cfg").write_bytes(config.replace("\n", "\r\n").encode())
            desktop = "libkas3_godot.dylib"
            (game / "bin").mkdir()
            (game / "bin" / desktop).write_bytes(b"old exporter")
            lib = base / "native/ios"
            (lib / "kas3_godot.xcframework").mkdir(parents=True)
            (lib.parent / desktop).write_bytes(b"matching exporter")
            template = base / "sim.zip"
            template.touch()
            godot = base / "godot"
            godot.write_text("""#!/bin/sh
while [ "$#" -gt 0 ]; do
  case "$1" in
    --path) stage=$2; shift 2;;
    --export-debug)
      [ "$2" = "QA Phone" ] || exit 15
      [ "$stage" != "$GAME" ] || exit 16
      [ "$(cat "$stage/bin/libkas3_godot.dylib")" = "matching exporter" ] || { echo "Staged exporter is stale"; exit 17; }
      cp "$stage/export_presets.cfg" "$STAGED"
      mkdir -p "${3%.ipa}.xcodeproj"
      exit;;
    *) shift;;
  esac
done
""")
            xcode = base / "xcodebuild"
            xcode.write_text("""#!/bin/sh
case " $* " in *" -showBuildSettings "*) settings=yes;; *) settings=no;; esac
while [ "$1" != "-derivedDataPath" ]; do shift; done
dir="$2/Products"
mkdir -p "$dir/Custom Product.app" "$dir/Example.app"
cat > "$dir/Custom Product.app/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.qa</string></dict></plist>
PLIST
if [ "$settings" = yes ]; then
  printf '[{"buildSettings":{"PRODUCT_TYPE":"com.apple.product-type.application","TARGET_BUILD_DIR":"%s","WRAPPER_NAME":"Custom Product.app"}}]' "$dir"
fi
""")
            for tool in (godot, xcode):
                tool.chmod(0o755)
            staged = base / "observed.cfg"
            env = dict(os.environ, PATH=f"{base}:{os.environ['PATH']}", GAME=str(game), STAGED=str(staged))
            result = subprocess.run([script, game, game / ".godot/ios-simulator", godot, template, lib,
                                     "QA Phone", "preset.1", "org.example.qa"], env=env, capture_output=True, text=True)
            self.assertEqual(b"old exporter", (game / "bin" / desktop).read_bytes())
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(config, (game / "export_presets.cfg").read_text())
            self.assertFalse((game / "bin/ios").exists())
            first, second = staged.read_text().split("[preset.1]")
            self.assertIn('application/app_store_team_id=""', first)
            self.assertIn('custom_template/debug="other.zip"', first)
            self.assertIn(str(template), second)
            self.assertIn('application/app_store_team_id="KEEP123456"', second)
            self.assertEqual(1, second.count("[preset.1.options]"))
            self.assertIn("application/export_project_only=true", second)
            self.assertEqual("Custom Product.app", Path(result.stdout.strip().splitlines()[-1]).name)
            self.assertTrue(Path(result.stdout.strip().splitlines()[-1]).is_dir())
