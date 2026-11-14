#!/bin/sh
# Builds a Godot game written in AS3 for the iOS Simulator, ready to be installed and launched.
#
#   build-ios-simulator.sh <project folder> <output folder> <godot> <simulator template .zip> [<folder with kas3_godot.xcframework>]
#
# The last line it prints is the path of the built .app.
#
# The template is one of Godot's iOS export templates with a Simulator library for Apple silicon in it:
# the one Godot publishes has it for Intel only, and a Mac with Apple silicon refuses to run that. It is
# made once from Godot's source (compiler/tests/build_ios_simulator_template.sh in this repository).
set -eu

project=$1
out=$2
godot=$3
template=$4
library=${5:-}

fail() { echo "$1" >&2; exit 1; }
[ -f "$project/project.godot" ] || fail "$project has no project.godot."
[ -f "$project/export_presets.cfg" ] || fail "$project has no export_presets.cfg: add an iOS export preset in Godot (Project > Export)."
[ -f "$template" ] || fail "The Simulator export template was not found at $template. See this script's first lines."
command -v xcodebuild >/dev/null || fail "Xcode is needed to build for the Simulator."

# The extension, built for iOS. A project made by the wizard does not have it until its first iOS build.
if [ ! -d "$project/bin/ios/kas3_godot.xcframework" ]; then
    [ -n "$library" ] && [ -d "$library/kas3_godot.xcframework" ] ||
        fail "The AS3 extension for iOS (kas3_godot.xcframework) is missing from $project/bin/ios."
    mkdir -p "$project/bin/ios"
    cp -R "$library/kas3_godot.xcframework" "$project/bin/ios/"
    echo "Copied the AS3 extension for iOS into bin/ios."
fi

# Godot takes the template's place from the preset, nowhere else. It is set in the iOS preset's options,
# and only there; a line of the person's own that already names a template is left alone.
presets="$project/export_presets.cfg"
if ! grep -q '^custom_template/debug="..*"' "$presets"; then
    awk -v template="$template" '
        /^\[preset\.[0-9]+\]$/ { section = $0; ios = 0 }
        /^platform="iOS"$/ { ios = 1 }
        /^custom_template\/debug=/ { next }
        { print }
        /^\[preset\.[0-9]+\.options\]$/ && ios { print ""; print "custom_template/debug=\"" template "\"" }
    ' "$presets" > "$presets.new" && mv "$presets.new" "$presets"
    echo "Set the iOS preset's debug template to $template."
fi

# Godot will not export for iOS without a team, even to write the Xcode project. The Simulator does not
# sign anything, so a stand-in is enough there; a real one is needed for a phone.
if grep -q '^application/app_store_team_id=""$' "$presets"; then
    sed -i '' 's/^application\/app_store_team_id=""$/application\/app_store_team_id="ABCDE12345"/' "$presets"
    echo "Set a stand-in team id in the iOS preset: enough for the Simulator, to be replaced for a phone."
fi

name=$(sed -n 's/^config\/name="\(.*\)"$/\1/p' "$project/project.godot" | head -1 | tr -cd 'A-Za-z0-9')
[ -n "$name" ] || name=Game
mkdir -p "$out"

echo "Exporting the Xcode project with Godot…"
"$godot" --headless --path "$project" --export-debug "iOS" "$out/$name.ipa"
[ -d "$out/$name.xcodeproj" ] || fail "Godot did not write $out/$name.xcodeproj. See its output above."

echo "Building for the Simulator…"
xcodebuild -project "$out/$name.xcodeproj" -scheme "$name" -configuration Debug -sdk iphonesimulator \
    -arch arm64 -derivedDataPath "$out/build" CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" -quiet build

app="$out/build/Build/Products/Debug-iphonesimulator/$name.app"
[ -d "$app" ] || fail "The build finished without $app."
echo "$app"
