#!/usr/bin/env python3
"""Times the compiler and the interpreter, to compare before and after a change.

    python3 run_bench.py <build dir> [--godot <tests dir>] [--runs N]

Prints one line per benchmark: the median of the runs in milliseconds, and the spread between the
fastest and the slowest. A change is slower only if its median moved by more than the spread.

  check     a full `kas3c check` of a generated project of about 100,000 lines
  recheck   the language server checking that project again after one edit (bench_recheck's median)
  run       `kas3c run` of loop.as: arithmetic, calls and fields in a loop
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


def timed(command, cwd=None):
    start = time.perf_counter()
    done = subprocess.run(command, capture_output=True, text=True, cwd=cwd, timeout=600)
    return (time.perf_counter() - start) * 1000.0, done


def report(name, samples):
    print("%-8s %9.1f ms   spread %6.1f ms   (%d runs)" % (name, statistics.median(samples), max(samples) - min(samples),
                                                          len(samples)))


def main():
    arguments = sys.argv[1:]
    if not arguments:
        sys.exit(__doc__)
    build = os.path.abspath(arguments[0])
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
            found = re.search(r"re-check:\s+([0-9.]+) ms median", done.stdout)
            if not found:
                sys.exit("bench_recheck printed no median:\n" + done.stdout[-600:])
            samples.append(float(found.group(1)))
        report("recheck", samples)
    samples = []
    for _ in range(runs):
        elapsed, done = timed([kas3c, "run", "loop.as"], cwd=HERE)
        if done.returncode != 0:
            sys.exit("run failed:\n" + done.stderr[-600:])
        samples.append(elapsed)
    report("run", samples)
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
