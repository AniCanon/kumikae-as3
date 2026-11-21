#!/bin/sh
# Builds a Godot game written in AS3 for the iOS Simulator, ready to be installed and launched.
#
#   build-ios-simulator.sh <project folder> <output folder> <godot> <simulator template .zip> [<folder with kas3_godot.xcframework> [<preset> [<section> [<expected bundle>]]]]
#
# The last line it prints is the path of the built .app.
#
# The template is one of Godot's iOS export templates with a Simulator library for Apple silicon in it:
# the one Godot publishes has it for Intel only, and a Mac with Apple silicon refuses to run that. It is
# made once from Godot's source (compiler/tests/build_ios_simulator_template.sh in this repository).
set -eu

fail() { echo "$1" >&2; exit 1; }
[ "$#" -ge 4 ] || fail "Usage: build-ios-simulator.sh <project> <output> <godot> <template> [<library> [<preset> [<section> [<expected bundle>]]]]"

project=$1
out=$2
godot=$3
template=$4
library=${5:-}
preset=${6:-iOS}
section=${7:-}
expected_bundle=${8:-}

if [ -n "$section" ]; then
    case "$section" in
        preset.[0-9]*)
            rest=${section#preset.}
            case "$rest" in
                *[!0-9]*) fail "Invalid export preset section: $section" ;;
            esac
            ;;
        *) fail "Invalid export preset section: $section" ;;
    esac
fi

[ -f "$project/project.godot" ] || fail "$project has no project.godot."
[ -f "$project/export_presets.cfg" ] || fail "$project has no export_presets.cfg: add an iOS export preset in Godot (Project > Export)."
[ -f "$template" ] || fail "The Simulator export template was not found at $template. See this script's first lines."
command -v xcodebuild >/dev/null || fail "Xcode is needed to build for the Simulator."

# Import and export a private copy. Godot and Xcode both write into their inputs.
mkdir -p "$out"
out=$(cd "$out" && pwd)

# Acquire an OS-released flock so SIGKILL never leaves a stale lock behind.
if [ -z "${KUMIKAE_SIM_LOCKED:-}" ]; then
    exec python3 -I - "$out/.building.lock" "$0" "$@" <<'LOCK'
import fcntl
import os
import signal
import subprocess
import sys

lock_path, script, *args = sys.argv[1:]
with open(lock_path, "a") as lock:
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        sys.exit(f"Another build is using {os.path.dirname(lock_path)}.")
    env = dict(os.environ, KUMIKAE_SIM_LOCKED="1")
    proc = subprocess.Popen(["/bin/sh", script, *args], env=env, start_new_session=True)

    def forward(signum, _frame):
        try:
            os.killpg(proc.pid, signum)
        except ProcessLookupError:
            pass

    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sig, forward)
    sys.exit(proc.wait())
LOCK
fi

run_timed() {
    mode=$1
    seconds=$2
    shift 2
    python3 -I - "$mode" "$seconds" "$@" <<'RUNNER'
import os
import signal
import subprocess
import sys
import threading

mode, seconds, *cmd = sys.argv[1:]
timeout = int(seconds)
timed_out = threading.Event()
proc = subprocess.Popen(
    cmd,
    stdout=subprocess.PIPE if mode == "godot" else None,
    stderr=subprocess.STDOUT if mode == "godot" else None,
    text=True,
    errors="replace",
    start_new_session=True,
)

def kill_group():
    if proc.poll() is not None:
        return
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(proc.pid, sig)
        except ProcessLookupError:
            return
        try:
            proc.wait(timeout=3)
            return
        except subprocess.TimeoutExpired:
            pass

def on_timeout():
    timed_out.set()
    kill_group()

timer = threading.Timer(timeout, on_timeout)
timer.daemon = True
timer.start()
native_error = ""
missing_runtime = False
try:
    if mode == "godot":
        for line in proc.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            if "ERROR: ActionScript:" in line:
                native_error = native_error or "AS3 compilation failed. See the compiler diagnostics above."
            if "ERROR: Native AS3: " in line:
                native_error = line.split("ERROR: Native AS3: ", 1)[1].strip()
            if "library found for GDExtension:" in line and "kas3.gdextension" in line:
                missing_runtime = True
    proc.wait()
finally:
    timer.cancel()
    if proc.poll() is None:
        kill_group()
if timed_out.is_set():
    sys.exit(f"Command timed out after {timeout}s: {cmd[0]}")
if native_error:
    sys.exit(native_error)
if missing_runtime:
    sys.exit("The export is missing the AS3 runtime.")
sys.exit(proc.returncode)
RUNNER
}

stage=$(mktemp -d "${TMPDIR:-/tmp}/kumikae-game.XXXXXXXX")
trap 'rm -rf "$stage" || true' EXIT
trap 'exit 1' HUP INT TERM
rsync -aL --exclude='/.godot/' --exclude='/.build/' --exclude='/build/' --exclude='/.git/' \
    --exclude='/.idea/' --exclude='/.run/' "$project/" "$stage/"
project=$stage
presets="$project/export_presets.cfg"

if [ -n "$library" ] && [ -d "$library/kas3_godot.xcframework" ]; then
    mkdir -p "$project/bin/ios"
    # The Mac exporter and the iOS runtime share a native program layout.
    if [ -f "$library/../libkas3_godot.dylib" ]; then
        cp "$library/../libkas3_godot.dylib" "$project/bin/libkas3_godot.dylib"
    fi
    rsync -a --delete "$library/kas3_godot.xcframework/" "$project/bin/ios/kas3_godot.xcframework/"
fi
[ -d "$project/bin/ios/kas3_godot.xcframework" ] || fail "The AS3 runtime for iOS is missing."

# The caller normally supplies the parsed section. Older CLI callers use the standard iOS preset.
if [ -z "$section" ]; then
    section=$(KUMIKAE_PRESET=$preset awk '
        {clean=$0; sub(/^[[:space:]]+/, "", clean); sub(/[[:space:]]+$/, "", clean)}
        clean ~ /^\[preset\.[0-9]+\]$/ {s=substr(clean, 2, length(clean)-2)}
        clean == "name=\"" ENVIRON["KUMIKAE_PRESET"] "\"" {print s; exit}
    ' "$presets")
fi
[ -n "$section" ] || fail "There is no export preset named $preset."
KUMIKAE_SIM_TEMPLATE=$template awk -v header="[$section.options]" '
    function escape_ini(v) {
        gsub(/\\/, "\\\\", v)
        gsub(/"/, "\\\"", v)
        return v
    }
    function finish() {
        if (active) print "application/export_project_only=true"
        if (active) print "custom_template/debug=\"" escape_ini(ENVIRON["KUMIKAE_SIM_TEMPLATE"]) "\""
        if (active && !team) print "application/app_store_team_id=\"ABCDE12345\""
    }
    {clean=$0; sub(/^[[:space:]]+/, "", clean); sub(/[[:space:]]+$/, "", clean)}
    clean ~ /^\[/ {finish(); active=(clean==header); if (active) seen=1}
    active && clean ~ /^application\/export_project_only[[:space:]]*=/ {next}
    active && clean ~ /^custom_template\/debug[[:space:]]*=/ {next}
    active && clean ~ /^application\/app_store_team_id[[:space:]]*=/ {if (clean ~ /=[[:space:]]*""/) next; team=1}
    {print}
    END {if (!seen) {print header; active=1} finish()}
' "$presets" > "$presets.new"
mv "$presets.new" "$presets"

mkdir -p "$project/.godot"
if [ -f "$project/kas3.gdextension" ]; then
    echo "res://kas3.gdextension" > "$project/.godot/extension_list.cfg"
fi
run_timed plain 180 "$godot" --headless --path "$project" --editor --quit-after 120 --max-fps 60

name=$(sed -n 's/^config\/name="\(.*\)"$/\1/p' "$project/project.godot" | head -1 | tr -cd 'A-Za-z0-9')
[ -n "$name" ] || name=Game

echo "Exporting the Xcode project with Godot…"
run_timed godot 900 "$godot" --headless --path "$project" --export-debug "$preset" "$out/$name.ipa"
[ -d "$out/$name.xcodeproj" ] || fail "Godot did not write $out/$name.xcodeproj. See its output above."

echo "Building for the Simulator…"
set -- -project "$out/$name.xcodeproj" -scheme "$name" -configuration Debug -sdk iphonesimulator \
    -arch arm64 -derivedDataPath "$out/build" CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY=""
run_timed plain 1800 xcodebuild "$@" -quiet build
run_timed plain 120 xcodebuild "$@" -showBuildSettings -json > "$stage/xcode-settings.json"
app=$(python3 -I - "$stage/xcode-settings.json" "$expected_bundle" <<'PRODUCT'
import json
from pathlib import Path
import plistlib
import sys

with open(sys.argv[1]) as stream:
    settings = [row["buildSettings"] for row in json.load(stream)]
products = [Path(s["TARGET_BUILD_DIR"]) / s["WRAPPER_NAME"] for s in settings
            if s.get("PRODUCT_TYPE") == "com.apple.product-type.application"]
if len(products) != 1 or not products[0].is_dir():
    sys.exit("Xcode did not report a single built game application.")
with (products[0] / "Info.plist").open("rb") as stream:
    identifier = plistlib.load(stream).get("CFBundleIdentifier")
if sys.argv[2] and identifier != sys.argv[2]:
    sys.exit("The built application identifier differs from the selected Godot preset.")
print(products[0])
PRODUCT
)
echo "$app"
