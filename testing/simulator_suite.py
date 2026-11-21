"""Run a project's registered Godot test scene on an iOS simulator."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def section(text, name):
    header = re.search(r"(?m)^[ \t]*\[" + re.escape(name) + r"\][ \t]*\r?$", text)
    if not header:
        raise ValueError("Missing Godot section " + name)
    next_header = re.search(r"(?m)^[ \t]*\[[^\]\r\n]+\][ \t]*\r?$", text[header.end():])
    return header.end(), header.end() + next_header.start() if next_header else len(text)


def string_entry(body, key):
    return re.search(r"(?m)^[ \t]*" + re.escape(key) + r'[ \t]*=[ \t]*("(?:[^"\\]|\\.)*")', body)


def setting(text, group, key, replacement=None):
    start, end = section(text, group)
    body = text[start:end]
    entry = string_entry(body, key)
    if replacement is None:
        return json.loads(entry[1]) if entry else ""
    value = json.dumps(replacement, ensure_ascii=False)
    if entry:
        return text[:start + entry.start(1)] + value + text[start + entry.end(1):]
    return text[:end].rstrip() + "\n" + key + "=" + value + "\n\n" + text[end:].lstrip("\r\n")


def include_test_folder(exports, group, folder="tests"):
    patterns = setting(exports, group, "exclude_filter").split(",")
    # Keep all exclusions unrelated to the test scene or fixtures.
    test_globs = {base + suffix for base in ("tests", folder.strip("/")) for suffix in ("", "/", "/*", "/**")}
    kept = [pattern for pattern in patterns if pattern.strip().removeprefix("res://") not in test_globs]
    return setting(exports, group, "exclude_filter", ",".join(kept))


def platform_presets(exports, platform):
    sections = re.split(r"(?m)^[ \t]*\[([^\]\r\n]+)\][ \t]*\r?$", exports)
    presets = {}
    for group, body in zip(sections[1::2], sections[2::2]):
        if re.fullmatch(r"preset\.\d+", group):
            target = string_entry(body, "platform")
            if target and json.loads(target[1]) == platform:
                name = string_entry(body, "name")
                if name:
                    presets[group] = json.loads(name[1])
    return presets


def select_preset(presets, platform, requested):
    matches = [(group, name) for group, name in presets.items() if not requested or requested == name]
    if len(matches) != 1:
        raise ValueError(f"Choose one {platform} export preset with --preset. Available presets: {list(presets.values())}")
    return matches[0]


def prepare(game, exports, preset, scene, folder="tests"):
    presets = platform_presets(exports, "iOS")
    group, _ = select_preset(presets, "iOS", preset)
    options = group + ".options"
    identifier = setting(exports, options, "application/bundle_identifier")
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9.-]+", identifier):
        raise ValueError("The selected iOS preset needs a valid bundle identifier.")
    bundle = identifier + ".tests"
    for other in presets:
        try:
            other_bundle = setting(exports, other + ".options", "application/bundle_identifier")
        except ValueError:
            continue
        if other_bundle == bundle:
            raise ValueError("Another game preset already uses the test bundle identifier.")
    game = setting(game, "application", "config/name", setting(game, "application", "config/name") + " Tests")
    game = setting(game, "application", "run/main_scene", scene)
    exports = include_test_folder(exports, group, folder)
    exports = setting(exports, options, "application/bundle_identifier", bundle)
    return game, exports, group, bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--case", required=True, help="Godot scene target in kas3-tests.json")
    parser.add_argument("--device", required=True)
    parser.add_argument("--preset", default="")
    parser.add_argument("--godot", default=os.environ.get("KAS3_GODOT", "godot"))
    kit = Path(__file__).resolve().parents[1]
    parser.add_argument("--template", type=Path, default=kit / "ios-with-arm64-simulator.zip")
    parser.add_argument("--ios-library", type=Path, default=kit / "ios")
    args = parser.parse_args()
    project = args.project.resolve()
    manifest = json.loads((project / "kas3-tests.json").read_text())
    target = next((t for t in manifest.get("tests", []) if t.get("name") == args.case), {})
    scenes = [arg for arg in target.get("command", []) if arg.startswith("res://") and arg.endswith(".tscn")]
    if len(scenes) != 1 or not (project / scenes[0][6:]).is_file():
        parser.error("Select a registered test target with one project scene.")
    game, cfg, group, bundle = prepare((project / "project.godot").read_text(), (project / "export_presets.cfg").read_text(),
                                       args.preset, scenes[0])
    build = kit / "tools/build-ios-simulator.sh"
    if not build.is_file():
        build = kit / "editors/jetbrains/tools/build-ios-simulator.sh"
    with tempfile.TemporaryDirectory(prefix="kas3-test-game-") as temporary:
        stage = Path(temporary)
        subprocess.run(["rsync", "-aL", *["--exclude=/" + name for name in (".git", ".idea", ".run", ".godot", ".build", "build")],
                        str(project) + "/", str(stage)], check=True, timeout=180)
        (stage / "project.godot").write_text(game)
        (stage / "export_presets.cfg").write_text(cfg)
        result = subprocess.run(["/bin/sh", str(build), stage, project / ".godot/ios-tests", args.godot,
                                 args.template.resolve(), args.ios_library.resolve(), setting(cfg, group, "name"), group, bundle],
                                stdout=subprocess.PIPE, text=True, timeout=1200)
        print(result.stdout)
        result.check_returncode()
        lines = [line for line in result.stdout.splitlines() if line.strip()]
        if not lines:
            raise ValueError("The Simulator build did not print the built .app path.")
        product = lines[-1].strip()
        subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("simulator_report.py")), product, "--device", args.device,
                        "--evidence", project / ".build/verification"], check=True, timeout=360)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
