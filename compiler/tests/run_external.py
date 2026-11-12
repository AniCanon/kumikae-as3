#!/usr/bin/env python3
"""Runs kas3c over the external corpus (see fetch_external.sh) and prints a summary.

    run_external.py <kas3c> [--check] [--show N] [--all]

Without --check: parses every file on its own and counts the files that parse.
With --check: also checks each project as one program and counts the errors by message.
--check leaves out "can't find" errors for imports, which are nearly all Flash's library; --all keeps them.
A crash or a hang is always reported and makes the exit status 1. Nothing else does: these projects
use Flash's library and AS3 features this compiler leaves out, so errors are expected.
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "external")

# Each is checked as one program. Starling's samples are separate programs that reuse class names.
PROJECTS = [
    "as3-signals", "as3corelib", "away3d", "flashpunk", "flixel", "starling/starling", "starling/tests",
    "starling/samples/demo_mobile", "starling/samples/demo_web", "starling/samples/scaffold_mobile",
]


def files_of(project):
    found = []
    for folder, _, names in os.walk(os.path.join(ROOT, project)):
        if os.sep + ".git" in folder:
            continue
        found += [os.path.join(folder, name) for name in names if name.endswith(".as")]
    return sorted(found)


def shape(message):
    """A message with its names and numbers removed, so that like errors count together."""
    message = re.sub(r"(?<![A-Za-z])'[^']*'", "'…'", message)
    return re.sub(r"\b\d+\b", "N", message)


def run(command):
    try:
        done = subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=120)
    except subprocess.TimeoutExpired:
        return None, ""
    return done.returncode, done.stderr + done.stdout


def main():
    kas3c = sys.argv[1]
    check = "--check" in sys.argv
    everything = "--all" in sys.argv
    show = int(sys.argv[sys.argv.index("--show") + 1]) if "--show" in sys.argv else 12
    if not os.path.isdir(ROOT):
        print("no external corpus; run fetch_external.sh")
        return 0
    bad = 0
    for project in PROJECTS:
        files = files_of(project)
        if not files:
            continue
        clean = []
        shapes = collections.Counter()
        examples = {}
        for path in files:
            status, output = run([kas3c, "parse", path])
            if status not in (0, 1):
                print(f"CRASH or hang (status {status}): kas3c parse {path}")
                bad += 1
                continue
            if status == 0:
                clean.append(path)
                continue
            first = output.splitlines()[0] if output else ""
            parts = first.split(": error: ", 1)
            key = shape(parts[1]) if len(parts) == 2 else first
            shapes[key] += 1
            examples.setdefault(key, first)
        print(f"{project}: {len(clean)} of {len(files)} files parse")
        for key, count in shapes.most_common(show):
            print(f"    {count:4}  {key}\n          {os.path.relpath(examples[key], ROOT)}")
        if check and clean:
            status, output = run([kas3c, "check"] + clean)
            if status not in (0, 1):
                print(f"CRASH or hang (status {status}): kas3c check on {project}")
                bad += 1
                continue
            shapes = collections.Counter()
            examples = {}
            imports = 0
            for line in output.splitlines():
                parts = line.split(": error: ", 1)
                if len(parts) != 2:
                    continue
                if parts[1].startswith("can't find ") and not everything:
                    imports += 1
                    continue
                key = shape(parts[1])
                shapes[key] += 1
                examples.setdefault(key, line)
            print(f"  checked {len(clean)} files as one program: {sum(shapes.values())} errors, "
                  f"and {imports} imports not found")
            for key, count in shapes.most_common(show):
                print(f"    {count:5}  {key}\n           {os.path.relpath(examples[key], ROOT)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
