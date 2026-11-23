"""Compare checked by-name operations in one headless Godot .NET project per build."""
import argparse
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from native_godot import Runner, USED

ROWS = [f"{kind}/{op}" for kind in ("node", "inherited", "gdscript", "script", "value", "alias", "aliasTyped", "proven", "typed", "mixed")
        for op in ("read", "write", "call") if (kind, op) != ("value", "write")]


TOLERANCE = 0.03


def failures(samples, baseline):
    misses = []
    for row, modes in samples.items():
        def compare(left, right, label):
            candidate, reference = statistics.median(left), statistics.median(right)
            if candidate > reference * (1 + TOLERANCE):
                misses.append(f"{row}: {label} ({candidate:.3f} vs {reference:.3f} ms, tolerance {TOLERANCE:.0%})")
        if "native" in modes and "interpreter" in modes:
            compare(modes["native"], modes["interpreter"], "native slower than interpreter")
        for mode in ("interpreter", "native"):
            if mode in modes and "C#" in modes and not row.startswith(("aliasTyped/", "typed/")):
                compare(modes[mode], modes["C#"], f"{mode} slower than C#")
            if baseline is not None and mode in modes:
                compare(modes[mode], baseline[row][mode], f"{mode} slower than main")
            kind, op = row.split("/")
            if kind == "proven" and mode in modes:
                compare(modes[mode], samples[f"typed/{op}"][mode], f"{mode} slower than typed")
    return misses


def check(label, ok, output):
    if not ok:
        raise SystemExit(f"{label}: {output}")


def prepare(godot, build, where):
    tests = build.parent / "tests"
    runner = Runner(godot, tests / "godot_project", tests, build / "kas3c", check)
    source = HERE / "by_name"
    project = Path(runner.project(where, source, ["Matrix.as", "Managed.cs", "matrix.gd", "foreign.gd",
                                  str(HERE / "languages/project.godot"), str(HERE / "languages/CmpCs.csproj")]))
    cache = Path(os.environ.get("NUGET_PACKAGES", Path.home() / ".nuget/packages"))
    # Restore only from the developer's installed packages.
    (project / "NuGet.Config").write_text('<configuration><packageSources><clear/>'
        f'<add key="local" value="{cache}"/></packageSources></configuration>')
    subprocess.run(["dotnet", "build", "CmpCs.csproj", "-v:q", "-p:Optimize=true", "-p:NuGetAudit=false"],
                   cwd=project, check=True)
    library = runner.library("by-name matrix", project, ["Matrix.as"], where)
    return project, library


def measure(godot, project, library, mode, rows, baseline):
    env = dict(os.environ, KAS3_NATIVE_PLAY="0")
    env.pop("KAS3_PROGRAM_LIBRARY", None)
    if mode == "native":
        env["KAS3_PROGRAM_LIBRARY"] = library
    wanted = [row for row in rows if mode != "C#" or not row.startswith("value/")]
    cmd = [godot, "--headless", "--path", project, "-s", "matrix.gd", "--",
           "cs" if mode == "C#" else "self" if baseline else "as", "20000", *wanted]
    run = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=90)
    output = run.stdout + run.stderr
    found = re.findall(r"^NAMED (\w+/\w+) (\d+)$", output, re.M)
    check(mode, run.returncode == 0 and (USED in output) == (mode == "native")
          and [name for name, _ in found] == wanted and "SCRIPT ERROR" not in output, output)
    return {name: int(micros) / 1000 for name, micros in found}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--against", type=Path)
    parser.add_argument("--before-script", type=Path, help="pre-54 build, script rows only")
    parser.add_argument("--gate", action="store_true")
    parser.add_argument("--runs", type=int, default=7)
    args = parser.parse_args()
    if args.gate and not args.against:
        parser.error("--gate requires --against pointing to the main build")
    godot = os.getenv("KAS3_GODOT_DOTNET")
    check("Godot .NET", godot, "set KAS3_GODOT_DOTNET to the editor executable")
    check("runs", args.runs >= 2, "need at least two rounds to measure spread")
    with tempfile.TemporaryDirectory() as folder:
        commands = []
        for label, build in (("PR", args.build), ("main", args.against), ("pre54", args.before_script)):
            if build:
                project, library = prepare(godot, build.resolve(), Path(folder) / label)
                rows = ROWS if label != "pre54" else [row for row in ROWS if row.startswith("script/")]
                for mode in ("interpreter", "native", "C#"):
                    commands.append((label, project, library, mode, rows))
        samples = {}
        for turn in range(args.runs):
            for label, project, library, mode, rows in (reversed(commands) if turn % 2 else commands):
                for row, time in measure(godot, project, library, mode, rows, label == "pre54").items():
                    samples.setdefault(label, {}).setdefault(row, {}).setdefault(mode, []).append(time)
        for label, rows in samples.items():
            for row, modes in rows.items():
                for mode, times in modes.items():
                    print(f"{label} {row} {mode}: {statistics.median(times):.3f} ms [{min(times):.3f}, {max(times):.3f}]")
        misses = failures(samples["PR"], samples.get("main"))
        print("\n".join(misses) or "All measured distributions meet the targets.")
        if args.gate and misses:
            sys.exit(1)


if __name__ == "__main__":
    main()
