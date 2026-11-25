"""Time the same recorded Lantern Reach commands in three headless runtimes."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from xml.etree import ElementTree

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from native_godot import Runner, USED

GAME = HERE.parents[2] / "examples/lantern-reach"
MODES = ("interpreter", "native", "C#")


def check(label, condition, detail):
    if not condition:
        raise SystemExit(f"{label}: {detail}")


def stage_project(build, godot, folder):
    project = folder / "project"
    shutil.copytree(GAME, project, ignore=shutil.ignore_patterns(".godot", ".idea", "bin", "obj"))
    extension = build.parent / "tests/godot_project"
    (project / "bin").mkdir()
    binaries = list((extension / "bin").glob("libkas3_godot.*"))
    check("extension", binaries, f"build the extension in {build}")
    for binary in binaries:
        shutil.copy2(binary, project / "bin")
    settings = ElementTree.Element("configuration")
    sources = ElementTree.SubElement(settings, "packageSources")
    ElementTree.SubElement(sources, "clear")
    cache = os.getenv("NUGET_PACKAGES", str(Path.home() / ".nuget/packages"))
    ElementTree.SubElement(sources, "add", key="local", value=cache)
    ElementTree.ElementTree(settings).write(project / "NuGet.Config")
    subprocess.run(["dotnet", "build", "-m:4", "-p:Optimize=true", "-p:NuGetAudit=false",
                    "--disable-build-servers", "-v:q"], cwd=project, check=True)
    (project / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
    imported = subprocess.run([godot, "--headless", "--editor", "--path", project, "--import"],
                              capture_output=True, text=True, timeout=180)
    report = imported.stdout + imported.stderr
    check("asset import", imported.returncode == 0 and not re.search(r"(ERROR|WARNING):", report), report[-6000:])
    return project


def prepare(build, godot, folder):
    project = stage_project(build, godot, folder)
    runner = Runner(godot, build.parent / "tests/godot_project", HERE.parent, build / "kas3c", check)
    scripts = sorted(path.relative_to(project) for path in (project / "src").rglob("*.as"))
    library = runner.library("Lantern Reach", project, scripts, folder)
    return project, library


def measure(godot, project, library, mode, scenario, ticks):
    env = dict(os.environ, KAS3_NATIVE_PLAY="0")
    env.pop("KAS3_PROGRAM_LIBRARY", None)
    if mode == "native":
        env["KAS3_PROGRAM_LIBRARY"] = library
    args = [godot, "--headless", "--path", project, "--fixed-fps", "60", "--quit-after", str(ticks + 8),
            "--", "--bench", scenario, "--lang", "cs" if mode == "C#" else "as3", "--ticks", str(ticks)]
    run = subprocess.run(args, capture_output=True, text=True, env=env, timeout=max(300, 15 * ticks))
    output = run.stdout + run.stderr
    rows = [line for line in run.stdout.splitlines() if line.startswith("{")]
    check(mode, run.returncode == 0 and len(rows) == 1 and (USED in output) == (mode == "native")
          and not re.search(r"(ERROR|WARNING):", output), output[:6000])
    result = json.loads(rows[0])
    sequence = result["checksums"]
    check(mode, len(sequence) == math.ceil(ticks / 60)
          and all(re.fullmatch("[0-9a-f]{16}", value) for value in sequence), sequence)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--scenario", action="append", choices=sorted(p.stem for p in (GAME / "data/scenarios").glob("*.json")))
    parser.add_argument("--ticks", type=int)
    args = parser.parse_args()
    check("rounds and ticks", args.runs > 0 and (args.ticks is None or args.ticks > 0), "must be positive")
    godot = os.getenv("KAS3_GODOT_DOTNET")
    check("Godot .NET", godot, "set KAS3_GODOT_DOTNET to the Godot 4.7 .NET executable")
    with tempfile.TemporaryDirectory() as scratch:
        project, library = prepare(args.build.resolve(), godot, Path(scratch))
        print("| Scenario | Metric (ms) | Interpreter | Native | C# |", flush=True)
        print("|---|---|---:|---:|---:|")
        for scenario in args.scenario or sorted(p.stem for p in (project / "data/scenarios").glob("*.json")):
            ticks = args.ticks or json.loads((project / "data/scenarios" / (scenario + ".json")).read_text())["ticks"]
            samples = {mode: {} for mode in MODES}
            expected = None
            for turn in range(args.runs):
                for mode in (MODES if turn % 2 == 0 else MODES[::-1]):
                    result = measure(godot, project, library, mode, scenario, ticks)
                    sequence = result["checksums"]
                    detail = (f"peak {result['peak']}, built {result['built']}, "
                              f"destroyed {result['destroyed']}, deaths {result['deaths']}")
                    if expected is None:
                        expected = sequence
                    check(f"{scenario} {mode} checksums", sequence == expected,
                          f"expected {expected}, got {sequence}")
                    metrics = dict(result["systems_ms"], total=result["total_ms"], presentation=result["presentation_ms"])
                    for key, value in metrics.items():
                        samples[mode].setdefault(key, []).append(value)
            for key in samples[MODES[0]]:
                cells = []
                for mode in MODES:
                    times = samples[mode][key]
                    cells.append(f"{statistics.median(times):.3f} [{min(times):.3f}, {max(times):.3f}]")
                print(f"| {scenario} | {key} | " + " | ".join(cells) + " |", flush=True)
            print(f"{scenario}: {len(expected)} matching checkpoints, final {expected[-1]}; {detail}", flush=True)


if __name__ == "__main__":
    main()
