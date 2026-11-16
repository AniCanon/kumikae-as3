"""The programs of run/ as native code: the oracle of the C++ backend.

Usage: run_native.py <kas3c> <tests dir> <work dir> --cxx <compiler> [--include <dir>]... [--lib <archive>]...
                     [--flag <flag>]... [--jobs N] [--only <name>]... [--update]

For each program of run/ (the same ones run_programs.py holds the interpreter to):

  1. `kas3c cpp` writes it as C++ into <work dir>/<name>. A program that uses something the backend does
     not translate yet is refused there with `not yet supported: <operation>`: it is counted as skipped,
     under that operation. Nothing but the generator decides that a program is skipped.
  2. The C++ is built into a plain executable with the flags generated code is meant for, warnings as
     errors, against the runtime (--include, --lib).
  3. The executable runs. Standard output must equal X.out; standard error (after what the compiler said,
     its warnings) must equal X.err, or be empty without one; the exit status is 1 for a program that ends
     in an error nothing catches and 0 otherwise. Nothing the program made may be left alive (status 3).

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


def expected(path, missing=""):
    return open(path).read() if os.path.exists(path) else missing


def one(name):
    """A program's outcome: ("skipped", operation), ("passed", ""), or ("failed", why)."""
    base = os.path.join(folder, name)
    files = open(base + ".args").read().split() if os.path.exists(base + ".args") else [name + ".as"]
    out = os.path.join(work, name)
    shutil.rmtree(out, ignore_errors=True)
    written = subprocess.run([kas3c, "cpp", *files, "-o", out], capture_output=True, text=True, cwd=folder, timeout=120)
    lines = written.stderr.splitlines()
    if written.returncode != 0:
        if lines and lines[-1].startswith(NOT_YET):
            return "skipped", lines[-1][len(NOT_YET):]
        return "failed", "kas3c cpp: exit %d\n%s" % (written.returncode, written.stderr)
    executable = os.path.join(out, "program")
    sources = sorted(glob.glob(os.path.join(out, "*.cpp")))
    includes = [flag for folder_ in given["--include"] for flag in ("-I", folder_)]
    built = subprocess.run([given["--cxx"][0], *FLAGS, *given["--flag"], *includes, *sources, *given["--lib"], "-o", executable],
                           capture_output=True, text=True, timeout=600)
    if built.returncode != 0:
        return "failed", "the C++ did not build\n" + built.stderr[-4000:]
    try:
        ran = subprocess.run([executable], capture_output=True, text=True, cwd=folder, timeout=120, env=environment)
    except subprocess.TimeoutExpired:
        return "failed", "it ran for more than 120 seconds"
    problems = []
    if ran.stdout != expected(base + ".out"):
        problems.append("--- output expected\n%s--- output\n%s" % (expected(base + ".out"), ran.stdout))
    errors = written.stderr + ran.stderr
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
jobs = int(given["--jobs"][0]) if given["--jobs"] else (os.cpu_count() or 4)
with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
    outcomes = dict(zip(names, pool.map(one, names)))

failures = ["%s\n%s" % (name, why) for name, (what, why) in outcomes.items() if what == "failed"]
skipped = {name: why for name, (what, why) in outcomes.items() if what == "skipped"}
passed = sum(1 for what, _ in outcomes.values() if what == "passed")

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
