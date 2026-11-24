"""Lowering one huge function must stay close to linear in time and memory.

Usage: test_lowering_bounds.py <path to kas3c>

Each case is a shape that made, or could make, the release of temporaries quadratic: a liveness set kept
for every instruction, a full row of bits for every block, or a walk of every handler's range for every
handler. The limits are a few times what an optimized build needs and far below what the quadratic forms
took (seconds and gigabytes). The time is the processor time of `kas3c pack`, so a busy machine does not
fail them. A build with sanitizers is not held to them.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "bench"))
from generate_function import function_text

MEGABYTE = 1024 * 1024
# Shape, statements, depth, seconds, megabytes. The parser allows about 200 levels of nesting.
CASES = (
    ("arguments", 8000, 1, 1.0, 64),
    ("chosen_arguments", 4000, 1, 1.0, 128),
    ("loops", 16000, 120, 1.0, 128),
    ("conditionals", 16000, 8, 1.0, 128),
    # Each handler releases what its whole range holds, so the module itself has 2.4 million releases and
    # most of the memory is the module. The limit here guards the time more than the memory.
    ("nested_try", 2000, 200, 1.0, 256),
)


def peak_bytes(usage):
    return usage.ru_maxrss if sys.platform == "darwin" else usage.ru_maxrss * 1024


def main():
    kas3c = os.path.abspath(sys.argv[1])
    failures = []
    with tempfile.TemporaryDirectory() as folder:
        source = os.path.join(folder, "F.as")
        for shape, count, depth, seconds, megabytes in CASES:
            with open(source, "w", encoding="utf-8") as output:
                output.write(function_text(shape, count, depth))
            process = subprocess.Popen([kas3c, "pack", source, "-o", os.path.join(folder, "F.kas3")])
            _, status, usage = os.wait4(process.pid, 0)
            process.returncode = status
            elapsed = usage.ru_utime + usage.ru_stime
            peak = peak_bytes(usage) / MEGABYTE
            print("%-12s %6d statements, depth %3d: %6.3f s, %6.1f MB" % (shape, count, depth, elapsed, peak))
            if status != 0:
                failures.append("%s: kas3c pack failed" % shape)
            if elapsed > seconds:
                failures.append("%s: %.3f s, over %.1f s" % (shape, elapsed, seconds))
            if peak > megabytes:
                failures.append("%s: %.1f MB, over %d MB" % (shape, peak, megabytes))
    for failure in failures:
        print("FAIL " + failure)
    print("%d cases, %d failed" % (len(CASES), len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
