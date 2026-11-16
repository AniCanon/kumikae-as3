#!/usr/bin/env python3
"""The same three workloads in Godot C#, GDScript and AS3, timed in one release export.

    python3 run.py [<extension project>] [--against <other extension project>] [--runs N]

An extension project is a folder with `kas3.gdextension` and `bin/libkas3_godot.dylib` (by default
compiler/tests/godot_project, where build-godot writes the library). With --against, a second export is
made with the other library, and the two are run turn and turn about.

Needs Godot .NET 4.7 (the KAS3_GODOT_DOTNET environment variable: the editor's executable), its export
templates, and `dotnet` with Godot's packages already in the local NuGet cache: nothing is downloaded. On
macOS the export is signed ad hoc so that it runs. Without one of these it says what is missing and stops
without failing.

main.gd warms each workload 35 times at a hundredth of its size, then takes the best of 5. Each run of an
export prints one table; with --runs N, the best of the runs is kept per cell.

  calls    200,000 rounds of property reads and writes, a method and a singleton's method on the engine
  script   3,000,000 calls of the script's own methods, with arithmetic and fields
  vectors  1,000,000 rounds of Vector2 arithmetic and normalized()
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_EXTENSION = os.path.join(HERE, "..", "..", "godot_project")
LIBRARY = os.path.join("bin", "libkas3_godot.dylib")
WORKLOADS = (("spin", "calls"), ("work", "script"), ("vec", "vectors"))
LANGUAGES = (("cs", "C#"), ("gd", "GDScript"), ("as", "AS3"))
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


def export(godot, extension, where):
    """A release export of the project with this extension's library; the path of the program it made."""
    project = os.path.join(where, "project")
    os.makedirs(os.path.join(project, "bin"))
    for name in PROJECT_FILES:
        shutil.copy(os.path.join(HERE, name), project)
    shutil.copy(os.path.join(extension, "kas3.gdextension"), project)
    shutil.copy(os.path.join(extension, LIBRARY), os.path.join(project, "bin"))
    # The editor's first scan learns of the extension and the C# project; quitting in the same frame crashes
    # Godot (see the compiler's README), so it is given time.
    step("the editor's first scan", [godot, "--headless", "--editor", "--quit-after", "300", "--path", project], project)
    # The local package cache is the only source: a package missing from it is an error, not a download.
    step("dotnet build", ["dotnet", "build", "CmpCs.csproj", "--source", nuget_cache(), "-v", "q"], project)
    os.makedirs(os.path.join(project, "out"))  # Godot exports only into a folder that is there.
    step("the export",[godot, "--headless", "--export-release", "macOS", "out/cmp.app"], project)
    app = os.path.join(project, "out", "cmp.app")
    if sys.platform == "darwin":
        step("signing", ["codesign", "--force", "--deep", "-s", "-", app], project)
    programs = os.listdir(os.path.join(app, "Contents", "MacOS"))
    if len(programs) != 1:
        sys.exit("languages: the export holds %d programs, not one: %s" % (len(programs), programs))
    return os.path.join(app, "Contents", "MacOS", programs[0])


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


def run_once(program):
    """{(workload, language): microseconds} from one run of an export."""
    done = step("the run", [program, "--headless"], os.path.dirname(program), timeout=900)
    found = {}
    for workload, language, micros in re.findall(r"^CMP (\w+) (\w+) (\d+)$", done.stdout, re.M):
        found[(workload, language)] = int(micros)
    if len(found) != len(WORKLOADS) * len(LANGUAGES):
        sys.exit("languages: the run printed %d timings, not %d:\n%s" % (len(found), len(WORKLOADS) * len(LANGUAGES),
                                                                         (done.stdout + done.stderr)[-1500:]))
    return found


def table(title, best):
    print("%s (ms, best of 5 after 35 warm-ups)" % title)
    print("%-10s" % "" + "".join("%12s" % name for _, name in LANGUAGES) + "%12s" % "AS3 / C#")
    for workload, name in WORKLOADS:
        cells = [best[(workload, language)] / 1000.0 for language, _ in LANGUAGES]
        print("%-10s" % name + "".join("%12.1f" % cell for cell in cells) + "%11.1fx" % (cells[2] / cells[0]))


def main():
    arguments = sys.argv[1:]
    runs = int(arguments[arguments.index("--runs") + 1]) if "--runs" in arguments else 1
    other = os.path.abspath(arguments[arguments.index("--against") + 1]) if "--against" in arguments else None
    positional = [argument for index, argument in enumerate(arguments)
                  if not argument.startswith("--") and (index == 0 or arguments[index - 1] not in ("--runs", "--against"))]
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
    with tempfile.TemporaryDirectory() as where:
        builds = [(extension, export(godot, extension, os.path.join(where, "first")))]
        if other:
            builds.append((other, export(godot, other, os.path.join(where, "second"))))
        best = {which: {} for which, _ in builds}
        for _ in range(runs):
            for which, program in builds:
                for key, micros in run_once(program).items():
                    best[which][key] = min(micros, best[which].get(key, micros))
        for which, _ in builds:
            table(os.path.relpath(which), best[which])


if __name__ == "__main__":
    main()
