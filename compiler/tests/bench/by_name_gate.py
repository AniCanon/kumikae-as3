"""Compare checked by-name operations in one headless Godot .NET project per build."""
import argparse
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from native_godot import Runner, USED, retained_on_macos

ROWS = [f"{kind}/{op}" for kind in ("node", "inherited", "gdscript", "script", "value", "alias", "aliasTyped", "proven", "typed", "mixed")
        for op in ("read", "write", "call") if (kind, op) != ("value", "write")]


ROWS.extend(("foreign/read", "foreign/call", "computed/read", "custom/read", "custom/call", "customString/read", "customVector/read", "customFields/read", "detached/call"))

STRING_ROWS = ("joinThree/read", "append/read", "appendLength/read", "joinLong/read")
ROWS.extend(STRING_ROWS)

TOLERANCE = 0.03


INFERRED = ("number", "boolean", "string", "as3Class", "vector", "color", "timer", "configure")
ROWS.extend(f"{kind}{suffix}/read" for kind in INFERRED for suffix in ("", "Typed"))


OPERATIONS = 20000


def short_pairs(pilots):
    pairs = []
    for row in pilots:
        kind, op = row.split("/")
        if kind != "proven" and kind not in INFERRED:
            continue
        other = ("typed" if kind == "proven" else kind + "Typed") + "/" + op
        if other in pilots and min(pilots[row] + pilots[other]) < 0.05:
            pairs.append((row, other))
    return pairs


def swap_placement(source, pairs):
    # Keep each workload's name and body together. Only its position in the compiled image changes.
    methods = {}
    for match in re.finditer(r"^\t\tpublic function (\w+)\b[^\n]*", source, re.M):
        end = match.end()
        if not match.group().rstrip().endswith("}"):
            end = source.index("\n\t\t}", end) + len("\n\t\t}")
        methods[match[1]] = match.start(), end
    replacements = []
    for pair in pairs:
        for left, right in (pair, pair[::-1]):
            start, end = methods[right]
            replacements.append((*methods[left], source[start:end]))
    for start, end, body in sorted(replacements, reverse=True):
        source = source[:start] + body + source[end:]
    return source


def batch_sizes(pilots):
    """One workload per row across builds and modes, shared by each inferred/typed pair."""
    counts = {}
    for row, times in pilots.items():
        initial = OPERATIONS // 100 if row.startswith("configure") else OPERATIONS
        fastest = min(time for time in times if time is not None)
        # Keep long batches. Aim for two milliseconds on short ones to allow clock variation.
        counts[row] = initial if fastest * initial >= OPERATIONS else math.ceil(2 * OPERATIONS / fastest)
    for row in counts:
        kind, op = row.split("/")
        control = "typed" if kind == "proven" else kind + "Typed"
        other = f"{control}/{op}"
        if other in counts:
            counts[row] = counts[other] = max(counts[row], counts[other])
    return counts


def layout_mean(original, swapped):
    return (original + swapped) / 2


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
            if baseline is not None and mode in modes and baseline[row][mode] is not None:
                compare(modes[mode], baseline[row][mode], f"{mode} slower than main")
            kind, op = row.split("/")
            if mode in modes and (kind == "proven" or kind in INFERRED):
                control = "typed" if kind == "proven" else kind + "Typed"
                compare(modes[mode], samples[f"{control}/{op}"][mode], f"{mode} slower than typed")
    return misses


def check(label, ok, output):
    if not ok:
        raise SystemExit(f"{label}: {output}")


def prepare(godot, build, where, pairs=()):
    tests = build.parent / "tests"
    runner = Runner(godot, tests / "godot_project", tests, build / "kas3c", check)
    source = HERE / "by_name"
    matrix = source / "Matrix.as"
    if pairs:
        where.mkdir(parents=True)
        def method(row):
            family, operation = row.split("/")
            return family + operation.capitalize()
        methods = [(method(left), method(right)) for left, right in pairs]
        swapped = where / matrix.name
        swapped.write_text(swap_placement(matrix.read_text(), methods))
        matrix = swapped
    project = Path(runner.project(where, source, [str(matrix), "Managed.cs", "matrix.gd", "foreign.gd", "custom.gd",
                                  str(HERE / "languages/project.godot"), str(HERE / "languages/CmpCs.csproj")]))
    cache = Path(os.environ.get("NUGET_PACKAGES", Path.home() / ".nuget/packages"))
    # Restore only from the developer's installed packages.
    (project / "NuGet.Config").write_text('<configuration><packageSources><clear/>'
        f'<add key="local" value="{cache}"/></packageSources></configuration>')
    subprocess.run(["dotnet", "build", "CmpCs.csproj", "-v:q", "-p:Optimize=true", "-p:NuGetAudit=false"],
                   cwd=project, check=True)
    library = runner.library("by-name matrix", project, ["Matrix.as"], where)
    return project, library


def measure(godot, project, library, mode, rows, label, sizes=None):
    env = dict(os.environ, KAS3_NATIVE_PLAY="0")
    env.pop("KAS3_PROGRAM_LIBRARY", None)
    if mode == "native":
        env["KAS3_PROGRAM_LIBRARY"] = library
    wanted = [row for row in rows if mode != "C#" or row.startswith(("timer/", "timerTyped/", "configure/", "configureTyped/"))
              or (row not in STRING_ROWS and row.split("/")[0].removesuffix("Typed") not in (*INFERRED, "value"))]
    cmd = [godot, "--headless", "--path", project, "-s", "matrix.gd", "--",
           "cs" if mode == "C#" else "self" if label == "pre54" else "main" if label == "main" else "as", str(OPERATIONS),
           "timed" if sizes else "pilot", *[f"{row}={sizes[row]}" if sizes else row for row in wanted]]
    run = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=300)
    output = run.stdout + run.stderr
    found = re.findall(r"^NAMED (\w+/\w+) ([\d.]+|unsupported)$", output, re.M)
    check(mode, run.returncode == 0 and (USED in output) == (mode == "native")
          and [name for name, _ in found] == wanted and "SCRIPT ERROR" not in output, output)
    for row, value in found:
        check(row, value != "unsupported" or (label == "main" and mode != "C#" and row.startswith(("foreign/", "computed/", "custom/", "customString/", "customVector/", "customFields/"))), output)
    return {name: None if micros == "unsupported" else float(micros) / 1000 for name, micros in found}


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
        retained = {}
        for label, build in (("PR", args.build), ("main", args.against), ("pre54", args.before_script)):
            if build:
                project, library = prepare(godot, build.resolve(), Path(folder) / label)
                if sys.platform == "darwin" and args.against and label != "pre54":
                    retained[label] = retained_on_macos(library)
                rows = ROWS if label != "pre54" else [row for row in ROWS if row.startswith("script/")]
                for mode in ("interpreter", "native", "C#"):
                    commands.append((label, project, library, mode, rows))
        if retained:
            print(f"Play images retained after eight closes: PR {retained['PR']}/8, main {retained['main']}/8", flush=True)
            check("Play image unloading", retained["PR"] == 0, retained)
        pilots = {}
        native_pilots = {}
        for label, project, library, mode, rows in commands:
            for row, time in measure(godot, project, library, mode, rows, label).items():
                pilots.setdefault(row, []).append(time)
                if mode == "native" and time is not None:
                    native_pilots.setdefault(row, []).append(time)
        sizes = batch_sizes(pilots)
        pairs = short_pairs(native_pilots)
        mirrored = {}
        if pairs:
            print("Balanced native code placement for " + ", ".join(a for a, _ in pairs), flush=True)
            for label, build in (("PR", args.build), ("main", args.against)):
                if build:
                    mirrored[label] = prepare(godot, build.resolve(), Path(folder) / (label + "-swapped"), pairs)
        swapped_rows = {row for pair in pairs for row in pair}
        samples = {}
        for turn in range(args.runs):
            for label, project, library, mode, rows in (reversed(commands) if turn % 2 else commands):
                order = rows[::-1] if turn % 2 else rows
                alternate = [row for row in order if row in swapped_rows]
                def second_layout():
                    return measure(godot, *mirrored[label], mode, alternate, label, sizes)
                cross = mode == "native" and label in mirrored
                second = second_layout() if cross and turn % 2 else {}
                times = measure(godot, project, library, mode, order, label, sizes)
                if cross and not turn % 2:
                    second = second_layout()
                for row, time in times.items():
                    if row in second:
                        time = layout_mean(time, second[row])
                    samples.setdefault(label, {}).setdefault(row, {}).setdefault(mode, []).append(time)
        for label, rows in samples.items():
            for row, modes in rows.items():
                for mode, times in modes.items():
                    if all(t is None for t in times):
                        print(f"{label} {row} {mode}: unsupported")
                        modes[mode] = None
                    else:
                        check(row, None not in times, "baseline support changed between runs")
                        print(f"{label} {row} {mode}: {statistics.median(times):.3f} ms [{min(times):.3f}, {max(times):.3f}]")
        misses = failures(samples["PR"], samples.get("main"))
        print("\n".join(misses) or "All measured distributions meet the targets.")
        if args.gate and misses:
            sys.exit(1)


if __name__ == "__main__":
    main()
