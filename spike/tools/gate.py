"""Measure every benchmark and print the stage 0 verdict.

Usage: python3 spike/tools/gate.py [--editor] [--langs gd,cs,as3] [--repetitions 5] [--benches a,b]

By default this exports a release build and measures that, which is the setting the gate is defined for.
--editor measures with the editor binary instead: quicker, useful while working, not the official number.
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import godot_bin  # noqa: E402
import run_bench  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out")

HOLDOUTS = ["saxpy", "shapes", "alloc"]
FLOOR_MARGIN = 0.97  # version 2 more than 3% faster than the floor suggests skipped work
CALIBRATION_TOLERANCE = 0.05


def _medians(rows: list[dict], bench: str, lang: str) -> list[float]:
    return [row["median_us"] for row in rows if row["bench"] == bench and row["lang"] == lang]


def drifted(baseline: float, value: float) -> bool:
    """True when a calibration time differs from the baseline by more than the tolerance."""
    return abs(value - baseline) > CALIBRATION_TOLERANCE * baseline


def verdict(rows: list[dict]) -> dict:
    """The version 2 gate: ahead of C# on every judged benchmark, with no overlap between the runs."""
    benches = {}
    for bench in run_bench.BENCHES:
        runs = {lang: _medians(rows, bench, lang) for lang in ("as3v2", "as3", "cs", "gd", "floor")}
        mid = {lang: (statistics.median(values) if values else None) for lang, values in runs.items()}
        entry = {
            "holdout": bench in HOLDOUTS,
            "as3v2_us": mid["as3v2"],
            "as3v1_us": mid["as3"],
            "cs_us": mid["cs"],
            "gd_us": mid["gd"],
            "floor_us": mid["floor"],
            "ratio_cs_over_as3": (mid["cs"] / mid["as3v2"]) if mid["cs"] and mid["as3v2"] else None,
            "below_floor": bool(mid["floor"] and mid["as3v2"] and mid["as3v2"] < mid["floor"] * FLOOR_MARGIN),
            "pass": None,
        }
        if bench in run_bench.JUDGED:
            if not runs["as3v2"] or not runs["cs"]:
                entry["pass"] = False
            else:
                entry["pass"] = mid["as3v2"] < mid["cs"] and max(runs["as3v2"]) < min(runs["cs"])
        benches[bench] = entry
    judged = [entry["pass"] for entry in benches.values() if entry["pass"] is not None]
    return {"pass": bool(judged) and all(judged), "benches": benches}


def _cell(value) -> str:
    return "-" if value is None else "%.0f" % value


def table(result: dict, rows: list[dict]) -> str:
    head = ("bench", "v2 us", "c# us", "c#/v2", "v2 range", "c# range", "floor", "v1 us", "gd us", "gate")
    lines = ["%-18s %8s %8s %6s %13s %13s %7s %7s %7s  %s" % head]
    for bench, entry in result["benches"].items():
        ratio = "-" if entry["ratio_cs_over_as3"] is None else "%.2f" % entry["ratio_cs_over_as3"]
        gate = {True: "pass", False: "FAIL", None: ""}[entry["pass"]]
        if entry["holdout"]:
            gate += " (holdout)"
        if entry["below_floor"]:
            gate += " BELOW FLOOR"
        spans = {}
        for lang in ("as3v2", "cs"):
            values = _medians(rows, bench, lang)
            spans[lang] = "%d-%d" % (min(values), max(values)) if values else "-"
        lines.append("%-18s %8s %8s %6s %13s %13s %7s %7s %7s  %s" % (
            bench, _cell(entry["as3v2_us"]), _cell(entry["cs_us"]), ratio, spans["as3v2"], spans["cs"],
            _cell(entry["floor_us"]), _cell(entry["as3v1_us"]), _cell(entry["gd_us"]), gate))
    lines.append("")
    lines.append("p99 frame (median of runs) and worst frame:")
    for bench in result["benches"]:
        parts = []
        for lang in ("as3v2", "cs"):
            p99 = [row["p99_us"] for row in rows if row["bench"] == bench and row["lang"] == lang]
            worst = [row["max_us"] for row in rows if row["bench"] == bench and row["lang"] == lang]
            parts.append("%s p99 %s worst %s" % (lang, _cell(statistics.median(p99)) if p99 else "-",
                                                _cell(max(worst)) if worst else "-"))
        lines.append("%-18s %s" % (bench, "   ".join(parts)))
    lines.append("")
    lines.append("VERDICT: %s" % ("PASS" if result["pass"] else "FAIL"))
    return "\n".join(lines)


def calibrate() -> float:
    """Median time of the fixed standalone workload, in microseconds."""
    out = subprocess.run([os.path.join(ROOT, "build", "calib")], capture_output=True, text=True, check=True)
    return float(out.stdout.strip())


def export_release() -> str:
    """Exports the project with the macOS preset and returns the path of the binary inside the app."""
    os.makedirs(OUT, exist_ok=True)
    app = os.path.join(OUT, "spike.app")
    godot_bin.ensure_imported()
    result = subprocess.run([godot_bin.godot_path(), "--headless", "--path", godot_bin.PROJECT,
                             "--export-release", "macOS", app], capture_output=True, text=True, timeout=1800)
    macos = os.path.join(app, "Contents", "MacOS")
    if result.returncode != 0 or not os.path.isdir(macos):
        sys.exit("export failed:\n" + result.stdout[-2000:] + result.stderr[-2000:])
    # The exporter signs with the hardened runtime, and macOS then kills the .NET build on launch.
    # A plain ad-hoc signature runs. This only changes how the local test app is signed.
    subprocess.run(["codesign", "--force", "--deep", "-s", "-", app], capture_output=True, text=True, check=True)
    binaries = [name for name in os.listdir(macos) if not name.endswith(".dylib")]
    return os.path.join(macos, binaries[0])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", action="store_true", help="measure with the editor binary, without exporting")
    parser.add_argument("--langs", default="gd,cs,as3,as3v2,floor")
    parser.add_argument("--benches", default=",".join(run_bench.BENCHES))
    parser.add_argument("--repetitions", type=int, default=5)
    args = parser.parse_args()
    langs = args.langs.split(",")
    benches = args.benches.split(",")
    binary = None if args.editor else export_release()
    if args.editor:
        godot_bin.ensure_imported()
    # The same work first: a benchmark that computes something different stops the measurement.
    for bench in benches:
        have = [lang for lang in langs if not (lang == "as3" and bench not in run_bench.ORIGINAL)
                and not (lang == "as3v2" and bench not in run_bench.JUDGED)
                and not (lang == "floor" and bench not in run_bench.JUDGED[:6])]
        run_bench.parity(bench, have, binary)
    show = lambda row: print("  %-18s %-6s %8d us" % (row["bench"], row["lang"], row["median_us"]), flush=True)
    baseline = calibrate()
    print("calibration baseline: %.1f us" % baseline, flush=True)
    rows, calibrations, discarded = [], [], 0
    repetition = 0
    while repetition < args.repetitions:
        before = calibrate()
        attempt = run_bench.sweep(binary, repetition, langs, benches, progress=show)
        after = calibrate()
        if drifted(baseline, before) or drifted(baseline, after):
            discarded += 1
            print("repetition %d discarded: calibration %.1f / %.1f us against %.1f" % (repetition, before, after,
                                                                                     baseline), flush=True)
            if discarded > 5:
                sys.exit("the machine would not hold still: more than five repetitions discarded")
            continue
        calibrations.append([before, after])
        rows += attempt
        repetition += 1
    result = verdict(rows)
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "results.json"), "w") as handle:
        json.dump({"setting": "editor" if args.editor else "release export", "langs": langs,
                   "calibration_baseline_us": baseline, "calibrations_us": calibrations,
                   "discarded_repetitions": discarded, "rows": rows, "verdict": result}, handle, indent=1)
    print()
    print("setting: %s" % ("editor binary (not the official gate setting)" if args.editor else "release export"))
    print("calibration: baseline %.1f us, %d repetitions discarded" % (baseline, discarded))
    print(table(result, rows))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
