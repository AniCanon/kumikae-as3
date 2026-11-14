#!/usr/bin/env python3
"""Checks the rules of docs/cpp-guidelines.md that a script can judge.

    python3 check_style.py <compiler dir> [--update]
    python3 check_style.py --self-test

  A .cpp file of more than 800 lines, a header of more than 500, a function of more than 120, and a banner
  comment (`// ---- name ----`) are faults.

  tools/style_baseline.txt lists the faults that were there when the check was added, one per line. The
  list may only shrink: a fault that is not on it fails, and so does an entry whose fault is gone (take
  the line out). --update rewrites the list from what is found; read the result before committing it.
"""
import os
import re
import sys
import tempfile

FOLDERS = ["src", "ir", "vm", "godot_ext", "lsp", "tools"]
LIMITS = {".cpp": 800, ".h": 500}
FUNCTION_LIMIT = 120
# A dispatch switch whose cases each call a named function (guidelines, section 1).
DISPATCH = {
    "Interpreter::run",  # The interpreter's loop, one case per operation.
    "walk",  # src/ast_walk.h: the children of each kind of node.
    "node",  # src/ast_printer.cpp: the text of each kind of node.
}

BANNER = re.compile(r"^\s*//\s*-{3,}")
HEAD = re.compile(r"^(\t?)(?!\t)(?:[\w:<>,*&~ ]+?[ *&])?~?(?P<name>[A-Za-z_][\w:]*)\((?:[^;{}]*)\)\s*(?:const\s*)?(?:noexcept\s*)?"
                  r"(?:override\s*)?(?:final\s*)?(?::[^;{}]*)?\{$")
NOT_FUNCTIONS = {"if", "for", "while", "switch", "else", "return", "do", "catch"}


def functions(lines):
    """(name, first line, length) of each function defined at the top of a file or of a class in it."""
    found = []
    open_at = None
    for number, line in enumerate(lines, 1):
        if open_at is None:
            head = HEAD.match(line)
            if head and head.group("name").split("::")[-1] not in NOT_FUNCTIONS:
                open_at = (head.group("name"), number, head.group(1))
        elif line == open_at[2] + "}":
            found.append((open_at[0], open_at[1], number - open_at[1] + 1))
            open_at = None
    return found


def faults(root):
    result = set()
    for folder in FOLDERS:
        base = os.path.join(root, folder)
        for name in sorted(os.listdir(base)) if os.path.isdir(base) else []:
            extension = os.path.splitext(name)[1]
            if extension not in LIMITS:
                continue
            path = "%s/%s" % (folder, name)
            with open(os.path.join(base, name)) as handle:
                lines = handle.read().split("\n")
            if lines and lines[-1] == "":
                lines.pop()
            if len(lines) > LIMITS[extension]:
                result.add("%s: the file is over %d lines" % (path, LIMITS[extension]))
            if any(BANNER.match(line) for line in lines):
                result.add("%s: banner comments" % path)
            for function, _, length in functions(lines):
                if length > FUNCTION_LIMIT and function not in DISPATCH:
                    result.add("%s: %s() is over %d lines" % (path, function, FUNCTION_LIMIT))
    return result


def check(root, update=False):
    baseline_path = os.path.join(root, "tools", "style_baseline.txt")
    found = faults(root)
    if update:
        with open(baseline_path, "w") as handle:
            handle.write("".join(line + "\n" for line in sorted(found)))
        return []
    known = set()
    if os.path.exists(baseline_path):
        with open(baseline_path) as handle:
            known = {line.rstrip("\n") for line in handle if line.strip()}
    problems = ["new: " + fault for fault in sorted(found - known)]
    problems += ["fixed, so take it out of tools/style_baseline.txt: " + fault for fault in sorted(known - found)]
    return problems


def self_test():
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, "src"))
        os.makedirs(os.path.join(root, "tools"))
        body = "".join("\tint line%d = %d;\n" % (i, i) for i in range(130))
        with open(os.path.join(root, "src", "long.cpp"), "w") as handle:
            handle.write("// ---- helpers ----\nstatic int big(int p_value) {\n" + body + "\treturn p_value;\n}\n" +
                         "void Thing::small() const {\n\tif (true) {\n\t}\n}\n" + "int filler;\n" * 700)
        with open(os.path.join(root, "src", "fine.h"), "w") as handle:
            handle.write("struct Fine {\n\tint get() const {\n\t\treturn 1;\n\t}\n};\n")
        with open(os.path.join(root, "src", "wide.h"), "w") as handle:
            handle.write("int declared;\n" * 501)
        expected = {"src/long.cpp: the file is over 800 lines", "src/long.cpp: banner comments",
                    "src/long.cpp: big() is over 120 lines", "src/wide.h: the file is over 500 lines"}
        found = faults(root)
        assert found == expected, found
        assert check(root) == ["new: " + fault for fault in sorted(expected)]
        check(root, update=True)
        assert check(root) == []
        os.remove(os.path.join(root, "src", "wide.h"))
        assert check(root) == ["fixed, so take it out of tools/style_baseline.txt: src/wide.h: the file is over 500 lines"]
    print("check_style: self-test OK")


def main():
    if "--self-test" in sys.argv:
        self_test()
        return
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    problems = check(os.path.abspath(sys.argv[1]), "--update" in sys.argv)
    for problem in problems:
        print("STYLE " + problem)
    print("style: %s" % ("OK" if not problems else "%d problems" % len(problems)))
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
