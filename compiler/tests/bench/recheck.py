#!/usr/bin/env python3
"""How long the language server takes to check a project again after an edit, for two builds turn and turn about.

    python3 recheck.py <build dir> --against <other build dir> [--lines 100000 1000000] [--runs 5] [--rounds 10]

For each size a project is generated with tests/bench/generate_project.py --hub, and each build's
bench_recheck runs on it in turn, the order swapped every run. bench_recheck must be built from this source
in both builds (it uses only what the Workspace has always offered). Prints, per edit and per build, the
median over the runs of each run's median, and the range of those medians, with the load average when the
size started: a timing taken on a busy machine says so.
"""
import argparse
import os
import re
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LINE = re.compile(r"^(\S+)\s+(file|program)\s+([0-9.]+) ms median\s+([0-9.]+) ms p95\s+([0-9.]+) ms max", re.M)


def run(build, project, rounds):
    done = subprocess.run([os.path.join(build, "bench_recheck"), project, "--rounds", str(rounds)], capture_output=True,
                          text=True, timeout=3600)
    if done.returncode not in (0, 1) or "first check" not in done.stdout:
        sys.exit("bench_recheck failed in %s:\n%s%s" % (build, done.stdout[-800:], done.stderr[-800:]))
    first = float(re.search(r"first check:\s+([0-9.]+) ms", done.stdout).group(1))
    memory = int(re.search(r"peak resident memory: (\d+) KiB", done.stdout).group(1))
    timings = {(name, until): float(median) for name, until, median, _, _ in LINE.findall(done.stdout)}
    timings[("first", "check")] = first
    timings[("memory", "KiB")] = memory
    return timings


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build")
    parser.add_argument("--against", required=True)
    parser.add_argument("--lines", type=int, nargs="+", default=[100000])
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--rounds", type=int, default=10)
    arguments = parser.parse_args()
    builds = {"after": os.path.abspath(arguments.build), "before": os.path.abspath(arguments.against)}
    for lines in arguments.lines:
        with tempfile.TemporaryDirectory() as project:
            subprocess.run([sys.executable, os.path.join(HERE, "generate_project.py"), project, str(lines), "--hub"],
                           check=True, capture_output=True)
            print("%d lines; load average %s" % (lines, " ".join("%.1f" % value for value in os.getloadavg())), flush=True)
            samples = {name: {} for name in builds}
            for index in range(arguments.runs):
                order = list(builds.items()) if index % 2 == 0 else list(reversed(builds.items()))
                for name, build in order:
                    for key, value in run(build, project, arguments.rounds).items():
                        samples[name].setdefault(key, []).append(value)
            for key in samples["after"]:
                for name in ("before", "after"):
                    values = samples[name].get(key, [])
                    if values:
                        unit = "KiB" if key[0] == "memory" else "ms"
                        print("  %-10s %-8s %-6s %12.3f %s   (%.3f to %.3f over %d runs)" % (
                            key[0], key[1], name, statistics.median(values), unit, min(values), max(values), len(values)),
                            flush=True)


if __name__ == "__main__":
    main()
