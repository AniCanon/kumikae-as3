"""Run a project's registered Godot test scene on an iOS simulator."""
import argparse
import json
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


def setting(text, group, key, replacement=None):
    start, end = section(text, group)
    body = text[start:end]
    entry = re.search(r"(?m)^[ \t]*" + re.escape(key) + r'[ \t]*=[ \t]*("(?:[^"\\]|\\.)*")', body)
    if replacement is None:
        return json.loads(entry[1]) if entry else ""
    value = json.dumps(replacement, ensure_ascii=False)
    if entry:
        return text[:start + entry.start(1)] + value + text[start + entry.end(1):]
    return text[:end].rstrip() + "\n" + key + "=" + value + "\n\n" + text[end:].lstrip("\r\n")


def prepare(game, exports, preset, scene):
    groups = re.findall(r"(?m)^[ \t]*\[(preset\.\d+)\][ \t]*\r?$", exports)
    matches = [g for g in groups if setting(exports, g, "platform") == "iOS"
               and (not preset or setting(exports, g, "name") == preset)]
    if len(matches) != 1:
        raise ValueError("Choose one iOS export preset with --preset.")
    group = matches[0]
    options = group + ".options"
    identifier = setting(exports, options, "application/bundle_identifier")
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9.-]+", identifier):
        raise ValueError("The selected iOS preset needs a valid bundle identifier.")
    bundle = identifier + ".tests"
    for other in groups:
        if setting(exports, other, "platform") != "iOS":
            continue
        try:
            other_bundle = setting(exports, other + ".options", "application/bundle_identifier")
        except ValueError:
            continue
        if other_bundle == bundle:
            raise ValueError("Another game preset already uses the test bundle identifier.")
    game = setting(game, "application", "config/name", setting(game, "application", "config/name") + " Tests")
    game = setting(game, "application", "run/main_scene", scene)
    exports = setting(exports, group, "exclude_filter", "")
    exports = setting(exports, options, "application/bundle_identifier", bundle)
    return game, exports, group, bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--case", required=True, help="Godot scene target in kas3-tests.json")
    parser.add_argument("--device", required=True)
    parser.add_argument("--preset", default="")
    parser.add_argument("--godot", default="godot")
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
                        str(project) + "/", str(stage)], check=True)
        (stage / "project.godot").write_text(game)
        (stage / "export_presets.cfg").write_text(cfg)
        result = subprocess.run(["/bin/sh", str(build), stage, project / ".godot/ios-tests", args.godot,
                                 args.template.resolve(), args.ios_library.resolve(), setting(cfg, group, "name"), group, bundle],
                                stdout=subprocess.PIPE, text=True)
        print(result.stdout)
        result.check_returncode()
        lines = [line for line in result.stdout.splitlines() if line.strip()]
        if not lines:
            raise ValueError("The Simulator build did not print the built .app path.")
        product = lines[-1].strip()
        subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("simulator_report.py")), product, "--device", args.device,
                        "--evidence", project / ".build/verification"], check=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
