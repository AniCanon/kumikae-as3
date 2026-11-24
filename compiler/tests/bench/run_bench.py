#!/usr/bin/env python3
"""Times the compiler and the interpreter, to compare before and after a change.

    python3 run_bench.py <build dir> [--godot <tests dir>] [--runs N]
    python3 run_bench.py <build dir> --against <other build dir> [--runs N] [--gate]

With --against, `check`, `pack` and `run` are timed for both builds turn and turn about, which is the
only way to see a difference of a few per cent: runs made one build after the other drift by more than
that. With --gate as well, the other build is main's, and the exit status is 1 if `pack` takes longer
than main's by more than PACK_TOLERANCE.

Prints one line per benchmark: the median of the runs in milliseconds, and the spread between the
fastest and the slowest. A change is slower only if its median moved by more than the spread.

  check     a full `kas3c check` of a generated project of about 100,000 lines
  pack      `kas3c pack --godot` of that project: the check, the lowering to IR and the module written
  recheck   the language server checking that project again after a method's parameters change (bench_recheck's
            median for the whole program; tests/bench/recheck.py times every kind of edit, for two builds)
  run       `kas3c run` of loop.as: arithmetic, calls and fields in a loop
  vectors   `kas3c run` of vectors.as: a vector's elements read and written in loops
  values    `kas3c run` of values.as: calls through function values, objects and arrays made in a loop
  objects   `kas3c run` of objects.as: objects of a class made, used and let go in a loop

KAS3_REMEMBER_CONSTRUCTIONS=1 in the environment times `kas3c run` as a game being edited runs: with what each
object is constructed with remembered, for a reload that updates objects in place (a build without that
ignores it, so --against an older build shows what remembering costs).
  engine    in headless Godot, 160,000 calls from AS3 into the engine (--godot only; needs the extension)
"""
import glob
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# How much longer than main `pack` may take. Main keeps every temporary until its function ends. A build
# that releases each at its last use works out where that is and writes about a sixth more instructions,
# which took a tenth longer when this was set. A lowering pass that grows faster than the code does costs
# several times that.
PACK_TOLERANCE = 0.15


def over_tolerance(name, candidate, reference):
    """Why a gated row fails, or None."""
    if name == "pack" and candidate > reference * (1 + PACK_TOLERANCE):
        return "pack: %.1f ms against main's %.1f ms, over %.0f%%" % (candidate, reference, PACK_TOLERANCE * 100)
    return None


def timed(command, cwd=None):
    start = time.perf_counter()
    # What is timed in Godot is the interpreter: Play's own build of the project's library is off.
    done = subprocess.run(command, capture_output=True, text=True, cwd=cwd, timeout=600, env=dict(os.environ, KAS3_NATIVE_PLAY="0"))
    return (time.perf_counter() - start) * 1000.0, done


def report(name, samples):
    print("%-28s %9.1f ms   spread %6.1f ms   (%d runs)" % (name, statistics.median(samples), max(samples) - min(samples),
                                                          len(samples)))


# The programs `kas3c run` times, by the name each is reported under.
PROGRAMS = (("run", "loop.as"), ("vectors", "vectors.as"), ("values", "values.as"), ("objects", "objects.as"),
            ("append", "string_append.as"), ("join", "string_join.as"))


def against(build, other, runs, gate=False):
    """`check`, `pack` and `run` for two builds, one run of each in turn."""
    misses = []
    with tempfile.TemporaryDirectory() as project:
        subprocess.run([sys.executable, os.path.join(HERE, "generate_project.py"), project, "100000"], check=True,
                       capture_output=True)
        files = sorted(glob.glob(os.path.join(project, "**", "*.as"), recursive=True))
        packed = os.path.join(project, "packed.kas3")
        timings = ([("check", ["check", *files], None), ("pack", ["pack", "--godot", *files, "-o", packed], None)] +
                   [(name, ["run", program], HERE) for name, program in PROGRAMS])
        for name, command, cwd in timings:
            samples = {build: [], other: []}
            for turn in range(runs):
                for which in ((build, other) if turn % 2 == 0 else (other, build)):
                    elapsed, done = timed([os.path.join(which, "kas3c"), *command], cwd=cwd)
                    if done.returncode != 0:
                        sys.exit("%s failed:\n%s" % (name, done.stderr[-600:]))
                    samples[which].append(elapsed)
            for which in (build, other):
                report("%s %s" % (name, os.path.relpath(which)), samples[which])
            first, second = statistics.median(samples[build]), statistics.median(samples[other])
            print("%-8s the first is %+.1f%% against the second" % (name, (first - second) / second * 100.0))
            miss = over_tolerance(name, first, second) if gate else None
            if miss:
                misses.append(miss)
    for miss in misses:
        print("FAIL " + miss)
    return not misses


def main():
    arguments = sys.argv[1:]
    if "--untyped" in arguments:
        from untyped import benchmark
        benchmark(arguments, native=False)
        return
    if not arguments:
        sys.exit(__doc__)
    build = os.path.abspath(arguments[0])
    if "--against" in arguments:
        runs = int(arguments[arguments.index("--runs") + 1]) if "--runs" in arguments else 21
        if not against(build, os.path.abspath(arguments[arguments.index("--against") + 1]), runs, "--gate" in arguments):
            sys.exit(1)
        return
    runs = int(arguments[arguments.index("--runs") + 1]) if "--runs" in arguments else 7
    godot_tests = os.path.abspath(arguments[arguments.index("--godot") + 1]) if "--godot" in arguments else None
    kas3c = os.path.join(build, "kas3c")
    with tempfile.TemporaryDirectory() as project:
        subprocess.run([sys.executable, os.path.join(HERE, "generate_project.py"), project, "100000"], check=True,
                       capture_output=True)
        files = sorted(glob.glob(os.path.join(project, "**", "*.as"), recursive=True))
        samples = []
        for _ in range(runs):
            elapsed, done = timed([kas3c, "check", *files])
            if done.returncode != 0:
                sys.exit("check failed:\n" + done.stderr[-600:])
            samples.append(elapsed)
        report("check", samples)
        samples = []
        for _ in range(runs):
            _, done = timed([os.path.join(build, "bench_recheck"), project])
            found = re.search(r"^signature\s+program\s+([0-9.]+) ms median", done.stdout, re.M)
            if not found:
                sys.exit("bench_recheck printed no median:\n" + done.stdout[-600:])
            samples.append(float(found.group(1)))
        report("recheck", samples)
    for name, program in PROGRAMS:
        samples = []
        for _ in range(runs):
            elapsed, done = timed([kas3c, "run", program], cwd=HERE)
            if done.returncode != 0:
                sys.exit("%s failed:\n%s" % (name, done.stderr[-600:]))
            samples.append(elapsed)
        report(name, samples)
    if godot_tests:
        godot = os.environ.get("KAS3_GODOT") or shutil.which("godot")
        samples = []
        for _ in range(runs):
            _, done = timed([godot, "--headless", "--path", os.path.join(godot_tests, "godot_project"), "-s", "bench.gd"])
            found = re.search(r"BENCH engine_calls (\d+)", done.stdout)
            if not found:
                sys.exit("the engine benchmark did not run:\n" + (done.stdout + done.stderr)[-800:])
            samples.append(int(found.group(1)) / 1000.0)
        report("engine", samples)


if __name__ == "__main__":
    main()
