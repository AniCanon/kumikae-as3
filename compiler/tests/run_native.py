"""The programs of run/ as native code: the oracle of the C++ backend.

Usage: run_native.py <kas3c> <tests dir> <work dir> --cxx <compiler> [--include <dir>]... [--lib <archive>]...
                     [--flag <flag>]... [--jobs N] [--only <name>]... [--update]

For each program of run/ (the same ones run_programs.py holds the interpreter to):

  1. `kas3c cpp` writes it as C++ into <work dir>/<name>. A program that uses something the backend does
     not translate yet is refused there with `not yet supported: <operation>`: it is counted as skipped,
     under that operation. Nothing but the generator decides that a program is skipped.
  2. The C++ is built with the flags generated code is meant for, warnings as errors, into a program
     library: a shared library that shows one symbol, `kas3_program`, and is linked against nothing of
     ours, so that a function of the runtime it reached by name and not through the host's table would be
     an undefined symbol and fail the build. The executable is the runtime's `main` (--lib) with that
     library. The runtime's headers are precompiled once for the
     run: every generated file reads them, and reading them is most of the time a file takes to build.
  3. The executable runs. Standard output must equal X.out; standard error (after what the compiler said,
     its warnings) must equal X.err, or be empty without one; the exit status is 1 for a program that ends
     in an error nothing catches and 0 otherwise. Nothing the program made may be left alive (status 3).

The benchmark programs of bench/ are built the same way and run with KAS3_NATIVE_COUNTS set: the host then
says how many times generated code left an operation to the library (a thunk). bench/native_counts.txt holds
the number for each, which is small and does not grow with the loop: a change that loses a fast path makes
it millions, and fails here and not only in a timing.

deep_frames, whose calls each hold a great deal, is run once more with a stack too small for it
(KAS3_STACK_KB): running out of stack must be Error #1023 too, never a crash.

native_skipped.txt lists the programs skipped and why. A program that is skipped and not on the list, or on
it and not skipped, fails: support is never lost unseen, and the list never goes stale. --update rewrites it;
read the result before committing it.

Prints the programs skipped, by operation, then `native: P passed, S skipped, F failed`.
"""
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys

# What generated code is built with (the design's flags), and as strictly as our own.
FLAGS = ["-std=c++17", "-O2", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-fno-unroll-loops", "-Wall", "-Wextra",
         "-Werror"]
# A program library: one symbol shown, and no symbol left to find when it is loaded (macOS's linker refuses
# one by itself; the others are told to).
LIBRARY_FLAGS = ["-shared", "-fPIC", "-fvisibility=hidden"] + ([] if sys.platform == "darwin" else ["-Wl,-z,defs"])
NOT_YET = "kas3c: not yet supported: "


def options(arguments):
    found = {"--cxx": [], "--include": [], "--lib": [], "--flag": [], "--jobs": [], "--only": []}
    update = False
    i = 0
    while i < len(arguments):
        if arguments[i] == "--update":
            update = True
            i += 1
        elif arguments[i] in found and i + 1 < len(arguments):
            found[arguments[i]].append(arguments[i + 1])
            i += 2
        else:
            sys.exit(__doc__)
    if len(found["--cxx"]) != 1:
        sys.exit(__doc__)
    return found, update


if len(sys.argv) < 4:
    sys.exit(__doc__)
kas3c = os.path.abspath(sys.argv[1])
tests = os.path.abspath(sys.argv[2])
work = os.path.abspath(sys.argv[3])
given, update = options(sys.argv[4:])
# As run_programs.py fixes them: Date's now and the zone, and leaks reported.
environment = dict(os.environ, KAS3_REPORT_LEAKS="1", KAS3_NOW="1763217551123", TZ="PST8PDT,M3.2.0,M11.1.0")
folder = os.path.join(tests, "run")


def precompiled():
    """The flags that give each build the runtime's headers, precompiled into the work folder."""
    folder_ = os.path.join(work, "precompiled")
    os.makedirs(folder_, exist_ok=True)
    header = os.path.join(folder_, "kas3_all.h")
    with open(header, "w") as handle:
        handle.write('#define KAS3_PROGRAM_LIBRARY 1\n#include "kas3.h"\n\n#include <cmath>\n#include <cstdint>\n#include <limits>\n')
    built = subprocess.run([given["--cxx"][0], *FLAGS, *given["--flag"], *includes, "-x", "c++-header", header, "-o", header + ".gch"],
                           capture_output=True, text=True, timeout=600)
    if built.returncode != 0:
        sys.exit("the runtime's headers did not build\n" + built.stderr[-4000:])
    return ["-include", header]


def expected(path, missing=""):
    return open(path).read() if os.path.exists(path) else missing


def build(files, sources_in, out):
    """The files as an executable in out: ("", what kas3c said), ("skipped", operation) or ("failed", why)."""
    shutil.rmtree(out, ignore_errors=True)
    written = subprocess.run([kas3c, "cpp", *files, "-o", out], capture_output=True, text=True, cwd=sources_in, timeout=120)
    lines = written.stderr.splitlines()
    if written.returncode != 0:
        if lines and lines[-1].startswith(NOT_YET):
            return "skipped", lines[-1][len(NOT_YET):]
        return "failed", "kas3c cpp: exit %d\n%s" % (written.returncode, written.stderr)
    executable = os.path.join(out, "program")
    sources = sorted(glob.glob(os.path.join(out, "*.cpp")))
    library = os.path.join(out, "libprogram.dylib" if sys.platform == "darwin" else "libprogram.so")
    built = subprocess.run([given["--cxx"][0], *FLAGS, *given["--flag"], *LIBRARY_FLAGS, *includes, *headers, *sources, "-o", library],
                           capture_output=True, text=True, timeout=600)
    if built.returncode != 0:
        return "failed", "the C++ did not build\n" + built.stderr[-4000:]
    linked = subprocess.run([given["--cxx"][0], *given["--flag"], library, *given["--lib"], "-Wl,-rpath," + out, "-o", executable],
                            capture_output=True, text=True, timeout=600)
    if linked.returncode != 0:
        return "failed", "the program did not link\n" + linked.stderr[-4000:]
    return "", written.stderr


def one(name):
    """A program's outcome: ("skipped", operation), ("passed", ""), or ("failed", why)."""
    base = os.path.join(folder, name)
    files = open(base + ".args").read().split() if os.path.exists(base + ".args") else [name + ".as"]
    out = os.path.join(work, name)
    what, said = build(files, folder, out)
    if what:
        return what, said
    try:
        ran = subprocess.run([os.path.join(out, "program")], capture_output=True, text=True, cwd=folder, timeout=120, env=environment)
    except subprocess.TimeoutExpired:
        return "failed", "it ran for more than 120 seconds"
    problems = []
    if ran.stdout != expected(base + ".out"):
        problems.append("--- output expected\n%s--- output\n%s" % (expected(base + ".out"), ran.stdout))
    errors = said + ran.stderr
    if errors != expected(base + ".err"):
        problems.append("--- errors expected\n%s--- errors\n%s" % (expected(base + ".err"), errors))
    fails = any(": warning: " not in line for line in expected(base + ".err").splitlines())
    if ran.returncode != (1 if fails else 0):
        problems.append("exit %d" % ran.returncode)
    return ("failed", "\n".join(problems)) if problems else ("passed", "")


names = sorted({os.path.splitext(os.path.basename(path))[0]
                for path in glob.glob(os.path.join(folder, "*.as")) + glob.glob(os.path.join(folder, "*.args"))})
if given["--only"]:
    names = [name for name in names if name in given["--only"]]
includes = [flag for folder_ in given["--include"] for flag in ("-I", folder_)]
headers = precompiled()
jobs = int(given["--jobs"][0]) if given["--jobs"] else (os.cpu_count() or 4)
with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
    outcomes = dict(zip(names, pool.map(one, names)))

failures = ["%s\n%s" % (name, why) for name, (what, why) in outcomes.items() if what == "failed"]
skipped = {name: why for name, (what, why) in outcomes.items() if what == "skipped"}
passed = sum(1 for what, _ in outcomes.values() if what == "passed")

if outcomes.get("deep_frames", ("", ""))[0] == "passed":
    small = subprocess.run([os.path.join(work, "deep_frames", "program")], capture_output=True, text=True, cwd=folder, timeout=120,
                           env=dict(environment, KAS3_STACK_KB="4096"))
    if small.returncode != 1 or "Error #1023" not in small.stderr or "9968 more" in small.stderr:
        failures.append("deep_frames with a small stack: exit %d\n%s" % (small.returncode, small.stderr[-600:]))


def counted(name):
    """What the host counted while a benchmark program ran, as native_counts.txt writes it; or why there is none."""
    out = os.path.join(work, "bench_" + name)
    what, said = build([name + ".as"], os.path.join(tests, "bench"), out)
    if what:
        return "%s: %s" % (what, said)
    ran = subprocess.run([os.path.join(out, "program")], capture_output=True, text=True, timeout=300, env=dict(environment, KAS3_NATIVE_COUNTS="1"))
    found = [line for line in ran.stderr.splitlines() if line.startswith("kas3: counts ")]
    if ran.returncode != 0 or len(found) != 1:
        return "exit %d, and no counts:\n%s" % (ran.returncode, ran.stderr[-600:])
    return found[0][len("kas3: counts "):] + "".join("\n" + line for line in ran.stderr.splitlines() if line.startswith("kas3:   "))


counts_path = os.path.join(tests, "bench", "native_counts.txt")
if not given["--only"]:
    wanted = dict(line.split(": ", 1) for line in expected(counts_path).splitlines() if not line.startswith("#"))
    for name in sorted(wanted):
        got = counted(name)
        if got.split("\n")[0] != wanted[name]:
            failures.append("bench/%s: native_counts.txt says %s, the run says %s" % (name, wanted[name], got))

listed_path = os.path.join(tests, "native_skipped.txt")
if not given["--only"]:
    text = "".join("%s: %s\n" % (name, skipped[name]) for name in sorted(skipped))
    if update:
        with open(listed_path, "w") as handle:
            handle.write(text)
    listed = dict(line.split(": ", 1) for line in expected(listed_path).splitlines())
    for name in sorted(set(skipped) | set(listed)):
        if skipped.get(name) != listed.get(name):
            failures.append("%s: native_skipped.txt says %s, the generator says %s" % (
                name, listed.get(name, "it is translated"), skipped.get(name, "it is translated")))

by_operation = {}
for name, operation in skipped.items():
    by_operation.setdefault(operation, []).append(name)
for operation in sorted(by_operation, key=lambda each: (-len(by_operation[each]), each)):
    print("skipped, %s: %s" % (operation, " ".join(sorted(by_operation[operation]))))
for failure in failures:
    print("FAIL " + failure)
print("native: %d passed, %d skipped, %d failed" % (passed, len(skipped), len(failures)))
sys.exit(1 if failures else 0)
