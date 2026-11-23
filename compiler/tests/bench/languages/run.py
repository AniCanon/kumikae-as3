#!/usr/bin/env python3
"""The same workloads in Godot C#, GDScript and AS3 (interpreted, and as native code), timed in one
release export.

    python3 run.py [<extension project>] [--against <other extension project>] [--runs N] [--no-frames]
                   [--no-workloads] [--only <bench>,...] [--columns cs,gd,as,native] [--keep <folder>]
                   [--frames N] [--warmup N] [--per-class]

An extension project is a folder with `kas3.gdextension` and `bin/libkas3_godot.dylib` (by default
compiler/tests/godot_project, where build-godot writes the library). With --against, a second export is
made with the other library, and the two are run turn and turn about.

"AS3 native" is the same script written as C++ by `kas3c cpp --godot` (the kas3c beside the extension's
build, or --kas3c <path>), built with the system's C++ compiler into a program library, and run by the same
export with KAS3_PROGRAM_LIBRARY naming it: each round runs the export once without it and once with it,
turn and turn about. Without kas3c or a compiler the column is left out, and said so. The library is built as a
release is, from the whole program in one file of C++ (`kas3c cpp --unit`); --per-class builds it from a file
for each class, as an editor does.

For a quick look while working: --only and --columns leave out frame benchmarks and languages, --no-workloads
the four workloads, --frames and --warmup shorten a measured run, and --keep <folder> makes the exports
there and uses them again the next time (an export is an editor scan and a .NET build), so only the program
library is rebuilt: the extension's own library is copied into a kept export afresh each time. A table that
is reported is a full one.

Needs Godot .NET 4.7 (the KAS3_GODOT_DOTNET environment variable: the editor's executable), its export
templates, and `dotnet` with Godot's packages already in the local NuGet cache: nothing is downloaded. On
macOS the export is signed ad hoc so that it runs. Without one of these it says what is missing and stops
without failing.

main.gd warms each workload 35 times at a hundredth of its size, then takes the best of 5. Each run of an
export prints one table; with --runs N, the best of the runs is kept per cell.

  calls    200,000 rounds of property reads and writes, a method and a singleton's method on the engine
  script   3,000,000 calls of the script's own methods, with arithmetic and fields
  vectors  1,000,000 rounds of Vector2 arithmetic and normalized()
  vectors3 1,000,000 rounds of Vector3 arithmetic, normalized() and dot()

Then the frame benchmarks (frames/, described in docs/performance.md), unless
--no-frames: each is a script on a node, left to Godot's frames at a fixed 60 a second, and what is timed is
the whole frame. frames/runner.gd warms each up for 300 frames and two seconds, then takes the median of 600
frames, in microseconds; with --runs N the median of the runs' medians is given, and for C# and native code
the least and the most. What each language's script ends with after ten frames is held to the others'.

  empty             a script whose _process does nothing
  move_nodes        20,000 nodes moved from one script
  per_node_process  a script with a _process on each of 20,000 nodes
  ray_queries       2,000 physics ray queries
  signal_traffic    20,000 signals emitted to a listener of the same script
  nbody             2,000 steps of five bodies
  sieve             the primes below 200,000
  array_conversion  1,000 points and 1,000 numbers handed to Godot twenty times
  saxpy             a*x + y over 100,000 Numbers ten times
  shapes            90,000 calls of each of two overridden methods
  alloc             20,000 objects made and let go
"""
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_EXTENSION = os.path.join(HERE, "..", "..", "godot_project")
LIBRARY = os.path.join("bin", "libkas3_godot.dylib")
WORKLOADS = (("spin", "calls"), ("work", "script"), ("vec", "vectors"), ("vec3", "vectors3"))
LANGUAGES = (("cs", "C#"), ("gd", "GDScript"), ("as", "AS3"))
NATIVE = ("native", "AS3 native")
FRAMES = ("empty", "move_nodes", "per_node_process", "ray_queries", "signal_traffic", "nbody", "sieve", "array_conversion", "saxpy", "shapes",
          "alloc")
COMPILER = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
# What generated code is built with (the design's flags), into a library that shows one symbol and needs
# none of ours: the host hands it the runtime's functions in a table.
NATIVE_FLAGS = ["-std=c++17", "-O2", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-fno-unroll-loops", "-fvisibility=hidden", "-shared"]
# The project's own files: anything else in the folder (an earlier editor's .godot, an export) is left behind.
PROJECT_FILES = ("CmpAs.as", "CmpCs.cs", "CmpCs.csproj", "CmpCs.sln", "CmpGd.gd", "export_presets.cfg", "main.gd",
                 "main.tscn", "project.godot")


def skip(reason):
    print("languages: skipped (%s)" % reason)
    sys.exit(0)


def step(what, command, cwd, timeout=900):
    done = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    if done.returncode != 0:
        sys.exit("languages: %s failed (exit %d):\n%s" % (what, done.returncode, (done.stdout + done.stderr)[-1500:]))
    return done


def exported(project):
    """The program an export of the project made."""
    app = os.path.join(project, "out", "cmp.app")
    programs = os.listdir(os.path.join(app, "Contents", "MacOS"))
    if len(programs) != 1:
        sys.exit("languages: the export holds %d programs, not one: %s" % (len(programs), programs))
    return os.path.join(app, "Contents", "MacOS", programs[0])


def refreshed(project, extension):
    """A kept export with the extension's library as it is now, signed again; the program."""
    app = os.path.join(project, "out", "cmp.app")
    name = os.path.basename(LIBRARY)
    found = [os.path.join(folder, name) for folder, _, files in os.walk(app) if name in files]
    if len(found) != 1:
        sys.exit("languages: the kept export holds %d copies of %s, not one" % (len(found), name))
    shutil.copy(os.path.join(extension, LIBRARY), found[0])
    if sys.platform == "darwin":
        step("signing", ["codesign", "--force", "--deep", "-s", "-", app], project)
    return exported(project)


def export(godot, extension, where, untyped=False):
    """A release export of the project with this extension's library; the path of the program it made."""
    project = os.path.join(where, "project")
    if os.path.isdir(os.path.join(project, "out", "cmp.app")):
        return refreshed(project, extension)
    os.makedirs(os.path.join(project, "bin"))
    for name in PROJECT_FILES + (("Untyped.as", "untyped.gd") if untyped else ()):
        shutil.copy(os.path.join(HERE, name), project)
    shutil.copytree(os.path.join(HERE, "frames"), os.path.join(project, "frames"), ignore=shutil.ignore_patterns("*.uid", "*.import"))
    shutil.copy(os.path.join(extension, "kas3.gdextension"), project)
    shutil.copy(os.path.join(extension, LIBRARY), os.path.join(project, "bin"))
    # The local package cache is the only source, for this build and for the `dotnet publish` Godot runs as
    # it exports: a package missing from it is an error, not a download.
    with open(os.path.join(project, "NuGet.Config"), "w") as config:
        config.write('<?xml version="1.0" encoding="utf-8"?>\n<configuration>\n  <packageSources>\n    <clear />\n'
                     '    <add key="cache" value="%s" />\n  </packageSources>\n</configuration>\n' % nuget_cache())
    # The editor's first scan learns of the extension and the C# project; quitting in the same frame crashes
    # Godot (see tests/README.md), so it is given time.
    step("the editor's first scan", [godot, "--headless", "--editor", "--quit-after", "300", "--path", project], project)
    step("dotnet build", ["dotnet", "build", "CmpCs.csproj", "-v", "q"], project)
    os.makedirs(os.path.join(project, "out"))  # Godot exports only into a folder that is there.
    step("the export",[godot, "--headless", "--export-release", "macOS", "out/cmp.app"], project)
    app = os.path.join(project, "out", "cmp.app")
    if sys.platform == "darwin":
        step("signing", ["codesign", "--force", "--deep", "-s", "-", app], project)
    return exported(project)


def scripts():
    """The project's AS3 scripts, in the order the extension reads them (by their place in the project)."""
    return sorted(["CmpAs.as"] + ["frames/" + name for name in os.listdir(os.path.join(HERE, "frames")) if name.endswith(".as")])


def program_library(kas3c, where, tree, unit=True):
    """The project's scripts as a program library built against a tree's runtime, or None with why not."""
    if not kas3c or not os.path.exists(kas3c):
        return None, "no kas3c at %s" % kas3c
    compiler = shutil.which("c++")
    if not compiler:
        return None, "no c++ on the PATH"
    out = os.path.join(where, "native")
    written = subprocess.run([kas3c, "cpp", "--godot", *(["--unit"] if unit else []), *scripts(), "-o", out], cwd=HERE, capture_output=True,
                             text=True)
    if written.returncode == 2 and unit:
        # An earlier build's kas3c, which does not know --unit: a file for each class, as it was built then.
        written = subprocess.run([kas3c, "cpp", "--godot", *scripts(), "-o", out], cwd=HERE, capture_output=True, text=True)
    if written.returncode != 0:
        return None, written.stderr.strip()
    includes = [flag for each in ("runtime", "vm", "ir", "src") for flag in ("-I", os.path.join(tree, each))]
    sources = sorted(os.path.join(out, name) for name in os.listdir(out) if name.endswith(".cpp"))
    library = os.path.join(where, "libkas3_program.dylib")
    step("building the program library", [compiler, *NATIVE_FLAGS, *includes, *sources, "-o", library], where)
    return library, None


def nuget_cache():
    return os.environ.get("NUGET_PACKAGES") or os.path.join(os.path.expanduser("~"), ".nuget", "packages")


def missing(godot):
    """What the export needs that is not here, in words, or None."""
    version = subprocess.run([godot, "--version"], capture_output=True, text=True, timeout=60).stdout.strip()
    # "4.7.2.stable.mono.official.ed1daf0bf": the templates' folder is "4.7.2.stable.mono".
    parts = version.split(".")
    if len(parts) < 5 or parts[4] != "mono":
        return "KAS3_GODOT_DOTNET is not Godot .NET (it says %r)" % version
    templates = os.path.join(os.path.expanduser("~"), "Library", "Application Support", "Godot", "export_templates",
                             ".".join(parts[:5]))
    if not os.path.isdir(templates):
        return "no export templates for %s in %s" % (".".join(parts[:5]), templates)
    sdk = re.search(r'Sdk="Godot\.NET\.Sdk/([^"]+)"', open(os.path.join(HERE, "CmpCs.csproj")).read()).group(1)
    for package in ("godot.net.sdk", "godotsharp", "godot.sourcegenerators"):
        if not os.path.isdir(os.path.join(nuget_cache(), package, sdk)):
            return "the NuGet cache has no %s %s, and nothing is downloaded" % (package, sdk)
    return None


def run_once(program, library=None):
    """{(workload, language): microseconds} from one run of an export; with a program library, AS3 is native."""
    environment = dict(os.environ, KAS3_NATIVE_PLAY="0")  # Native code is the library named, and nothing Play builds.
    environment.pop("KAS3_PROGRAM_LIBRARY", None)
    if library:
        environment["KAS3_PROGRAM_LIBRARY"] = library
    done = subprocess.run([program, "--headless"], cwd=os.path.dirname(program), capture_output=True, text=True, timeout=900, env=environment)
    if done.returncode != 0:
        sys.exit("languages: the run failed (exit %d):\n%s" % (done.returncode, (done.stdout + done.stderr)[-1500:]))
    if library and "ActionScript: the scripts run as native code." not in done.stdout:
        sys.exit("languages: the program library was not used:\n%s" % (done.stdout + done.stderr)[-1500:])
    found = {}
    for workload, language, micros in re.findall(r"^CMP (\w+) (\w+) (\d+)$", done.stdout, re.M):
        found[(workload, language)] = int(micros)
    if len(found) != len(WORKLOADS) * len(LANGUAGES):
        sys.exit("languages: the run printed %d timings, not %d:\n%s" % (len(found), len(WORKLOADS) * len(LANGUAGES),
                                                                         (done.stdout + done.stderr)[-1500:]))
    return found


def frame_once(program, bench, language, library=None, more=()):
    """What frames/runner.gd says of one benchmark in one language: its JSON line."""
    environment = dict(os.environ, KAS3_NATIVE_PLAY="0")  # Native code is the library named, and nothing Play builds.
    environment.pop("KAS3_PROGRAM_LIBRARY", None)
    if library:
        environment["KAS3_PROGRAM_LIBRARY"] = library
    command = [program, "--headless", "--fixed-fps", "60", "--", "--bench=" + bench, "--lang=" + language, "--mode=measure", *more]
    # A run that dies is run again, twice at most, and said: Godot .NET now and then crashes as it starts or
    # ends on a loaded machine, whatever the language. A benchmark that never gives its row is a failure.
    for attempt in range(3):
        done = subprocess.run(command, cwd=os.path.dirname(program), capture_output=True, text=True, timeout=1800, env=environment)
        rows = [line for line in done.stdout.splitlines() if line.startswith("{")]
        if done.returncode == 0 and len(rows) == 1:
            break
        print("languages: %s in %s died (exit %d), attempt %d" % (bench, language, done.returncode, attempt + 1), flush=True)
    else:
        sys.exit("languages: %s in %s failed (exit %d):\n%s" % (bench, language, done.returncode, (done.stdout + done.stderr)[-1500:]))
    return json.loads(rows[0])


def frames(builds, runs, chosen):
    """The frame benchmarks in every column of every build, turn and turn about: each benchmark is run in one
    build's columns, then the other's, before the next. The tables, and whether the checksums agree.
    builds: (title, program, library) each. chosen: the benchmarks and the columns wanted (None for all of
    either), and what a measured run is given."""
    wanted_benches, wanted_columns, more = chosen
    benches = tuple(each for each in FRAMES if not wanted_benches or each in wanted_benches)
    columns = {title: tuple(each for each in LANGUAGES + ((NATIVE,) if library else ()) if not wanted_columns or each[0] in wanted_columns)
               for title, _, library in builds}
    medians = {}
    sums = {}

    def once(build, bench, language, extra):
        _title, program, library = build
        native = language == NATIVE[0]
        return frame_once(program, bench, "as" if native else language, library if native else None, extra)

    # What each ends with after ten frames, whatever its speed: a measured run goes on for as long as its
    # warm-up takes, so its checksum is its own.
    for build in builds:
        for bench in benches:
            for language, _name in columns[build[0]]:
                sums[(build[0], bench, language)] = once(build, bench, language, ["--mode=verify"])["checksum"]
    for _ in range(runs):
        for bench in benches:
            for build in builds:
                for language, _name in columns[build[0]]:
                    medians.setdefault((build[0], bench, language), []).append(once(build, bench, language, more)["median_us"])
    agree = True
    for title, _program, library in builds:
        mine = columns[title]
        print("%s: frames (microseconds a frame, the median of 600; %d run%s; load %s)" % (title, runs, "" if runs == 1 else "s", load()))
        both = bool(library) and all(any(language == each for each, _ in mine) for language in ("cs", NATIVE[0]))
        print("%-18s" % "" + "".join("%12s" % name for _, name in mine) + ("%14s%16s%16s" % ("native / C#", "C# runs", "native runs") if both else ""))
        for bench in benches:
            cells = [statistics.median(medians[(title, bench, language)]) for language, _ in mine]
            line = "%-18s" % bench + "".join("%12.0f" % cell for cell in cells)
            if both:
                ours, theirs = medians[(title, bench, NATIVE[0])], medians[(title, bench, "cs")]
                line += "%13.2fx%16s%16s" % (statistics.median(ours) / max(statistics.median(theirs), 1), "%d-%d" % (min(theirs), max(theirs)),
                                             "%d-%d" % (min(ours), max(ours)))
            print(line)
            wanted = sums[(title, bench, mine[0][0])]
            for language, name in mine:
                if abs(sums[(title, bench, language)] - wanted) > 1e-6 * max(1.0, abs(wanted)):
                    agree = False
                    print("languages: %s ends with %r in %s and %r in %s" % (bench, sums[(title, bench, language)], name, wanted, mine[0][1]))
    return agree


def load():
    """The machine's load averages, in words: a timing is read beside them."""
    return "%.0f %.0f %.0f" % os.getloadavg()


def table(title, best):
    native = (WORKLOADS[0][0], NATIVE[0]) in best
    columns = LANGUAGES + ((NATIVE,) if native else ())
    print("%s (ms, best of 5 after 35 warm-ups; load %s)" % (title, load()))
    print("%-10s" % "" + "".join("%12s" % name for _, name in columns) + "%12s" % "AS3 / C#" + ("%14s" % "native / C#" if native else ""))
    for workload, name in WORKLOADS:
        cells = [best[(workload, language)] / 1000.0 for language, _ in columns]
        print("%-10s" % name + "".join("%12.1f" % cell for cell in cells) + "%11.1fx" % (cells[2] / cells[0]) +
              ("%13.2fx" % (cells[3] / cells[0]) if native else ""))


def main():
    arguments = sys.argv[1:]
    runs = int(arguments[arguments.index("--runs") + 1]) if "--runs" in arguments else 1
    other = os.path.abspath(arguments[arguments.index("--against") + 1]) if "--against" in arguments else None
    valued = ("--runs", "--against", "--kas3c", "--only", "--columns", "--keep", "--frames", "--warmup")
    value = lambda name: arguments[arguments.index(name) + 1] if name in arguments else None
    positional = [argument for index, argument in enumerate(arguments)
                  if not argument.startswith("--") and (index == 0 or arguments[index - 1] not in valued)]
    chosen = (value("--only").split(",") if value("--only") else None, value("--columns").split(",") if value("--columns") else None,
              ["--%s=%d" % (name, int(value("--" + name))) for name in ("frames", "warmup") if value("--" + name)])
    extension = os.path.abspath(positional[0] if positional else DEFAULT_EXTENSION)
    godot = os.environ.get("KAS3_GODOT_DOTNET")
    if sys.platform != "darwin":
        skip("only the macOS export is set up")
    if not godot or not os.path.exists(godot):
        skip("KAS3_GODOT_DOTNET does not name Godot .NET's executable")
    if not shutil.which("dotnet"):
        skip("no dotnet on the PATH")
    why = missing(godot)
    if why:
        skip(why)
    for which in [extension] + ([other] if other else []):
        if not os.path.exists(os.path.join(which, LIBRARY)):
            skip("no %s in %s: build the extension first" % (LIBRARY, which))
    with tempfile.TemporaryDirectory() as made:
        # A kept folder holds one export for each extension, by where the extension is.
        where = made
        if value("--keep"):
            where = os.path.abspath(value("--keep"))
            os.makedirs(where, exist_ok=True)
        builds = [(extension, export(godot, extension, os.path.join(where, "first-untyped" if "--untyped-cs" in arguments else "first"), "--untyped-cs" in arguments))]
        if other:
            builds.append((other, export(godot, other, os.path.join(where, "second-untyped" if "--untyped-cs" in arguments else "second"), "--untyped-cs" in arguments)))
        if "--untyped-cs" in arguments:
            samples = {}
            for _ in range(runs):
                for which, program in builds:
                    run = step("C# by-name access", [program, "--headless", "--", "untyped-cs", "cs"], where)
                    rows = re.findall(r"^NAMED (\w+) (\d+)$", run.stdout, re.M)
                    if [case for case, _ in rows] != ["script", "node"]:
                        sys.exit(run.stdout + run.stderr)
                    for case, micros in rows:
                        samples.setdefault((which, case), []).append(int(micros) / 1000)
            for (which, case), times in samples.items():
                print(f"{which} C# {case} {statistics.median(times):.3f} ms  spread {max(times)-min(times):.3f} ms ({runs} runs)")
            return
        best = {which: {} for which, _ in builds}
        # The scripts as native code, by the kas3c built with each extension (its build folder is beside
        # compiler/tests, as build-godot is), unless one is named.
        libraries = {}
        for which, _ in builds:
            kas3c = (os.path.abspath(arguments[arguments.index("--kas3c") + 1]) if "--kas3c" in arguments
                     else os.path.join(which, "..", "..", "build-godot", "kas3c"))
            # Built against the runtime of the tree the extension came from: its headers are what it was built with.
            tree = os.path.join(which, "..", "..")
            libraries[which], why = program_library(kas3c, os.path.join(made, "native%d" % len(libraries)),
                                                    tree if os.path.exists(os.path.join(tree, "runtime", "kas3.h")) else COMPILER,
                                                    "--per-class" not in arguments)
            if why:
                print("languages: no AS3 native column for %s (%s)" % (which, why))
        for _ in range(0 if "--no-workloads" in arguments else runs):
            for which, program in builds:
                for key, micros in run_once(program).items():
                    best[which][key] = min(micros, best[which].get(key, micros))
                if libraries[which]:
                    for (workload, language), micros in run_once(program, libraries[which]).items():
                        if language == "as":
                            key = (workload, NATIVE[0])
                            best[which][key] = min(micros, best[which].get(key, micros))
        for which, _program in builds:
            if "--no-workloads" not in arguments:
                table(which, best[which])
        agree = "--no-frames" in arguments or frames([(which, program, libraries[which]) for which, program in builds], runs, chosen)
        if not agree:
            sys.exit("languages: the languages do not end with the same checksums")


if __name__ == "__main__":
    main()
