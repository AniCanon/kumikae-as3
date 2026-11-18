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
    followed by a look at whether an error is on its way out;
  - push() of one value onto a vector with room for it is written where it is: no call of the library, and
    for a Vector2 into a vector that holds each in its place, no handle;
  - a Vector2 that is a field of the script, read only to be multiplied, is read out of the field's own
    handle: no handle is made for the read and given back after it;
  - a loop can be stopped when a call has run too long (Error #1502), and a loop counted by a whole number
    against a bound that stays as it is pays nothing for that each round: its test is one comparison, of
    the counter and the end of a stretch, and the host's alarm is looked at where a stretch ends. A counter
    that goes up to a vector's own length is an index of the vector with nothing more asked;
  - a sum of whole numbers handed on untyped (pushed onto an Array) is the int it is by integer arithmetic,
    whatever the C++ compiler makes of the loop it is in;
  - a constructor built to tell what its object is made with (--remember) keeps the arguments where the
    object is, and tells the host only where that can't be done.
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


def loops(body, copies="LV"):
    """The text of each loop: from its label to the last jump back to it, or, for a loop counted by its
    stretches, to the label its rounds begin at (N for L, W for V). With copies="V", only the copies in
    which the vectors' lengths and elements are locals."""
    found = []
    for label in re.findall(r"^(?:\[\[maybe_unused\]\] )?([%s]\d+):;$" % copies, body, re.M):
        start = body.index(label + ":;")
        rounds = {"L": "N", "V": "W"}[label[0]] + label[1:]
        back = max(body.rfind("goto %s;" % label), body.rfind("goto %s;" % rounds))
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
        # A loop can be stopped (Error #1502), so the function it is in can fail: work's is counted by `i`
        # against a bound that stays, and looks at the alarm where a stretch ends, not each round.
        check("work's loop is held to a stretch, and counts no rounds", "= stretch_" in inside and "--rounds" not in work and
              "overdue(c)" in inside, inside)

with tempfile.TemporaryDirectory() as where:
    sources = os.path.join(tests, "bench", "languages")
    written = subprocess.run([kas3c, "cpp", "--godot", "frames/NBody.as", "-o", where], capture_output=True, text=True, cwd=sources)
    check("NBody.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        advance = functions(where, "frames_NBody.cpp").get("frames_NBody_advance", "")
        check("advance is found", bool(advance))
        check("a square root is the machine's, with no call of the runtime's", "std::sqrt(" in advance and "math_unary(" not in advance, advance)
        inside = "".join(loops(advance))
        check("advance's loops are found", bool(inside), advance)
        check("a body read out of the vector counts no reference", "store(c" not in inside and "put(c" not in inside,
              advance)

with tempfile.TemporaryDirectory() as where:
    sources = os.path.join(tests, "bench", "languages")
    written = subprocess.run([kas3c, "cpp", "--godot", "frames/Saxpy.as", "-o", where], capture_output=True, text=True, cwd=sources)
    check("Saxpy.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        process = functions(where, "frames_Saxpy.cpp").get("frames_Saxpy__process", "")
        inside = "".join(loops(process))
        check("Saxpy's loops are found", bool(inside), process)
        check("a static field no code in the function can write is read once, not in its loops", "c->globals[" not in inside and
              "c->globals[" in process, process)
        fast = loops(process, "V")
        check("Saxpy's loops over its vectors are held to stretches as indexes, and count no rounds", len(fast) >= 2 and
              all("= stretch_index" in loop for loop in fast) and "--rounds" not in process, process)

with tempfile.TemporaryDirectory() as where:
    sources = os.path.join(tests, "bench", "languages")
    written = subprocess.run([kas3c, "cpp", "--godot", "--remember", "frames/ArrayConversion.as", "-o", where], capture_output=True, text=True, cwd=sources)
    check("ArrayConversion.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        process = functions(where, "frames_ArrayConversion.cpp").get("frames_ArrayConversion__process", "")
        pushes = [loop for loop in loops(process) if "push_room(" in loop and loop.count("vector_call(") == 1]
        check("ArrayConversion's two loops of push() are found, each written where the vector has room, with the library's push only where "
              "there is none", len(pushes) == 2 and all("} else" in loop for loop in pushes), process)
        whole = [loop for loop in pushes if "value_place(" in loop]
        check("a Vector2 is pushed into its place as it is: a handle is made only for a vector that must grow or holds none whole",
              len(whole) == 1 and whole[0].index("value_place(") < whole[0].index("Boxed<"), process)

with tempfile.TemporaryDirectory() as where:
    sources = os.path.join(tests, "bench", "languages")
    written = subprocess.run([kas3c, "cpp", "--godot", "frames/MoveNodes.as", "-o", where], capture_output=True, text=True, cwd=sources)
    check("MoveNodes.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        process = functions(where, "frames_MoveNodes.cpp").get("frames_MoveNodes__process", "")
        moving = [loop for loop in loops(process) if "f2_velocity" in loop]
        check("MoveNodes' loop is found", len(moving) == 1, process)
        for loop in moving:
            check("the velocity is read out of the field's own handle: none is made for the read",
                  "unbox<Vector2>(c, static_cast<C3_frames_MoveNodes *>(r0)->f2_velocity.handle)" in loop and
                  not re.search(r"store\(c, r\d+, [^;]*f2_velocity\)", loop), loop)
            check("and multiplied where it is, by the runtime's own formula", "= multiply(a0, r1);" in loop, loop)

with tempfile.TemporaryDirectory() as where:
    written = subprocess.run([kas3c, "cpp", "objects.as", "-o", where], capture_output=True, text=True, cwd=os.path.join(tests, "bench"))
    check("objects.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        made = functions(where, "objects.cpp").get("objects", "")
        inside = "".join(loops(made))
        check("objects' loop is found, and makes its object of one let go of where there is one", "made_object(" in inside, made)
        check("the object made in objects' loop is handed over, not shared", "store(c, " not in inside and "= nullptr;" in inside, inside)

with tempfile.TemporaryDirectory() as where:
    written = subprocess.run([kas3c, "cpp", "vectors.as", "-o", where], capture_output=True, text=True, cwd=os.path.join(tests, "bench"))
    check("vectors.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        made = functions(where, "vectors.cpp").get("vectors", "")
        fast = loops(made, "V")
        check("vectors' loops have a copy with locals", len(fast) >= 1, made)
        for loop in fast:
            check("the copy with locals does not look at the vector", "->length" not in loop and "element" not in loop and "thunk(" not in loop,
                  loop)
            check("a counter that goes up to the vector's own length is its index with nothing more asked", "goto S" not in loop, loop)

with tempfile.TemporaryDirectory() as where:
    written = subprocess.run([kas3c, "cpp", "static_const_still.as", "-o", where], capture_output=True, text=True, cwd=os.path.join(tests, "run"))
    check("static_const_still.as is written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        both = functions(where, "Sub.cpp").get("Sub_both", "")
        inside = "".join(loops(both))
        # MORE is Sub's own, and the function sets no class of its own up: read once. LIMIT is a base's, which
        # the function sets up itself before it reads it: read where the function reads it, after that.
        check("a constant of the function's own class is read once, before its loops", re.search(r"^\tconst int32_t g\d+ = c->globals\[\d+\]\.i;$", both, re.M) and
              len(re.findall(r"^\tconst int32_t g\d+ = ", both, re.M)) == 1, both)
        check("a constant of a class the function sets up itself is read after it is set up", "use_class(c, " in inside and "c->globals[" in inside, both)

with tempfile.TemporaryDirectory() as where:
    sources = os.path.join(tests, "bench", "languages")
    written = subprocess.run([kas3c, "cpp", "--godot", "--remember", "frames/ArrayConversion.as", "frames/Alloc.as", "-o", where], capture_output=True,
                             text=True, cwd=sources)
    check("the frame benchmarks' scripts are written as C++", written.returncode == 0, written.stderr)
    if written.returncode == 0:
        process = functions(where, "frames_ArrayConversion.cpp").get("frames_ArrayConversion__process", "")
        pushes = [loop for loop in loops(process) if "number_any(" in loop]
        check("the loop that pushes sums of two ints is found", len(pushes) >= 1, process)
        for loop in pushes:
            check("a sum of whole numbers handed on untyped is an int by integer arithmetic",
                  re.search(r"if \(const int64_t n = \(static_cast<int64_t>\(r\d+\) \+ static_cast<int64_t>\(r\d+\)\); KAS3_LIKELY\(n != 0 && ", loop), loop)

        particle = open(os.path.join(where, "frames_Particle.cpp")).read()
        check("a constructor built to tell keeps its arguments where the object is, and tells the host where it can't",
              re.search(r"if \(KAS3_UNLIKELY\(c->remembers\)\) \{ const Slot t\[\] = \{[^}]*\}; if \(!remembered_here\(c, r0, \d+, t, 5\)\) "
                        r"\{ constructing\(c, \d+, t, 5\); \} \}", particle), particle[-3000:])

for failure in failures:
    print("FAIL " + failure[:2500])
print("generated: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
