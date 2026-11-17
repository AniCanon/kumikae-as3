"""Programs through `kas3c run`: the start of the conformance suite.

Usage: run_programs.py <path to kas3c> <tests dir> [--update]

  run/X.as            runs; standard output must equal X.out. With X.err, standard error must equal it and
                      the exit status must be 1 (0 if X.err holds only warnings); without, standard error
                      must be empty and the status 0.
  run/X.args          the files of a program of several files, the entry file first, instead of X.as.
  run/X.ir            if present, `kas3c ir` of the program must print it.
  run/godot/X.as      a script for Godot: `kas3c ir --godot` of it must print X.ir (it does not run here).
  run/refused/X.as    uses something the interpreter does not run yet: status 1, nothing on standard
                      output, and X.err on standard error.
  corpus/, golden/    every other test program: it runs or is refused, and never crashes.

Expected output for numbers, strings and evaluation order was produced by Node from a line-by-line
JavaScript copy of the program, since AS3 and JavaScript share those rules. Stack traces and anything about
classes follow the AS3 language and were read by hand.

Nothing a program makes may be left alive when it ends: kas3c reports that with status 3. Date's now and the
zone are fixed (see `environment` below). Each program in run/ is run once more with what its objects are
constructed with remembered (KAS3_REMEMBER_CONSTRUCTIONS, as a game being edited has it), and must do the same.

--update rewrites the .out, .err and .ir files. Read the result before committing it.
"""
import glob
import os
import subprocess
import sys

kas3c = os.path.abspath(sys.argv[1])
tests = os.path.abspath(sys.argv[2])
update = "--update" in sys.argv[3:]
failures = []
passed = 0
# Date's now is fixed (2025-11-15 14:39:11.123 UTC), and its zone is the US Pacific one as a POSIX rule,
# which every C library reads without a zone database: no output depends on this machine's clock or zone.
# No program here is held to Flash's fifteen seconds (KAS3_TIME_LIMIT_MS=0: test_cli.py tests the limit): on a
# machine too busy to run one in that time, it would end with Error #1502 and a wrong failure.
environment = dict(os.environ, KAS3_REPORT_LEAKS="1", KAS3_NOW="1763217551123", TZ="PST8PDT,M3.2.0,M11.1.0", KAS3_TIME_LIMIT_MS="0")


def run(command, files, cwd, extra=None):
    """One run of kas3c. A run that does not end is a failure like any other, said as one (status -1)."""
    arguments = [kas3c, command, *files]
    try:
        return subprocess.run(arguments, capture_output=True, text=True, cwd=cwd, timeout=600, env=dict(environment, **(extra or {})))
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(arguments, -1, "", "did not end within 600 seconds")


def compare(name, actual, path, missing=None):
    global passed
    if update and (os.path.exists(path) or missing is None):
        with open(path, "w") as handle:
            handle.write(actual)
    expected = open(path).read() if os.path.exists(path) else missing
    if expected == actual:
        passed += 1
    else:
        failures.append("%s\n--- expected\n%s--- actual\n%s" % (name, expected, actual))


def check(name, condition, detail=""):
    global passed
    if condition:
        passed += 1
    else:
        failures.append("%s %s" % (name, detail))


folder = os.path.join(tests, "run")
names = sorted({os.path.splitext(os.path.basename(path))[0]
                for path in glob.glob(os.path.join(folder, "*.as")) + glob.glob(os.path.join(folder, "*.args"))})
for name in names:
    base = os.path.join(folder, name)
    files = open(base + ".args").read().split() if os.path.exists(base + ".args") else [name + ".as"]
    result = run("run", files, folder)
    # Standard error with only warnings on it is a program that ran.
    fails = os.path.exists(base + ".err") and any(": warning: " not in line for line in open(base + ".err").read().splitlines())
    compare(name + " output", result.stdout, base + ".out")
    compare(name + " errors", result.stderr, base + ".err", "")
    check(name + " status", result.returncode == (1 if fails else 0), "exit %d" % result.returncode)
    if os.path.exists(base + ".ir"):
        compare(name + " ir", run("ir", files, folder).stdout, base + ".ir")
    # With what objects are constructed with remembered, as a game being edited has it (vm/constructions.h),
    # a program does the same and leaves nothing alive.
    remembering = run("run", files, folder, {"KAS3_REMEMBER_CONSTRUCTIONS": "1"})
    check(name + " with constructions remembered", (remembering.stdout, remembering.stderr, remembering.returncode) ==
          (result.stdout, result.stderr, result.returncode), "exit %d\n%s" % (remembering.returncode, remembering.stderr[-600:]))

# With a stack too small for what a program does, it ends with Error #1023 and never with a crash: a library
# callback that calls the library again (callbacks_nest: caught, where the stack holds its first part),
# and words that nest in C++ alone (text_cycles). How much fits depends on the build, so only that is held.
for name, kilobytes in (("callbacks_nest", "4096"), ("text_cycles", "2048"), ("deep_frames", "1024")):
    small = run("run", [name + ".as"], folder, {"KAS3_STACK_KB": kilobytes})
    overflowed = "Error #1023" in small.stderr or "1023" in small.stdout
    check(name + " with a small stack", small.returncode in (0, 1) and overflowed and "Sanitizer" not in small.stderr and
          (small.returncode == 1) == small.stderr.startswith("Error: Error #1023"), "exit %d\n%s%s" % (small.returncode, small.stdout[-300:], small.stderr[-600:]))

for path in sorted(glob.glob(os.path.join(folder, "godot", "*.as"))):
    name = "godot/" + os.path.basename(path)
    compare(name + " ir", run("ir", ["--godot", os.path.basename(path)], os.path.dirname(path)).stdout, path[:-3] + ".ir")

# Every register that holds one of Godot's values says which type, in each script of the Godot test project.
project = os.path.join(tests, "godot_project")
lowered = run("ir", ["--godot"] + sorted(os.path.basename(path) for path in glob.glob(os.path.join(project, "*.as"))), project)
untyped = [line for line in lowered.stdout.splitlines() if line.startswith("  godot ") and ":?" in line]
check("godot_project ir", lowered.returncode == 0 and not untyped, "\n".join(untyped[:5]) or lowered.stderr[-600:])

refused = os.path.join(folder, "refused")
for path in sorted(glob.glob(os.path.join(refused, "*.as"))):
    name = "refused/" + os.path.basename(path)
    result = run("run", [os.path.basename(path)], refused)
    compare(name, result.stderr, path[:-3] + ".err")
    named = ("is not supported by the interpreter yet" in result.stderr or "the program's first class" in result.stderr or
             "has no class to start" in result.stderr)
    check(name + " status", result.returncode == 1 and result.stdout == "" and named, "exit %d" % result.returncode)

# Everything else in the test tree: run it or refuse it, but never crash, hang or leak.
others = sorted(glob.glob(os.path.join(tests, "corpus", "*.as")) + glob.glob(os.path.join(tests, "golden", "**", "*.as"),
                                                                              recursive=True))
for path in others:
    result = run("run", [path], tests)
    check("no crash: " + os.path.relpath(path, tests), result.returncode in (0, 1),
          "exit %d %s" % (result.returncode, result.stderr[-200:]))
check("found the other programs", len(others) >= 30, str(len(others)))

for failure in failures:
    print("FAIL " + failure)
print("programs: %d passed, %d failed" % (passed, len(failures)))
sys.exit(1 if failures else 0)
