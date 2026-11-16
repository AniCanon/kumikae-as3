"""What `kas3c cpp` writes for the benchmark's script, where its shape is its speed.

Usage: test_generated.py <kas3c> <tests dir>

run_native.py holds what generated code does to the interpreter; this holds how it does the things the
timings rest on, so that a change that undoes one fails here and not only in a benchmark:

  - a field of the method's own object, used in a loop nothing else can see it from, is read once before
    and written once after: the loop itself has it in a local;
  - an object handed from where it was made to a variable, as the last thing done with it there, is handed
    over with its reference: none is taken and given back;
  - two whole numbers (ints, uints, sums of them, whole constants) are compared as integers, not as the
    Numbers the IR makes of them first: a loop's test is then one the C++ compiler can reason about;
  - a loop over a vector of numbers has a copy in which the vector's length and elements are locals: no
    look at the vector itself, and one test of each index against the local;
  - a method that calls nothing and cannot fail keeps no count of the depth, and a call of it is not
    followed by a look at whether an error is on its way out.
"""
import os
import re
import subprocess
import sys
import tempfile

kas3c = os.path.abspath(sys.argv[1])
tests = os.path.abspath(sys.argv[2])
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append("%s\n%s" % (name, detail))


def functions(folder, file):
    """{name: body} for the functions of one generated file, each without its #line markers."""
    text = open(os.path.join(folder, file)).read()
    found = {}
    for match in re.finditer(r"^[\w:\* ]+?\bf\d+_(\w+)\(Context \*c[^\n]*\) \{\n(.*?)^\}\n", text, re.M | re.S):
        found[match.group(1)] = "\n".join(line for line in match.group(2).split("\n") if not line.startswith("#line"))
    return found


def loops(body):
    """The text of each loop: from a label to the last jump back to it."""
    found = []
    for label in re.findall(r"^([LV]\d+):;$", body, re.M):
        start = body.index(label + ":;")
        back = body.rfind("goto %s;" % label)
        if back > start:
            found.append(body[start:back])
    return found


with tempfile.TemporaryDirectory() as where:
    sources = os.path.join(tests, "bench", "languages")
    written = subprocess.run([kas3c, "cpp", "--godot", "CmpAs.as", "-o", where], capture_output=True, text=True, cwd=sources)
    check("CmpAs.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        made = functions(where, "CmpAs.cpp")
        work = made.get("CmpAs_work", "")
        check("work is found", work and len(loops(work)) == 1, work)
        inside = "".join(loops(work))
        check("work's loop does not touch the field `total`", "f1_total" not in inside, inside)
        check("work reads `total` once and writes it once", work.count("->f1_total") == 2, work)
        check("work does not look for an error after calls that cannot fail", "c->failing" not in inside, inside)
        check("work compares its whole numbers as integers", "less(" not in work and "equal(" not in work, work)
        check("work calls its two methods", "CmpAs_scaled(" in inside and "CmpAs_bump(" in inside, inside)
        for leaf in ("CmpAs_scaled", "CmpAs_bump"):
            body = made.get(leaf, "")
            check("%s is found" % leaf, bool(body))
            check("%s keeps no count of the depth" % leaf, "c->depth" not in body and "stack_overflow" not in body, body)
        vec = made.get("CmpAs_vec", "")
        check("vec, which calls nothing, keeps no count of the depth", vec and "c->depth" not in vec, vec)

with tempfile.TemporaryDirectory() as where:
    sources = os.path.join(tests, "bench", "languages")
    written = subprocess.run([kas3c, "cpp", "--godot", "frames/NBody.as", "-o", where], capture_output=True, text=True, cwd=sources)
    check("NBody.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        advance = functions(where, "frames_NBody.cpp").get("frames_NBody_advance", "")
        check("advance is found", bool(advance))
        check("a square root is the machine's, with no call of the runtime's", "std::sqrt(" in advance and "math_unary(" not in advance, advance)

with tempfile.TemporaryDirectory() as where:
    written = subprocess.run([kas3c, "cpp", "objects.as", "-o", where], capture_output=True, text=True, cwd=os.path.join(tests, "bench"))
    check("objects.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        made = functions(where, "objects.cpp").get("objects", "")
        inside = "".join(loops(made))
        check("objects' loop is found", "new_object(" in inside, made)
        check("the object made in objects' loop is handed over, not shared", "store(c, " not in inside and "= nullptr;" in inside, inside)

with tempfile.TemporaryDirectory() as where:
    written = subprocess.run([kas3c, "cpp", "vectors.as", "-o", where], capture_output=True, text=True, cwd=os.path.join(tests, "bench"))
    check("vectors.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        made = functions(where, "vectors.cpp").get("vectors", "")
        fast = [loop for loop in loops(made) if "goto S" in loop]
        check("vectors' loops have a copy with locals", len(fast) >= 1, made)
        for loop in fast:
            check("the copy with locals does not look at the vector", "->length" not in loop and "element" not in loop and "thunk(" not in loop,
                  loop)

for failure in failures:
    print("FAIL " + failure[:2500])
print("generated: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
