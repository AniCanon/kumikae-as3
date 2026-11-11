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

GROUPS = {
    "empty": "baseline",
    "move_nodes": "gameplay",
    "per_node_process": "gameplay",
    "ray_queries": "gameplay",
    "signal_traffic": "gameplay",
    "nbody": "compute",
    "sieve": "compute",
    "array_conversion": "conversion",
}
GAMEPLAY_MARGIN = 1.10  # AS3 must be at least 10% ahead of C# on gameplay code.
COMPUTE_NOISE = 1.03  # "No worse" on pure computation, with 3% allowed for noise.


def _medians(rows: list[dict], bench: str, lang: str) -> list[float]:
    return [row["median_us"] for row in rows if row["bench"] == bench and row["lang"] == lang]


def verdict(rows: list[dict]) -> dict:
    """Applies the gate rules to measured rows. A bench with no C# or AS3 numbers cannot pass."""
    benches = {}
    for bench, group in GROUPS.items():
        runs = {lang: _medians(rows, bench, lang) for lang in ("as3", "cs", "gd")}
        mid = {lang: (statistics.median(values) if values else None) for lang, values in runs.items()}
        entry = {
            "group": group,
            "as3_us": mid["as3"],
            "cs_us": mid["cs"],
            "gd_us": mid["gd"],
            "ratio_cs_over_as3": (mid["cs"] / mid["as3"]) if mid["cs"] and mid["as3"] else None,
            "pass": None,
        }
        if group in ("gameplay", "compute"):
            if not runs["as3"] or not runs["cs"]:
                entry["pass"] = False
            elif group == "gameplay":
                ahead = mid["as3"] * GAMEPLAY_MARGIN <= mid["cs"]
                beyond_noise = max(runs["as3"]) < min(runs["cs"])
                entry["pass"] = ahead and beyond_noise
            else:
                entry["pass"] = mid["as3"] <= mid["cs"] * COMPUTE_NOISE
        benches[bench] = entry
    judged = [entry["pass"] for entry in benches.values() if entry["pass"] is not None]
    return {"pass": bool(judged) and all(judged), "benches": benches}


def _cell(value) -> str:
    return "-" if value is None else "%.0f" % value


def table(result: dict, rows: list[dict]) -> str:
    lines = ["%-18s %-10s %9s %9s %9s %8s  %s" % ("bench", "group", "as3 us", "c# us", "gd us", "c#/as3", "gate")]
    for bench, entry in result["benches"].items():
        ratio = "-" if entry["ratio_cs_over_as3"] is None else "%.2f" % entry["ratio_cs_over_as3"]
        gate = {True: "pass", False: "FAIL", None: ""}[entry["pass"]]
        lines.append("%-18s %-10s %9s %9s %9s %8s  %s" % (bench, entry["group"], _cell(entry["as3_us"]),
                                                          _cell(entry["cs_us"]), _cell(entry["gd_us"]), ratio, gate))
    lines.append("")
    lines.append("worst frame (max of max_us across runs):")
    for bench in result["benches"]:
        worst = {}
        for lang in ("as3", "cs", "gd"):
            values = [row["max_us"] for row in rows if row["bench"] == bench and row["lang"] == lang]
            worst[lang] = max(values) if values else None
        lines.append("%-18s as3 %8s   c# %8s   gd %8s" % (bench, _cell(worst["as3"]), _cell(worst["cs"]),
                                                         _cell(worst["gd"])))
    lines.append("")
    lines.append("VERDICT: %s" % ("PASS" if result["pass"] else "FAIL"))
    return "\n".join(lines)


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
    parser.add_argument("--langs", default="gd,cs,as3")
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
        run_bench.parity(bench, langs, binary)
    rows = run_bench.matrix(binary, args.repetitions, langs, benches,
                            progress=lambda row: print("  %-18s %-3s %8d us" % (row["bench"], row["lang"],
                                                                               row["median_us"]), flush=True))
    result = verdict(rows)
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "results.json"), "w") as handle:
        json.dump({"setting": "editor" if args.editor else "release export", "langs": langs, "rows": rows,
                   "verdict": result}, handle, indent=1)
    print()
    print("setting: %s" % ("editor binary (not the official gate setting)" if args.editor else "release export"))
    print(table(result, rows))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
