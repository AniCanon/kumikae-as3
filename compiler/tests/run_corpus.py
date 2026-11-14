#!/usr/bin/env python3
"""Which of the corpus programs the interpreter can run in Godot. Usage: run_corpus.py <kas3c> <tests>

Each program is lowered as the extension lowers it (`kas3c ir --godot`). The ones in corpus/runs.txt must
lower with no error, and so must the ones in corpus/waits.txt, which the interpreter takes but which fail
when run for the reason written there. A program in neither that lowers cleanly fails this test too: the list is then out of
date, and the stage that made the program run adds it. What stops the others is printed, most common
first, so that the next piece of the language is chosen by what the programs need.
"""
import collections
import os
import re
import subprocess
import sys

KAS3C, TESTS = sys.argv[1], sys.argv[2]
CORPUS = os.path.join(TESTS, "corpus")

# A program and the other files it is compiled with.
WITH = {"PerNodeProcess": ["PerNodeChild"]}
# The programs that run, one name to a line: run_godot.py runs each of them inside Godot.
with open(os.path.join(CORPUS, "runs.txt")) as listed:
    RUNS = set(listed.read().split())
# The programs the interpreter takes but that do not work yet when run: the name, then what each waits for.
with open(os.path.join(CORPUS, "waits.txt")) as listed:
    WAITS = {line.split()[0] for line in listed if line.strip()}

failures = []
stopped_by = collections.Counter()
names = sorted(name[:-3] for name in os.listdir(CORPUS) if name.endswith(".as"))
for name in names:
    files = [os.path.join(CORPUS, each + ".as") for each in [name] + WITH.get(name, [])]
    result = subprocess.run([KAS3C, "ir", "--godot", *files], capture_output=True, text=True, timeout=60)
    if result.returncode not in (0, 1):
        failures.append("%s: kas3c ended with %d" % (name, result.returncode))
        continue
    errors = re.findall(r"error: (.*)", result.stderr)
    for error in errors:
        stopped_by[re.sub(r"'[^']*'", "'...'", error)] += 1
    if name in RUNS | WAITS and errors:
        failures.append("%s must be taken by the interpreter, and is stopped by: %s" % (name, errors[0]))
    if name not in RUNS | WAITS and not errors:
        failures.append("%s runs now: add it to corpus/runs.txt" % name)

for what, count in stopped_by.most_common(12):
    print("%4d  %s" % (count, what))
for failure in failures:
    print("FAIL " + failure)
print("corpus: %d of %d run, %d more wait for something at run time, %d failed" % (len(RUNS), len(names), len(WAITS), len(failures)))
sys.exit(1 if failures else 0)
