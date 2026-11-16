#!/usr/bin/env python3
"""Times the benchmark programs as native code against the interpreter.

    python3 run_native_bench.py <build dir> [--runs N] [--against <other build dir>]

Each program (loop.as, vectors.as, values.as and objects.as) is written as C++ by <build dir>/kas3c, built with the flags
generated code is meant for against <build dir>'s libraries, and run turn and turn about with `kas3c run`
of the same program: the median of the runs in milliseconds and the spread between the fastest and the
slowest, then the two set against each other. The times are whole processes, start to end,
so each includes a few milliseconds of starting up (and, for the interpreter, of compiling the program).

With --against, the native programs of two builds are timed turn and turn about instead: how a change to
the backend or the runtime moved them. A change is faster only if its median moved by more than the spread.
"""
import glob
import os
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
COMPILER = os.path.dirname(os.path.dirname(HERE))
PROGRAMS = ("loop.as", "vectors.as", "values.as", "objects.as")
FLAGS = ["-std=c++17", "-O2", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-fno-unroll-loops"]


def timed(command):
    start = time.perf_counter()
    done = subprocess.run(command, capture_output=True, text=True, cwd=HERE, timeout=600)
    return (time.perf_counter() - start) * 1000.0, done


def native(build, program, folder):
    """The program as an executable built from what <build>/kas3c writes."""
    out = os.path.join(folder, "%d_%s" % (len(os.listdir(folder)), program[:-3]))  # Two builds may have one name.
    written = subprocess.run([os.path.join(build, "kas3c"), "cpp", program, "-o", out], capture_output=True, text=True, cwd=HERE)
    if written.returncode != 0:
        sys.exit("%s: %s" % (program, written.stderr.strip()))
    # A build folder is beside the sources it was built from (compiler/build): its own runtime's headers.
    tree = os.path.dirname(build) if os.path.exists(os.path.join(os.path.dirname(build), "runtime", "kas3.h")) else COMPILER
    includes = [flag for each in ("runtime", "vm", "ir", "src") for flag in ("-I", os.path.join(tree, each))]
    libraries = [os.path.join(build, "lib%s.a" % each) for each in ("kas3standalone", "kas3runtime", "kas3vm", "kas3ir", "kas3front")]
    executable = os.path.join(out, "program")
    built = subprocess.run(["c++", *FLAGS, *includes, *sorted(glob.glob(os.path.join(out, "*.cpp"))), *libraries, "-o", executable],
                           capture_output=True, text=True)
    if built.returncode != 0:
        sys.exit("%s did not build:\n%s" % (program, built.stderr[-2000:]))
    return executable


def compare(name, first, second, labels, runs):
    samples = ([], [])
    outputs = set()
    for turn in range(runs + 1):
        for which, command in enumerate((first, second)):
            elapsed, done = timed(command)
            if done.returncode != 0:
                sys.exit("%s failed: %s" % (" ".join(command), done.stderr.strip()))
            if turn > 0:  # The first turn only warms the files up.
                samples[which].append(elapsed)
            outputs.add(done.stdout)
    if len(outputs) != 1:
        sys.exit("%s: the two do not print the same" % name)
    medians = [statistics.median(each) for each in samples]
    for label, each, median in zip(labels, samples, medians):
        print("%-10s %-12s %9.1f ms   spread %6.1f ms   (%d runs)" % (name, label, median, max(each) - min(each), runs))
    print("%-10s %s takes %.2f of the time of %s (%.1f times as fast)" % (name, labels[1], medians[1] / medians[0], labels[0],
                                                                        medians[0] / medians[1]))


def main():
    arguments = sys.argv[1:]
    if not arguments:
        sys.exit(__doc__)
    build = os.path.abspath(arguments[0])
    runs = int(arguments[arguments.index("--runs") + 1]) if "--runs" in arguments else 9
    other = os.path.abspath(arguments[arguments.index("--against") + 1]) if "--against" in arguments else None
    with tempfile.TemporaryDirectory() as folder:
        for program in PROGRAMS:
            name = program[:-3]
            if other:
                compare(name, [native(other, program, folder)], [native(build, program, folder)], ("other", "this"), runs)
            else:
                compare(name, [os.path.join(build, "kas3c"), "run", program], [native(build, program, folder)], ("interpreter", "native"), runs)


main()
