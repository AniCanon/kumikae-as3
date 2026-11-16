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

FOLDERS = ["src", "ir", "vm", "backend_cpp", "runtime", "godot_ext", "lsp", "tools"]
LIMITS = {".cpp": 800, ".h": 500}
FUNCTION_LIMIT = 120
# A dispatch switch whose cases each call a named function (guidelines, section 1).
DISPATCH = {
    "vm/interpreter_run.cpp: Interpreter::run",  # The interpreter's loop, one case per operation.
    "src/ast_walk.h: walk",  # The children of each kind of node.
    "src/ast_printer.cpp: Printer::node",  # The text of each kind of node.
}

BANNER = re.compile(r"^\s*//\s*[-=*#]{3,}")
SCOPES = re.compile(r"^(?:template\s*<[^{]*>\s*)?(?:typedef\s+)?(?:namespace|class|struct|union|enum|extern)\b")
NOT_FUNCTIONS = re.compile(r"^(?:if|for|while|switch|else|do|try|catch|return|case|default)\b")
NAME = re.compile(r"((?:\w+::)*(?:operator\s*(?:\(\)|[^\s(]+)|~?\w+))\s*$")


def name_of(head):
    """The name a function's head gives it: `Class::method`, `operator==`, or what a lambda is assigned to."""
    pointer = re.search(r"\(\s*\*\s*(\w+)\s*\(", head)  # A function that returns a pointer to a function.
    if pointer:
        return pointer.group(1)
    before = head[:head.index("(")].rstrip()
    if before.endswith("]") and "=" in before:  # A lambda.
        before = before[:before.rindex("=")].rstrip()
    found = NAME.search(before)
    return re.sub(r"\s+", "", found.group(1)) if found else "?"


def blank(text):
    """The text with comments and the insides of strings blanked out, line for line."""
    out, i, n = [], 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif two == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("".join(c if c == "\n" else " " for c in text[i:end]))
            i = end
        elif text[i] in "\"'":
            quote, start = text[i], i
            i += 1
            while i < n and text[i] != quote and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            i = min(i + 1, n)
            out.append(quote + " " * (i - start - 2) + quote if i - start >= 2 else text[start:i])
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def functions(lines):
    """(name, first line, length) of every function that is not inside another: in a namespace, a class
    or a nested class, whatever its head looks like."""
    # A preprocessor directive, with the lines its trailing backslashes continue it onto (a table written as
    # a macro), is not part of any head.
    kept, directive = [], False
    for line in lines:
        directive = directive or line.lstrip().startswith("#")
        kept.append("" if directive else line)
        directive = directive and line.rstrip().endswith("\\")
    text = blank("\n".join(kept))
    found = []
    stack = []  # One entry per open brace: "scope", ("function", name, first line) or "other".
    head_start = 0
    parentheses = 0
    line = 1
    for i, c in enumerate(text):
        if c == "\n":
            line += 1
        elif c == "(":
            parentheses += 1
        elif c == ")":
            parentheses = max(0, parentheses - 1)
        elif c == ";" and parentheses == 0:
            head_start = i + 1
        elif c == "{":
            if parentheses:
                stack.append("other")  # Braces in a default argument.
                continue
            head = text[head_start:i].strip()
            inside_function = any(entry != "scope" for entry in stack)
            if not inside_function and SCOPES.match(head) and "(" not in head.split("{")[0].split(":")[0]:
                stack.append("scope")
            elif not inside_function and "(" in head and not NOT_FUNCTIONS.match(head):
                first = line - text[head_start:i].lstrip().count("\n")
                stack.append(("function", name_of(head), first))
            else:
                stack.append("other")
            head_start = i + 1
        elif c == "}":
            if parentheses and stack and stack[-1] == "other":
                stack.pop()
                continue
            entry = stack.pop() if stack else "other"
            if isinstance(entry, tuple):
                found.append((entry[1], entry[2], line - entry[2] + 1))
            head_start = i + 1
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
                if length > FUNCTION_LIMIT and "%s: %s" % (path, function) not in DISPATCH:
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
            # A table written as a macro, one row per line, is no function, however long.
            table = "#define TABLE(X) \\\n" + "".join("\tX(row%d, \"(\") \\\n" % i for i in range(130)) + "\n"
            handle.write(table + "enum class Row { TABLE(NAME) };\n" +
                         "struct Fine {\n\tint get() const {\n\t\treturn 1;\n\t}\n};\n")
        with open(os.path.join(root, "src", "wide.h"), "w") as handle:
            handle.write("int declared;\n" * 501)
        shapes = [
            "auto Thing::place(int p_a,\n\t\tint p_b) -> Thing::Place {",
            "[[nodiscard]] bool marked(int p_value) {",
            "bool Thing::operator==(const Thing &p_other) const {",
            "Thing::~Thing() {",
            "Thing::Thing(int p_a) :\n\t\ta_(p_a) {",
            "void defaults(std::vector<int> p_list = {}, char p_end = ';') { // a comment {",
            "int (*pick(int p_which))(int) {",
            "static const auto at_file_scope = [](int p_value) {",
            "template <typename T>\nvoid generic(T &&p_value) {",
        ]
        text = "#include <vector>\n\nnamespace kas3 {\nclass Outer {\n\tstruct Inner {\n\t\tvoid nested() {\n" + body + "\t\t}\n\t};\n};\n"
        for shape in shapes:
            text += shape + "\n" + body + ("};\n" if "= [" in shape else "}\n")
        text += "} // namespace kas3\n"
        with open(os.path.join(root, "src", "shapes.cpp"), "w") as handle:
            handle.write(text)
        names = ["Thing::place", "marked", "Thing::operator==", "Thing::~Thing", "Thing::Thing", "defaults", "pick",
                 "at_file_scope", "generic", "nested"]
        expected = {"src/long.cpp: the file is over 800 lines", "src/long.cpp: banner comments",
                    "src/long.cpp: big() is over 120 lines", "src/wide.h: the file is over 500 lines",
                    "src/shapes.cpp: the file is over 800 lines"}
        expected |= {"src/shapes.cpp: %s() is over 120 lines" % name for name in names}
        found = faults(root)
        assert found == expected, found
        assert check(root) == ["new: " + fault for fault in sorted(expected)]
        check(root, update=True)
        assert check(root) == []
        assert [name for name, _, length in functions(open(os.path.join(root, "src", "long.cpp")).read().split("\n"))] == ["big", "Thing::small"]
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
