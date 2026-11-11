"""Run one benchmark in one language and read back the runner's JSON line."""
from __future__ import annotations

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import godot_bin  # noqa: E402

BENCHES = ["empty", "move_nodes", "per_node_process", "ray_queries", "signal_traffic", "nbody", "sieve",
           "array_conversion", "saxpy", "shapes", "alloc"]


ORIGINAL = BENCHES[:8]  # what the first spike measured; version 1 exists only for these
JUDGED = ["move_nodes", "per_node_process", "ray_queries", "signal_traffic", "nbody", "sieve",
          "saxpy", "shapes", "alloc"]  # what the version 2 gate judges; the last three are holdouts


def command(bench: str, lang: str, mode: str, binary: str | None = None) -> list[str]:
    if binary is None:
        head = [godot_bin.godot_path(), "--path", godot_bin.PROJECT]
    else:
        head = [binary]
    return head + ["--headless", "--fixed-fps", "60", "--",
                   f"--bench={bench}", f"--lang={lang}", f"--mode={mode}"]


def run(bench: str, lang: str, mode: str, binary: str | None = None) -> dict:
    result = subprocess.run(command(bench, lang, mode, binary), capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        raise RuntimeError(f"{bench}/{lang}/{mode} exited {result.returncode}: {result.stderr.strip()}")
    for line in reversed(result.stdout.splitlines()):
        if line.startswith("{"):
            return json.loads(line)
    raise RuntimeError(f"{bench}/{lang}/{mode} printed no result: {result.stdout}\n{result.stderr}")


def parity(bench: str, langs: list[str], binary: str | None = None) -> None:
    sums = {lang: run(bench, lang, "verify", binary)["checksum"] for lang in langs}
    first = sums[langs[0]]
    for lang, value in sums.items():
        scale = max(abs(first), abs(value), 1e-300)
        assert abs(value - first) <= 1e-6 * scale, f"{bench}: checksums differ: {sums}"


def rotated(langs: list[str], repetition: int) -> list[str]:
    """The order of languages for a repetition: rotated by one each time, so none always follows another."""
    shift = repetition % len(langs)
    return langs[shift:] + langs[:shift]


def sweep(binary: str | None, repetition: int, langs: list[str], benches: list[str], progress=None) -> list[dict]:
    """One repetition: every benchmark once in every language that has it."""
    rows = []
    for bench in benches:
        for lang in rotated(langs, repetition):
            if lang == "as3" and bench not in ORIGINAL:
                continue  # version 1 exists only for the original benchmarks
            if lang == "as3v2" and bench not in JUDGED:
                continue
            if lang == "floor" and bench not in JUDGED[:6]:
                continue  # the holdouts have no floor
            row = run(bench, lang, "measure", binary)
            row["repetition"] = repetition
            rows.append(row)
            if progress:
                progress(row)
    return rows
