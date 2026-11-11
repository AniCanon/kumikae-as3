"""Textual check that the AS3-shaped C++ in spike/benches obeys the plan's translation rules."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BANNED = [
    (r"godot_cpp/classes/", "includes a godot-cpp class wrapper (rule 9)"),
    (r"\btry\b", "uses try (rule 4)"),
    (r"\bthrow\b", "uses throw (rule 4)"),
    (r"\bdynamic_cast\b", "uses dynamic_cast"),
    (r"std::atomic", "uses std::atomic (main thread only)"),
]


def strip_comments(text: str) -> str:
    return re.sub(r"//[^\n]*", "", text)


def check(path: str, text: str) -> list:
    problems = []
    code = strip_comments(text)
    for pattern, why in BANNED:
        if re.search(pattern, code):
            problems.append(f"{path}: {why}")
    # Every function that opens a frame must store a line somewhere in it.
    parts = code.split("AS3_FRAME(")
    for part in parts[1:]:
        if "AS3_LINE(" not in part:
            problems.append(f"{path}: a function opens AS3_FRAME but never stores AS3_LINE (rule 5)")
    # A Godot collection may only appear where it is being converted (rule 7).
    for number, line in enumerate(code.splitlines(), 1):
        if re.search(r"godot::(Dictionary|Array|PackedVector2Array)\b", line) and \
                not re.search(r"as3::(from_godot_|to_godot_|to_packed_)", line):
            problems.append(f"{path}:{number}: touches a Godot collection without converting it (rule 7)")
    return problems


def main() -> int:
    problems = []
    files = sorted(glob.glob(os.path.join(ROOT, "benches", "*.cpp")))
    for path in files:
        with open(path) as handle:
            problems += check(os.path.relpath(path, ROOT), handle.read())
    for problem in problems:
        print(problem)
    print("check_rules: %d files, %s" % (len(files), "OK" if not problems else "%d problems" % len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
