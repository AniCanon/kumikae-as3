"""AS3 inside Godot: runs the test project in headless Godot and checks what the script did.

Usage: run_godot.py <tests dir> [<kas3c>]

Needs the extension library (built when CMake is given -DKAS3_GODOT_CPP=<godot-cpp checkout>) and Godot 4.7
(`godot` on the PATH, or the KAS3_GODOT environment variable). Without either, the test is skipped and says
so: it does not fail.

With kas3c and a C++ compiler, scripts are also run as native code (native_godot.py): written as C++ by
`kas3c cpp --godot`, built into a program library and loaded by the extension. tests/godot_native's Probe.as
is held to what the interpreter prints for the same script, line for line, and the benchmark's workloads to
the fast paths they are meant to take.

The project is compiler/tests/godot_project: Player.as on a CharacterBody2D sets its velocity and calls
moveAndSlide() for 30 physics frames. At 200 units a second and 60 frames a second that is 100 units.
"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

import test_web_export
import native_godot
import play_godot

tests = os.path.abspath(sys.argv[1])
project = os.path.join(tests, "godot_project")
# Every build folder's run uses this one project (its .godot folder, the one extension library in its
# bin): two runs at once would each rewrite what the other is reading. The second waits for the first,
# on a lock that goes with the process however it ends.
try:
    import fcntl
    held = open(os.path.join(tempfile.gettempdir(), "kas3-godot-" + hashlib.sha256(project.encode()).hexdigest()[:16] + ".lock"), "w")
    fcntl.flock(held, fcntl.LOCK_EX)
except ImportError:
    held = None  # No such lock on this system: runs there are not made side by side.
godot = os.environ.get("KAS3_GODOT") or shutil.which("godot")
# Every run made here is the interpreter's, or native code by a library the test built: Play's own build of
# a project's library is off but where it is what is tested (play_godot.py).
os.environ[play_godot.SWITCH] = "0"


def skip(reason):
    print("godot: skipped (%s)" % reason)
    sys.exit(0)


if not glob.glob(os.path.join(project, "bin", "libkas3_godot.*")):
    skip("the extension library is not built: configure with -DKAS3_GODOT_CPP=<godot-cpp checkout>")
if not godot:
    skip("no godot on the PATH and KAS3_GODOT is not set")
version = subprocess.run([godot, "--version"], capture_output=True, text=True, timeout=60).stdout.strip()
if not version.startswith("4.7"):
    skip("Godot 4.7 is needed, found %s" % version)

failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append("%s %s" % (name, detail))


# Godot learns of a project's extensions when the editor first scans it. Told to quit at once (--import),
# Godot crashes as it exits after that first scan: its documentation thread for the new classes outlives
# the documentation (see tests/README.md). first_scan.gd ends when the scan has and that thread has
# been waited for, however long either takes: the first scan is then clean, and is checked so in a copy
# that has never been scanned.
FIRST_SCAN = ["--headless", "--editor", "-s", "first_scan.gd"]
with tempfile.TemporaryDirectory() as fresh:
    copy = os.path.join(fresh, "project")
    shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios", ".godot"))
    run = subprocess.run([godot, *FIRST_SCAN, "--path", copy], capture_output=True, text=True, timeout=300)
    check("the first editor scan exits cleanly", run.returncode == 0, "exit %d\n%s" % (run.returncode, (run.stdout + run.stderr)[-800:]))
    if run.returncode == 0:
        run = subprocess.run([godot, "--headless", "--editor", "--path", copy, "--quit-after", "600",
                              "-s", "script_editor.gd"], capture_output=True, text=True, timeout=300)
        check("the Script editor highlights, completes and saves ActionScript",
              run.returncode == 0 and "EDITOR passed" in run.stdout, (run.stdout + run.stderr)[-3600:])
        check("opening an ActionScript helper does not warn about attaching it",
              "ActionScript: res://Plain.as:" not in run.stderr, run.stderr[-1800:])
listed = os.path.join(project, ".godot", "extension_list.cfg")
if not os.path.exists(listed):
    subprocess.run([godot, *FIRST_SCAN, "--path", project], capture_output=True, text=True, timeout=300)
check("the extension is listed", os.path.exists(listed) and "kas3.gdextension" in open(listed).read())
# The extension keeps bindings of its own on Godot's objects, whose callbacks are its code
# (godot_ext/native_program.cpp, object_behind): Godot must never unload it while a game or the editor runs.
for described in glob.glob(os.path.join(os.path.dirname(os.path.dirname(tests)), "**", "kas3.gdextension"), recursive=True):
    if os.sep + "build" not in described and os.sep + ".godot" + os.sep not in described:
        check("the extension is not one Godot reloads: %s" % described, "reloadable" not in open(described).read())

run = subprocess.run([godot, "--headless", "--path", project, "-s", "driver.gd"], capture_output=True, text=True,
                     timeout=300)
output = run.stdout + run.stderr
moved = re.search(r"RESULT steps=(\d+) x=([-0-9.]+) y=([-0-9.]+) text=(.*)", output)
check("the script ran", moved is not None, output[-600:])
if moved:
    check("30 physics frames", moved.group(1) == "30", moved.group(1))
    # Godot's vectors are single precision: allow for it.
    check("moved 100 units", abs(float(moved.group(2)) - 100.0) < 0.01, moved.group(2))
    check("no sideways movement", abs(float(moved.group(3))) < 0.001, moved.group(3))
    check("a string came back", moved.group(4).strip() == "moved after 30 steps", moved.group(4))
check("the AS3 error was reported", "TypeError: Error #1009" in output and "at Player/fail()" in output, output[-600:])
check("the AS3 error is shown as Godot shows a script's own: with the script's file and line as its source",
      re.search(r"SCRIPT ERROR: TypeError: Error #1009[^\n]*\n(\s+at [^\n]+\n)*\s+at: Player/fail\(\) \(res://Player\.as:\d+\)", output) is not None,
      output[-900:])
check("the engine carried on", "RESULT after error steps=30" in output, output[-600:])
check("no crash", run.returncode == 0, "exit %d" % run.returncode)

# The faults the first review of this slice found.
run = subprocess.run([godot, "--headless", "--path", project, "-s", "checks.gd"], capture_output=True, text=True,
                     timeout=300)
output = run.stdout + run.stderr
lines = dict(line[len("CHECK "):].split("=", 1) for line in output.split("\n") if line.startswith("CHECK ") and "=" in line)
expected = {
    "script alive": "true",
    "derived steps": "300",
    "ready": "1",
    "two": "12.0",
    "too few": "<null>",
    "default": "8",
    "int": "42",
    "big int": "<null>",
    "whole float as int": "3",
    "fraction as int": "<null>",
    "negative uint": "<null>",
    "number from text": "<null>",
    "node from int": "<null>",
    "string from null": "<null>",
    "flag": "true",
    "counted references": "1 hello=hello",
    "counted freed": "true",
    "things": "a.Thing b.Thing",
    "vector": "3.0",
    "wrong vector": "<null>",
    "body": "true",
    "no body": "false",
    "wrong body": "<null>",
    "missing node": "false",
    "no instance": "false",
    "null script object": "<null>",
    "wrong node has script": "false",
    "plain can attach": "false",
    "returned script object": "true",
}
for name, value in expected.items():
    check("check: " + name, lines.get(name) == value, "got %r, expected %r" % (lines.get(name), value))
check("a call on a freed object is an AS3 error", "CHECK survived freed object" in output and "freed" in output, output[-800:])
check("says why a script won't attach to the wrong kind of object", "can only be attached to a CharacterBody2D" in output, output[-900:])
check("says why a plain class is not a script", "Plain.as" in output and "does not extend a Godot class" in output, output[-900:])
check("a script object that is one of Godot's is returned with no word", output.count("returns a script object") == 0, output[-900:])
check("checks: no crash", run.returncode == 0, "exit %d\n%s" % (run.returncode, output[-800:]))
check("checks: nothing leaked", "leaked" not in output.lower(), output[-600:])

def compare(script, names):
    """Runs a driver that prints `VALUE name=<from AS3>|<from GDScript>` and checks that the two agree."""
    run = subprocess.run([godot, "--headless", "--path", project, "-s", script], capture_output=True, text=True,
                         timeout=300)
    output = run.stdout + run.stderr
    seen = {}
    for line in output.split("\n"):
        if line.startswith("VALUE ") and "=" in line:
            name, both = line[len("VALUE "):].split("=", 1)
            seen[name] = both.split("|", 1)
    for name in names:
        pair = seen.get(name)
        check("%s: %s" % (script, name), pair is not None and len(pair) == 2 and pair[0] == pair[1],
              "got %r" % (pair,))
    check("%s: no crash" % script, run.returncode == 0, "exit %d\n%s" % (run.returncode, output[-800:]))
    check("%s: nothing leaked" % script, "leaked" not in output.lower(), output[-600:])
    return output


# Godot's values: operators, as GDScript works them out.
output = compare("values.gd", [
    "add", "a freed object arrives as null", "sub", "times", "times int", "number first", "divide", "negate", "cells", "cells by number", "cells over",
    "tint", "same", "not same", "differs", "same node", "not same node", "other node", "chain", "compound", "moved",
    "cells over zero", "after the error",
    "text", "text after", "cast", "own", "color text", "node text", "node joined", "null text", "null node", "null own",
    "freed text", "freed text after", "alive is not null", "alive is set", "alive counts", "freed is null", "freed is not set", "freed does not count",
    "not freed", "freed name", "null as a Godot class", "for each over packed arrays", "new of a script class", "what new made, seen from GDScript", "a script object returned to Godot", "vectors of vectors and of script objects to Godot", "searching vectors of Godot's objects and values", "a part of an element", "a whole Number index", "a fraction as an index", "NaN as an index",
    "a fraction as a part's index", "freed children", "freed as an argument",
    "set x", "add x", "bump x", "nested", "held part", "local part", "other part", "from itself",
    "first", "color part", "set second", "add element", "position element", "packed", "set packed",
    "packed out of range", "out of range",
    "clamp default", "clamp one", "clamp trailing null", "clamp both null", "clamp middle null", "clamp null variable", "looking",
    "looking null", "whole numbers", "to string", "to string in AS3", "no to string", "no to string in AS3",
    "negative cells", "cells plus", "cells compound", "plain order", "compound order", "mixed", "big hash", "decode",
    "anchors", "words", "action", "action known", "angled", "parts", "same twice", "zeroes", "copies",
])
check("a null that is not the last argument takes Godot's default, with no error", "at Values/clampMiddleNull()" not in output,
      output[-800:])
check("a member of a freed object is AS3's null error, and says the object was freed",
      all(re.search(r"TypeError: Error #1009[^\n]*Godot has freed the object\.\n\s+at Values/%s\(\)" % name, output) for name in
          ("keptName", "keptRename", "keptChildren", "keptSignal", "keptByName")), output[-1500:])
check("a fraction as an index is AS3's RangeError", "RangeError: Error #1125: The index 1.5 is out of range 2." in output and
      "The index NaN is out of range 2." in output and "The index 0.5 is out of range." in output, output[-1500:])
check("an index out of range is an AS3 error", "at Values/outOfRange()" in output and "at Values/packed()" in output, output[-800:])
traced = re.search(r"EXPECT TRACE (.*)", output)
check("trace shows Godot's text", traced is not None and ("\n" + traced.group(1) + "\n") in output, output[-800:])
check("toString() on a missing node is an AS3 error", "at Values/nullOwn()" in output, output[-800:])
check("dividing whole numbers by zero is an AS3 error", "at Values/cellsOver()" in output, output[-800:])

# Signals, the AS3 way.
output = compare("signals.gd", [
    "add", "has", "one listener", "still once", "emit", "remove", "add once", "remove all", "counts what scripts added", "remove all leaves others", "lasting or once, not both",
    "null argument and order", "arguments", "removes itself",
    "after a failing listener", "another object's signal", "by name", "by a wrong name", "listeners and emit by name", "a listener's error comes back out of emit", "a function written in a method sees its members", "user signal", "declared signal", "a signal's object", "declared signal listed", "filter",
    "valid while its owner lives", "gone with its owner", "deferred not yet", "deferred after the frame",
    "deferred with its owner freed", "own signal field by name", "by name from a base: its own private field",
    "by name from a subclass: its own private field", "by name from a subclass: a base's private field is not reached", "by name in a function written in the base: its private field",
    "by name from a base: a subclass's public field first", "by name from outside: a public signal field",
    "by name from outside, the object typed as a Node: a public signal field",
    "by name from a subclass: a base's protected field", "by name from outside: a private or protected field is not reached", "a loop lives until the frame ends", "a loop is freed", "a longer loop is freed",
    "a removed listener that is still held keeps what it holds",
    "a loop with no listener is freed", "a loop through a script's Godot object is freed", "a loop through an untyped field is freed",
    "a loop whose listener is in two signals is freed",
    "a loop whose callable is held stays", "a held listener keeps its place", "a loop goes when its callable is let go",
    "an unheld listener stays",
])
check("a listener given the wrong kind of argument is an AS3 error that names it",
      "TypeError: Error #1034" in output and "Signals/onHit" in output, output[-1200:])
check("a listener given too many arguments is an AS3 error that names it",
      "ArgumentError: Error #1063: Argument count mismatch on Signals/onHit(). Expected 1, got 2." in output, output[-1200:])
check("a listener's argument error says where the listener is", re.search(r"Error #1063[^\n]*\n\s*at Signals/onHit\(\)\[.*Signals\.as:\d+\]", output) is not None, output[-1200:])
check("Godot is told that a listener given arguments that don't fit was not called",
      output.count("Error calling from signal 'hit' to callable") == 2, output[-1500:])
check("a field that is not a signal, by name, is an AS3 error", "'count' is not a signal" in output, output[-1200:])
check("an error inside a listener is reported", "at Signals/fails()" in output, output[-800:])
check("a wrong signal name is an AS3 error", output.count("at Signals/listenByName()") == 2, output[-800:])
check("a private field reached by name from where it can't be seen is an AS3 error",
      output.count("at Signals/listenOther()") == 2 and "at SignalDerived/listenFromDerived()" in output, output[-800:])
check("add after addOnce, and the reverse, are AS3 errors", "at Signals/mixed()" in output and "at Signals/mixedBack()" in output,
      output[-800:])
check("an empty signal says so", "the signal is empty" in output and "at Signals/empty()" in output, output[-800:])

# What a signal carries from AS3's emit() to its listeners and from Godot to a listener written in AS3
# (Emits.as): the rows are emits_cases.gd's, which the native probe runs too.
output = compare("emits.gd", [
    "emit: an int, a Number, a Boolean and a Vector2", "emit: a uint, an Int64 and a sum", "emit: Godot's values with parts",
    "emit: values made where they are emitted, in a loop", "emit: a String, a node, an untyped value and no Vector2",
    "emit: nothing, and six values", "emit: one of Godot's own signals", "emit: another object's signal",
    "emit: one place, whichever signal it is handed, and a signal of no object", "a listener's int and Vector2",
    "a listener is not given what does not fit", "a listener's uint, Number, Boolean and Int64",
    "such a listener is not given what does not fit", "a listener's String, node, untyped value and Vector2",
    "a listener with a default, with the rest, a static one and a function", "the same, given less",
    "a listener that changes what it was given changes its own", "a listener that keeps what it was given keeps its own",
    "a listener is not given what fits only in part", "AS3 emits to AS3", "AS3 emits what its own listener does not take",
    "a listener's error comes out of the emit", "a listener's error nothing catches ends the emitting function",
    "a listener that emits what it listens to", "a listener for once only", "emit: a signal kept in a field",
    "emit: a signal of an object freed since", "emit: no signal at all", "a listener freed by the one before it is not called",
    "a listener that changes the field the signal was emitted through",
    "emit: the signal a field held before a function called for what is emitted changed the field",
    "a listener called later has heard nothing yet",
    "a listener called later hears what was emitted",
    "a parameter of the script's class takes the object that carries the script, one of a class that extends it, and null",
    "an object that has been freed is null there", "an object without the script, or with another, is refused",
    "a field of the script's class set from Godot", "as and is on a node", "as and is on an untyped value",
    "a cast to the script's class on a node", "a cast of an untyped value, to the script's class and to Godot's", "a cast of the script's own object",
    "such a field is not set to an object without the script", "and is set to one of a class that extends it", "and to null",
    "an untyped value stored where the script's class is declared", "what an engine call gives, as the script's class",
    "a method of the class that extends it", "a listener's parameter of the script's class",
    "such a listener is not given an object without the script",
])
for refused in ("argument 1 of Emits/onStruck(), a String, to int", "argument 2 of Emits/onStruck(), a Vector3, to Vector2",
                "argument 1 of Emits/onStruck(), a float, to int", "argument 2 of Emits/onStruck(), a Object, to Vector2",
                "argument 2 of Emits/onWide(), a String, to Number", "argument 3 of Emits/onWide(), a int, to Boolean",
                "argument 4 of Emits/onWide(), a float, to Int64", "argument 2 of Emits/onTurned(), a String, to int",
                "argument 1 of Emits/onMate(), a Object, to Emits", "argument 1 of Emits/onMate(), a String, to Emits"):
    check("emits.gd: a listener given the wrong kind of value is AS3's TypeError, which names it: " + refused,
          "TypeError: Error #1034: Type Coercion failed: cannot convert " + refused + "." in output, output[-1500:])
check("emits.gd: a whole number too wide for a listener's int or uint is AS3's RangeError",
      "RangeError: 5000000000 does not fit an int.\n\tat Emits/onStruck()" in output and "RangeError: -1 does not fit a uint.\n\tat Emits/onWide()" in output,
      output[-1500:])
# From GDScript's emit (too few, too many), from one given less than three of the four listeners take, and
# from AS3's own emit of one value.
check("emits.gd: a listener given too few or too many values is AS3's ArgumentError",
      output.count("ArgumentError: Error #1063: Argument count mismatch on Emits/onStruck(). Expected 2, got 1.") == 2 and
      "Argument count mismatch on Emits/onStruck(). Expected 2, got 3." in output and
      "Argument count mismatch on Emits/onStatic(). Expected 2, got 1." in output, output[-1500:])
check("emits.gd: what AS3 emits that its own listener does not take is refused in the same words",
      output.count("argument 1 of Emits/onStruck(), a String, to int") == 2, output[-1500:])
check("emits.gd: an error in a listener that nothing catches is reported with the function that emitted",
      re.search(r"SCRIPT ERROR: Error: in the listener at 3\n\s+at Emits/onFail\(\)\[res://Emits\.as:\d+\]\n\s+at Emits/strike\(\)\[res://Emits\.as:\d+\]", output)
      is not None, output[-1500:])

# Godot closing. As the scene tree is taken down, AS3's listeners still run and its static fields still
# hold their values; and with a main loop that is not a scene tree, an object a static field holds, with a
# GDScript listener on it, is let go while Godot can still free it.
run = subprocess.run([godot, "--headless", "--path", project, "-s", "exit_tree.gd"], capture_output=True, text=True, timeout=300)
output = run.stdout + run.stderr
check("exit_tree.gd: a listener runs as the tree goes, and sees the static field", "LATE static=kept" in output, output[-800:])
check("exit_tree.gd: no crash", run.returncode == 0 and "leaked" not in output.lower(), "exit %d\n%s" % (run.returncode, output[-800:]))
run = subprocess.run([godot, "--headless", "--path", project, "-s", "exit_loop.gd"], capture_output=True, text=True, timeout=300)
output = run.stdout + run.stderr
check("exit_loop.gd: no crash at exit", run.returncode == 0 and "EXIT LOOP ready" in output and "leaked" not in output.lower(),
      "exit %d\n%s" % (run.returncode, output[-800:]))

# Singletons, constants and static methods, as GDScript works them out.
output = compare("statics.gd", [
    "pressed", "strength", "stick", "system", "an editor-only singleton is an AS3 error", "a class's static method",
    "orphan nodes printed", "a static call's arguments in order", "the value before the missing object", "a static call's result of the wrong type",
    "a singleton's members through a variable", "a singleton variable that is null", "slowed", "time scale", "faster", "faster in Godot", "at start", "mouse",
    "listening", "heard", "cleared", "others still listen", "not listening", "a function written in place listens", "and is removed through its value", "input class", "same singleton",
    "singleton object",
    "a singleton's listeners counted by their own script", "removeAll() on a singleton's signal takes the caller's own",
    "and leaves the others' counted", "from another class, it takes that class's own", "from static code, its class's static listeners",
    "from a function expression, which has no object, the class's static listeners", "nothing is left listening",
    "escape", "captured", "ready notice", "axis", "zero", "up", "red", "forward", "identity", "sum", "up y",
    "default key", "given key",
    "from angle", "hsv", "html", "euler", "image", "after orphans",
    "unset", "unset tint", "unset spot", "unset list", "cleared local",
    "declared twice", "declared twice, value first", "declared twice, other order", "declaration skipped",
    "field read early", "static read early", "race", "a field from another thread is refused",
])
check("statics.gd: a field asked for from another thread is refused and says so",
      output.count("Godot asked for the field 'shared' from another thread") == 2, output[-900:])
check("statics.gd: an error's source is where it was thrown, whatever its message says",
      re.search(r"at: Statics/fakeTrace\(\) \(res://Statics\.as:\d+\)", output) is not None and "at: fake()" not in output, output[-1500:])
check("statics.gd: Godot says nothing of an editor-only singleton", "Can't retrieve singleton" not in output, output[-900:])
check("statics.gd: a call from a worker thread is refused and says so",
      "Statics/spin() was called by Godot from another thread" in output, output[-900:])
check("statics.gd: no AS3 error", "Error #" not in output and "does not run" not in output and "Godot has no" not in output,
      output[-900:])

# A script reached from Godot's worker threads: what would run AS3 there is refused, and said so; what Godot
# frees there is freed on the main thread.
output = compare("threads.gd", [
    "toString on the main thread", "toString from a worker is refused: Godot's own text",
    "attached on a worker is refused: no script, no constructor run", "lists asked for on workers and on the main thread at once",
    "main thread warms a by-name site", "worker cannot enter a warm by-name site", "scripts loaded on workers at once",
    "freed on a worker: nothing of AS3's freed there",
    "freed on a worker: freed on the main thread",
])
check("threads.gd: toString from a worker is refused and says so",
      "Threaded/toString() was called by Godot from another thread" in output, output[-900:])
check("threads.gd: a script attached on a worker is refused and says so",
      "res://Threaded.as was not attached to a RefCounted: Godot attached it from another thread" in output, output[-900:])

# The names Godot asks a script about: only those that reach something are kept, and none is left at exit
# (Godot lists a name still held then, with --verbose).
compare("names.gd", ["names that reach nothing are not kept", "a field is still reached"])
run = subprocess.run([godot, "--headless", "--verbose", "--path", project, "-s", "names.gd"], capture_output=True, text=True,
                     timeout=300)
left = re.findall(r"Orphan StringName: (\S+)", run.stdout + run.stderr)
check("names.gd: no name the script was asked about is left at exit",
      not [name for name in left if name in ("n", "hello", "heard") or "_unreached_" in name], left[:20])


# Int64 at Godot's edge, as GDScript works it out.
output = compare("wide.gd", [
    "counted id", "node id", "same id", "rgba64", "rgba32", "count", "frames", "element", "negative element", "plus one",
    "stored", "narrow fits", "narrow refused", "echo most", "echo least", "wraps", "whole float", "fraction refused",
    "text refused", "int refused", "number exact", "number refused", "to number", "to number refused", "field", "flags",
    "product", "heard", "product past int", "a whole NaN goes as 0", "cell", "cell refused", "cell3 refused", "box", "box refused", "over", "over refused",
    "remainder", "remainder refused", "times", "times refused", "times left refused", "grow refused", "at least one",
    "at least its size",
])
check("wide.gd: a wide value for a 32-bit array is a RangeError before Godot sees it",
      "RangeError: 5000000000 does not fit an int." in output and "at Wide/narrow()" in output, output[-1200:])
check("wide.gd: a wide value for an integer vector is a RangeError, eight times",
      output.count("RangeError: 4294967299 does not fit an int.") == 8 and "RangeError: 3000000000 does not fit an int." in output,
      output[-1500:])
check("wide.gd: an int argument too wide is AS3's RangeError, not Godot's word",
      "RangeError: 5000000000 does not fit an int.\n\tat Wide/asInt()" in output and "to int" not in output, output[-1500:])
check("wide.gd: an Int64 past what a Number holds is a RangeError", "RangeError: 9007199254740993 does not fit a Number exactly." in output,
      output[-1200:])

# Godot's global functions, as GDScript works them out.
output = compare("functions.gd", [
    "lerpf", "rest from Godot", "arguments from Godot", "clampf", "clampi", "absi", "maxi", "sin", "snappedf", "wrapi", "posmod", "is_equal_approx", "randf",
    "randi_range", "floori and roundi", "hash", "str", "is_instance_valid", "instance_from_id", "instance_from_id counted",
    "lerp as a number", "max of three", "wrong argument", "in a loop", "no arguments", "whole numbers", "no answer",
    "anything as text", "a default is not shared",
    "max past what a Number holds", "lerp of vectors", "int of lerp cuts", "Boolean of max", "lerp and max as text",
    "lerp of anything", "lerp into the wrong type", "now", "zone", "a regular expression",
    "a freed object as text", "a freed object to hash and isSame", "str of a freed object and of null",
    "a freed object inside a collection", "a script object of no Godot class is not written as null", "a whole Number stays a float",
    "a freed object in locale text",
])
check("functions.gd: trace shows a freed object as null", "traced: null" in output, output[-1200:])
check("functions.gd: print shows a freed object and a null as null", "freed: null null" in output, output[-1200:])
check("functions.gd: a wide whole number into a Number is a RangeError",
      "RangeError: 9007199254740993 does not fit a Number exactly.\n\tat Functions/widest()" in output, output[-1500:])

check("functions.gd: a function that could not answer is an AS3 error", "at Functions/noAnswer()" in output and "'max'" in output,
      output[-1200:])
check("functions.gd: print, printerr and pushWarning reach Godot's output",
      "said 4 (1.0, 2.0)" in output and "to the error output" in output and "a warning from AS3" in output, output[-1200:])
check("functions.gd: a wrong argument is an AS3 error that names the function", "at Functions/wrong()" in output and "lerp" in output,
      output[-1200:])

# Godot's methods as values, as GDScript's own method references behave.
output = compare("callables.gd", [
    "a listener that is one of Godot's methods", "held, called and then given to Godot", "passed where a Function is declared",
    "the same method of the same object", "Godot's own Callable is what the signal holds",
    "arguments are counted and converted", "call and apply", "a typed callback", "a method Godot's collection calls",
    "statics, a global function and a singleton", "a singleton's method as a listener", "a method of a value",
    "called after its object is freed", "added after its object is freed",
])
check("callables.gd: nothing is reported", "SCRIPT ERROR" not in output and "ERROR:" not in output, output[-1500:])

# Godot's classes as class values, as GDScript's own classes held in variables behave.
output = compare("kinds.gd", [
    "new, is and as with a class value", "a script object is its Godot object", "a value type as a class value", "a reference-counted class",
    "a cast of a node", "a cast of null", "a cast of a number fails", "a cast of a value fails as it does where the class is named", "the same class",
    "a class the engine won't create",
])
check("kinds.gd: a cast that fails is AS3's coercion error", "Error #1034: Type Coercion failed: cannot convert 5 to Node2D." in output and
      "at Kinds/cast()" in output, output[-1500:])

# Godot's values and objects held untyped, used by name, as GDScript's untyped variables are.
output = compare("by_name.gd", [
    "properties read by name", "properties written by name", "methods called by name", "a name that is not there", "in and hasOwnProperty",
    "a value's parts and methods", "a node that carries a script", "the script's own object", "a method read by name", "a signal reached by name",
    "exact untyped arguments", "fresh timer after attaching a script", "foreign declarations", "foreign declaration after replacement", "foreign declaration after in-place reload", "typed reload after staging source", "foreign script accessors", "same site on alternating classes and scripts", "replacement on the same receiver",
    "computed misses", "null math receivers", "parameter alias keeps foreign accessors", "parameter alias respects position overrides", "freed after warming the site", "a freed object",
])
check("by_name.gd: nothing is reported", "SCRIPT ERROR" not in output, output[-1500:])

# Every default the name table records must be one Godot can read back.
run = subprocess.run([godot, "--headless", "--path", project, "-s", "defaults.gd", "--",
                      os.path.join(tests, "..", "godot", "godot_names.txt")], capture_output=True, text=True, timeout=300)
read = re.search(r"DEFAULTS read=(\d+) failed=(.*)", run.stdout)
check("Godot reads every recorded default", read is not None and int(read.group(1)) > 10 and read.group(2) == "[]",
      (run.stdout + run.stderr)[-600:])

# Untyped values to and from Godot: an integer stays one, a whole float stays a float.
output = compare("untyped.gd", [
    "an int", "a float", "a whole float", "a string", "a string name", "a node path", "a bool", "null", "a vector", "a node",
    "round trip int", "round trip float", "round trip String", "round trip Vector2", "round trip Object", "round trip Nil",
    "round trip bool", "meta keeps an int", "meta keeps a float", "dictionary element", "dictionary number", "wrong type",
    "script object not given", "equal values", "unequal values", "large integers", "a vector stays a vector",
    "a color is not a vector", "a vector3 is not a vector2", "a resource is not a node", "a node is a node",
    "a property takes its type only", "a script object returned", "a script object in a field", "freed", "caught in AS3",
    "a toString() that throws, caught where the text was asked for",
    "an int plus one stays an int", "a Godot value and an untyped one",
    "a value read above its line is its zero", "an untyped node is a Node", "an untyped value as a Node", "the script's own object held untyped is its node", "a part is written to the object named before the value", "the other object is untouched",
    "minusOne of an int stays an int", "twice of an int stays an int", "stepped of an int stays an int",
    "an Int64 minus one stays whole", "a half is a float", "a Godot value plus text joins them",
    "text plus a Godot value joins them", "text plus-assigned a Godot value joins them",
    "a Godot value plus a number joins them, as AS3 does", "a Godot value plus-assigned a Godot value",
    "untyped Godot values multiplied, subtracted, divided and negated", "untyped Godot values in order",
    "an untyped Godot value multiplied and subtracted in place", "a typed Godot value times an untyped one",
    "untyped numbers stay AS3's", "where Godot has no such operator the answer is AS3's",
    "JSON refuses Godot's values", "is with Godot's value types", "as with Godot's value types", "a cast to one of Godot's value types",
    "is, as and a cast on typed values",
])
check("untyped.gd: a value of another Godot type is AS3's coercion error, where it is stored",
      output.count("Error #1034") >= 5 and "at Untyped/asVector()" in output and "at Untyped/asNode()" in output and
      "at Untyped/place()" in output, output[-2000:])
check("untyped.gd: a script object reaching Godot is said", "giveScript returns a script object" in output and
      "'keep' holds an object of 'Thing'" in output, output[-2000:])
check("untyped.gd: a value of the wrong type is AS3's coercion error", "at Untyped/wrongType()" in output and "Error #1034" in output,
      output[-1500:])
check("untyped.gd: a thrown Godot value nothing catches is reported as its text", "(3.0, 4.0)\n\tat Untyped/throwValue()" in output,
      output[-2000:])
check("untyped.gd: a script object given to Godot is an AS3 error", "at Untyped/scriptObject()" in output, output[-1500:])
check("untyped.gd: a cast to one of Godot's value types of a value that is not one is AS3's coercion error",
      "Error #1034: Type Coercion failed: cannot convert text to Vector2." in output and output.count("\tat Untyped/castValue()") == 2, output[-2500:])

# AS3's Objects and Dictionaries to Godot as its Dictionaries (keys as text), and a Dictionary of Godot's
# copied where an AS3 Dictionary is wanted.
output = compare("objects.gd", [
    "an Object is a Dictionary", "a Dictionary is a Dictionary, its keys text", "Objects inside an Array", "an Object returned",
    "an Object where Godot takes a Dictionary", "an untyped Object where Godot takes a Dictionary", "a Dictionary from Godot", "Godot's Dictionary as a Dictionary",
    "a loop over Godot's Dictionary held untyped is not run", "an Object that holds itself is not given", "an Array with properties is not given", "a script object inside an Object is not given",
])
check("objects.gd: what Godot can't hold is an AS3 error that says why",
      "holds itself, or nests too deep" in output and "at Objects/holdsItself()" in output and
      "an Array with properties other than its indices" in output and "at Objects/plainInside()" in output and
      "'for in' and 'for each' over one of Godot's values held untyped" in output, output[-2000:])

# Loops over Godot's own collections, and `in` and `delete` on its Dictionary, as GDScript goes through them.
output = compare("loops.gd", [
    "for each over what a method gives", "for each into a Number", "for each into an untyped variable", "for in gives an Array's indices",
    "for in gives a Dictionary's keys", "for in into a variable declared above", "for each gives a Dictionary's values", "break and continue",
    "an element added on the way is reached", "a key erased on the way is passed over, one added is not reached", "a loop inside a loop",
    "for in over a packed array", "in on a Dictionary", "delete on a Dictionary", "delete by a name", "an element of another type",
    "for each into the script's own class", "a node without the script is not of its class", "for each into one of Godot's values",
    "no collection is nothing to go through",
])
check("loops.gd: a node without the script, where the loop's variable is the script's class, is AS3's coercion error",
      "to Loops." in output and "at Loops/scripted()" in output, output[-1500:])
check("loops.gd: an element that is not what the loop's variable is declared is AS3's coercion error",
      "Error #1034: Type Coercion failed: cannot convert 5 to Node." in output and "at Loops/wrongElement()" in output, output[-1500:])

# What GDScript reaches of a script by name: public fields to read and write, public methods, and the lists.
compare("members.gd", [
    "fields set", "fields read back", "ratio is a float", "refused", "a whole float into an int", "an array is copied",
    "an array set to null", "an array refuses what is not one", "a dictionary is copied", "a dictionary set to null",
    "a dictionary refuses what is not one", "an object field", "a date field is not set from Godot", "a setter and its getter",
    "accessors and containers", "a node field", "a node field refuses another class",
    "a node field set to null", "a const", "a script object field is not set to an object without the script",
    "a script object field is set to the object that carries the script", "hidden fields read",
    "hidden fields are not written", "hidden methods", "public methods",
    "the script's properties", "the instance's properties", "their types", "a node's class", "not shown or stored",
    "untyped is any value", "the script's methods", "a method's arguments", "the instance's methods", "has_method agrees",
])

# [Export]: a scene's values reach the script; Godot is told each field's first value; a scene saved again
# keeps what differs from it. In the editor, no AS3 runs and the object holds the exported values.
compare("exported.gd", [
    "the scene's values", "shown in the inspector", "stored and shown", "a resource's hint", "first values",
    "one of Godot's values made from numbers", "a field that is not exported has none", "a node's hint", "saved again",
    "a resource and a node let go", "a subclass's exports", "a subclass's methods",
    "saved after a change",
])
run = subprocess.run([godot, "--headless", "--editor", "--path", project, "-s", "exported_editor.gd"], capture_output=True, text=True,
                     timeout=300)
output = run.stdout + run.stderr
seen = dict(line[len("VALUE "):].split("=", 1) for line in output.split("\n") if line.startswith("VALUE ") and "=" in line)
for name in ["in the editor", "no AS3 ran", "first values", "a value the inspector set", "what the editor saves",
             "a scene opened in the editor"]:
    pair = seen.get(name, "").split("|", 1)
    check("exported_editor.gd: " + name, len(pair) == 2 and pair[0] == pair[1], "got %r" % seen.get(name))
check("exported_editor.gd: no crash", run.returncode == 0, "exit %d\n%s" % (run.returncode, output[-800:]))
# A field exported by an edit shows in the editor without a restart. In a copy: the test rewrites a script.
with tempfile.TemporaryDirectory() as edited:
    copy = os.path.join(edited, "project")
    shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios"))
    run = subprocess.run([godot, "--headless", "--editor", "--path", copy, "-s", "exported_reload.gd"], capture_output=True, text=True,
                         timeout=300)
    output = run.stdout + run.stderr
    values = re.findall(r"VALUE (.+?)=(.*)\|(.*)", output)
    check("exported_reload.gd ran to its end", run.returncode == 0 and len(values) == 2, output[-1500:])
    for name, got, want in values:
        check("exported_reload.gd: %s" % name, got == want, "got %r, expected %r" % (got, want))

# A script object of a reference-counted class lives as long as AS3 or Godot holds it, and no longer.
compare("kept.gd", [
    "made", "alive while AS3 holds them", "still alive when Godot lets go", "and AS3 still sees them", "alive while Godot holds it",
    "freed when both let go", "freed when AS3 lets go", "given to Godot, alive while Godot holds them", "freed when Godot lets go",
    "made and forgotten in AS3", "a resource that listens to itself goes", "a listener on another node goes",
    "attached, kept while AS3 holds it", "attached, freed when AS3 lets go too",
    "handed over by keep, kept while AS3 holds it", "handed over by keep, freed when AS3 lets go",
    "handed over by keepAs, kept while AS3 holds it", "handed over by keepAs, freed when AS3 lets go",
    "handed over by counted, kept while AS3 holds it", "handed over by counted, freed when AS3 lets go",
    "detached, AS3's hold given back", "detached, freed when Godot lets go", "detached and freed, to AS3 and Godot it is a freed object",
    "detached by itself, freed once nothing holds it",
])
# The cycle collector with such objects in loops: what nothing holds goes; what GDScript or AS3 holds stays
# whole, its fields with it.
compare("cycles.gd", [
    "a getter read", "a loop held by nobody goes", "a loop through Godot's count goes", "a loop GDScript holds stays whole",
    "a loop an AS3 static holds stays whole", "kept by Godot alone stays whole", "what a getter kept outlives GDScript's hold",
    "what a setter kept outlives GDScript's hold", "a static's loop goes when the static lets go", "what a getter kept goes when AS3 lets go",
    "what a setter kept goes when AS3 lets go", "a loop GDScript held goes when it lets go",
    "a function listening to its own object's signal goes with it", "and stays, still listening, while GDScript holds the object",
    "so does the listening one", "a script whose base is Object goes on a RefCounted",
    "on a RefCounted, a loop through Godot's count held by nobody goes", "on a RefCounted, a loop held by nobody goes",
    "on a RefCounted, a loop GDScript holds stays whole", "on a RefCounted, what an AS3 static holds stays whole",
    "on a RefCounted, what an AS3 static holds keeps its Godot object", "on a RefCounted, a loop GDScript held goes when it lets go",
    "on a RefCounted, what a static held goes when the static lets go", "on an Object, a loop GDScript holds stays whole",
    "on an Object, what an AS3 static holds stays whole when Godot frees the Object",
])

def timeout_test(label, native=False):
    """A script stuck in a loop is stopped with Flash's error, after the limit the project's settings give. With
    `native`, the scripts run as native code, by the library Play built in a run before: they are stopped
    the same way, and the same is said."""
    with tempfile.TemporaryDirectory() as limited:
        copy = os.path.join(limited, "project")
        shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios", "kas3"))
        with open(os.path.join(copy, "project.godot"), "a") as settings:
            settings.write("\n[actionscript]\n\nlimits/script_timeout_seconds=0.5\n")
        environment = dict(os.environ, **{play_godot.SWITCH: "1" if native else "0"})
        if native:
            first = subprocess.run([godot, "--headless", "--path", copy, "--quit-after", "5"], capture_output=True, text=True, timeout=300, env=environment)
            check("%s: a run before builds the library" % label, play_godot.BUILDS in first.stdout + first.stderr and play_godot.settled(copy) and
                  len(play_godot.libraries(copy)) == 1, (first.stdout + first.stderr)[-900:])
        run = subprocess.run([godot, "--headless", "--path", copy, "-s", "timeout.gd"], capture_output=True, text=True, timeout=300, env=environment)
        output = run.stdout + run.stderr
        values = re.findall(r"VALUE (.+?)=(.*)\|(.*)", output)
        check("%s ran to its end" % label, run.returncode == 0 and len(values) == 6, output[-1500:])
        check("%s: the scripts run %s" % (label, "as native code" if native else "on the interpreter"), (play_godot.USED in output) == native, output[:900])
        for name, got, want in values:
            check("%s: %s" % (label, name), got == want, "got %r, expected %r" % (got, want))
        check("%s: Flash's error, where the loop is" % label, output.count(
            "Error #1502: A script has executed for longer than the timeout period of 0.5 seconds.") == 3 and
            "at Spin/forever()" in output and "at Spin/caught()" in output and "at Spin/spin()" in output, output[-1500:])
        play_godot.settled(copy)


timeout_test("timeout.gd")
timeout_test("timeout.gd, as native code", native=True)

# Godot's own classes with `is` and `as`, and input events as a script receives them.
compare("events.gd", [
    "kind of InputEventScreenTouch", "kind of InputEventScreenDrag", "kind of InputEventMouseButton", "kind of InputEventKey",
    "touch position", "drag position", "neither", "is Node", "as Sprite2D", "null", "the script's own object", "delivered",
])

# A script's getters and setters, which GDScript reaches as its properties.
output = compare("accessors.gd", [
    "getter", "setter", "setter ran once", "get by name", "set by name", "getter alone", "no setter, no change",
    "setter alone", "setter alone reads as nothing", "a Godot value", "a value of the wrong kind is refused", "compound",
    "through an interface", "an accessor is no method by its AS3 name", "a getter's error reads as nothing", "a native property still", "a native property set",
])
check("a getter's error is reported as a script's own", re.search(r"SCRIPT ERROR: Error: broken getter[^\n]*\n(\s+at [^\n]+\n)*\s+at: Accessors/get broken\(\)", output) is not None, output[-900:])

# AS3's vectors handed to Godot: packed arrays by the element's type, arrays for the rest.
output = compare("vectors.gd", [
    "points", "points type", "hull", "bytes", "empty", "too big for a byte", "below a byte", "offsets", "colors", "filters",
    "polygons", "wholes", "unsigned", "an unsigned too big for 32 bits", "whole", "whole are integers", "fractions", "fractions are floats", "words", "flags", "nodes", "wide",
    "keep", "spread", "moved", "words of values", "forget", "array to Godot", "array from Godot", "Godot's array as an Array", "made a node", "the node is in the tree", "made a resource", "no vector", "no vector where Godot takes points",
    "added to while it is shortened", "what it let go of is freed, what was added is held", "and let go of in the end",
])
check("vectors.gd: no vector where Godot takes a packed array is an AS3 error", "at Vectors/noPoints()" in output, output[-1500:])
check("vectors.gd: a uint that does not fit 32 bits is an AS3 error that says so",
      "3000000000 does not fit a 32-bit integer" in output and "at Vectors/unsignedTooBig()" in output, output[-1500:])
check("vectors.gd: a value that does not fit a byte is an AS3 error that says so",
      output.count("does not fit a byte") == 2 and "at Vectors/tooBig()" in output and "at Vectors/negative()" in output, output[-1500:])

# A vector of Vector2s or of Vector2is holds each value in its place: never null, a copy read out and a copy
# stored, the places themselves handed to Godot as a packed array's elements.
compare("places.gd", [
    "grown", "no value is refused", "methods", "ends", "a part of an element", "given: counts", "given: a packed array", "given: an Array of values", "given: inside an Array",
    "given: none", "given: whole numbers", "a larger value type", "a larger value type: to Godot", "taken",
])

# A scene edited while it runs: no edit starts the scene again by itself. An object whose constructor depends
# only on its arguments and constants is updated in place; what could not be is said, and a restart that is
# asked for (the language's restart_scene()) shows it.
RESTART = """package { import godot.*; public class Restart extends Node2D {%s
	private var word:String;
	private var token:Token = new Token(100);
	private var stamp:Stamp;
	private var last:Pair;
	private var worded:Worded;
	private var spare:Number = 0;
	override public function _ready():void { word = "%s"; token.y = 7; churn(); }
	private function churn():void {
		for (var i:int = 0; i < 64; i++) { var pair:Pair = new Pair(1000 + i, 2); spare += pair.b; }
		last = pair;
		pair = null;
		for (i = 5; i < 8; i++) { worded = new Worded(i); }
		stamp = new Stamp(3, 4);
	}
	public function stamped():String { return stamp.a + "/" + stamp.b; }
	public function paired():String { return last.a + "/" + last.b; }
	public function wordedNow():String { return worded.label + worded.w; }
	public function said():String { return word%s; }
	public function limit():String { return LIMIT; }
	public function firstNow():String { return first; }
	public function width():Number { return token.w; }
	public function moved():Number { return token.y; }
} }
class Token {
	public var w:Number;
	public var y:Number;
	public function Token(p_y:Number) { w = Restart.WIDTH; y = p_y - Restart.WIDTH; }
}
class Pair {
	public var a:Number;
	public var b:Number;
	public function Pair(p_a:Number, p_b:Number) { a = p_a; b = p_b * Restart.WIDTH; }
}
class Worded {
	public var w:Number;
	public var label:String = "n";
	public function Worded(p_w:Number) { w = p_w * Restart.WIDTH; }
}
class Stamp {
	private static var made:int = 0;
	public var a:Number;
	public var b:Number;
	public function Stamp(p_a:Number, p_b:Number) { a = p_a; b = p_b + Restart.WIDTH - Restart.WIDTH;%s }
}
"""


def restart_test(label, native=False):
    """Runs restart.gd in a copy of the project. With `native`, the game starts as native code, by the library
    Play built in a run before, which tells what its objects are made with: the first edit moves it onto the
    interpreter, and the object that was made before it, as native code, is updated in place like any
    other. There the first edit changes the constant too, so that the object's first values are what the
    native constructor gives for its arguments, run again; the edit after changes it once more."""
    with tempfile.TemporaryDirectory() as edited:
        copy = os.path.join(edited, "project")
        shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios", "kas3"))
        # The second version also adds a field before the one `_ready` sets: nothing that ran changed. The third
        # edits `_ready`, a static variable's first value and two constants: the constants have their new values
        # at once and the Token made from one is updated in place, the variable keeps the one it was given, and
        # the scene, whose `_ready` can't be run again, is left as it is until a restart is asked for. The fourth
        # and fifth are saved one after the other, the fifth while the scene a restart asked for after the
        # fourth is being made: its constructors have run, its `_ready` not.
        unused = "\n\tpublic var unused:int;"
        first = "\n\tpublic static var first:String = \"%s\";\n\tpublic static const LIMIT:String = \"%s\";\n\tpublic static const WIDTH:Number = %s;"
        made = "\n\tpublic var made:String = \"%s\";"
        early = 26 if native else 24  # The width as the first edit has it.
        # Stamp's constructor counts in a static variable as the game starts, so it can't be run again; every
        # edit takes that away.
        for name, text in {"Restart.as": RESTART % (first % ("a", "a", 24), "one", "", " made++;"),
                           "restart_body.txt": RESTART % (unused + first % ("a", "a", early), "one", ' + "!"', ""),
                           "restart_ready.txt": RESTART % (unused + first % ("b", "b", 30), "three", ' + "!"', ""),
                           "restart_four.txt": RESTART % (unused + first % ("b", "b", 30) + made % "four", "four", ' + "!" + made', ""),
                           "restart_five.txt": RESTART % (unused + first % ("b", "b", 30) + made % "five", "five", ' + "!" + made', "")}.items():
            with open(os.path.join(copy, name), "w") as file:
                file.write(text)
        environment = dict(os.environ)
        if native:
            environment[play_godot.SWITCH] = "1"
            first = subprocess.run([godot, "--headless", "--path", copy, "--quit-after", "5"], capture_output=True, text=True, timeout=300, env=environment)
            check("%s: a run before builds the library" % label, play_godot.BUILDS in first.stdout + first.stderr and play_godot.settled(copy) and
                  len(play_godot.libraries(copy)) == 1, (first.stdout + first.stderr)[-900:])
        run = subprocess.run([godot, "--headless", "--path", copy, "-s", "restart.gd"], capture_output=True, text=True, timeout=300, env=environment)
        output = run.stdout + run.stderr
        values = re.findall(r"VALUE (.+?)=(.*)\|(.*)", output)
        check("%s ran to its end" % label, run.returncode == 0 and len(values) == 18, output[-1500:])
        check("%s: the game %s" % (label, "starts as native code" if native else "runs on the interpreter"), (play_godot.USED in output) == native, output[:900])
        for name, got, want in values:
            # The width after the first edit is the driver's to show and this test's to know: an object
            # native code made takes the constant that edit changed, and keeps what the game gave it.
            want = "%d.0" % early if name == "the first edit: the width of an object alive" else want
            want = "1063/%d" % (2 * early) if name == "the first edit: an object made where another had been is made again from its own arguments" else want
            want = "n%d" % (7 * early) if name == "the first edit: an object that holds text, made after others of its class, is made again from its own arguments" else want
            check("%s: %s" % (label, name), got == want, "got %r, expected %r" % (got, want))
        # Stamp was made by a constructor that could not be run again. The interpreter kept its first values,
        # and makes it again once the edit lets its constructor run again; native code kept nothing of it, and
        # no arguments of the object that was there before it are taken for its own.
        told = ("ActionScript: objects updated in place: Pair, Token, Worded. Not updated: Restart (it extends Godot's Node2D), Stamp (an object of it was "
                "made while the scripts ran as native code, by a constructor that could not be run again then). Restart the scene to see the new "
                "code on those." if native else
                "ActionScript: objects updated in place: Pair, Stamp, Token, Worded. Not updated: Restart (it extends Godot's Node2D). Restart the scene to "
                "see the new code on those.")
        check("%s: Godot is told what was updated in place, and what was not and why" % label, output.count(told) >= (2 if native else 1), output[-1500:])
        if native:
            check("%s: the libraries built ahead end, and none fails" % label, play_godot.settled(copy) and
                  not glob.glob(os.path.join(play_godot.cache(copy), "*.failed")), str(play_godot.held(copy)))
        check("%s: a reload starts no scene again by itself" % label, output.count("so the scene starts again") == 2, output[-1500:])
        check("%s: Godot is told why the scene a restart was asked for starts again" % label,
              "code that runs once changed in Restart, so the scene starts again" in output, output[-1500:])
        check("%s: Godot is told what is kept outside the scene and not made again" % label,
              re.search(r"not made again[^\n]*/root/Keeper", output) is not None, output[-1500:])
        check("%s: Godot is told a static field's first value changed and is kept" % label,
              "the first values of static fields changed in Restart" in output, output[-1500:])
        check("%s: Godot is told the scene starts once more" % label, "it starts once more when it is ready" in output and
              "so it starts once more" in output and "restart it by hand" not in output and "restart the scene to see it" not in output, output[-1500:])

restart_test("restart.gd")

# A game on a simulator is told where on this computer its files are edited (KAS3_SOURCE_DIR): a scene
# edited there is read from there, and the scene starts again from it.
with tempfile.TemporaryDirectory() as edited:
    copy = os.path.join(edited, "project")
    shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios"))
    # As Godot's editor saves a scene: with a uid. (An AS3 script has none, so its ext_resource has none.)
    with open(os.path.join(copy, "restart.tscn")) as file:
        scene = file.read().replace("format=3]", 'format=3 uid="uid://b8kas3restart"]')
    with open(os.path.join(copy, "restart.tscn"), "w") as file:
        file.write(scene)
    sources = os.path.join(edited, "sources")
    shutil.copytree(copy, sources, ignore=shutil.ignore_patterns("bin", ".godot"))
    environment = dict(os.environ)
    environment["KAS3_SOURCE_DIR"] = sources
    run = subprocess.run([godot, "--headless", "--path", copy, "-s", "scene_edit.gd"], capture_output=True, text=True, timeout=300,
                         env=environment)
    output = run.stdout + run.stderr
    values = re.findall(r"VALUE (.+?)=(.*)\|(.*)", output)
    check("scene_edit.gd ran to its end", run.returncode == 0 and len(values) == 3 and "leaked" not in output.lower(), output[-1500:])
    for name, got, want in values:
        check("scene_edit.gd: %s" % name, got == want, "got %r, expected %r" % (got, want))
    check("scene_edit.gd: no warning", "WARNING" not in output, re.findall(r"WARNING.*\n.*", output))
    check("scene_edit.gd: Godot is told why the scene started again", "the scene's file changed, so the scene starts again" in output,
          output[-1500:])

# The scene the iOS test app opens finishes and says so, here too; and when the scripts can't compile (a
# library older than they are), it says why instead of never finishing.
for broken in (False, True):
    with tempfile.TemporaryDirectory() as folder:
        copy = os.path.join(folder, "project")
        shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios"))
        if broken:
            with open(os.path.join(copy, "Broken.as"), "w") as file:
                file.write("package { public class Broken { public function Broken() { nothing(); } } }\n")
        run = subprocess.run([godot, "--headless", "--path", copy, "--quit-after", "200"], capture_output=True, text=True, timeout=300)
        output = run.stdout + run.stderr
        if broken:
            check("ios_test: scripts that do not compile are reported, and the run finishes",
                  "AS3TEST DONE passed=0 failed=1" in output and "did not load" in output, output[-1500:])
        else:
            done = re.search(r"AS3TEST DONE passed=(\d+) failed=(\d+)", output)
            check("ios_test: the scene finishes with every check passed", done is not None and done.group(2) == "0" and int(done.group(1)) > 10,
                  output[-1500:])

# The corpus programs that are said to run (corpus/runs.txt) do run, in Godot, for some frames.
with open(os.path.join(tests, "corpus", "runs.txt")) as listed:
    runs = listed.read().split()
with tempfile.TemporaryDirectory() as folder:
    copy = os.path.join(folder, "project")
    shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios", "*.as", "*.as.uid"))
    for name in runs:
        shutil.copy(os.path.join(tests, "corpus", name + ".as"), copy)
    run = subprocess.run([godot, "--headless", "--path", copy, "-s", "corpus.gd", "--", *runs], capture_output=True, text=True, timeout=600)
    output = run.stdout + run.stderr
    check("corpus.gd: no crash", run.returncode == 0, "exit %d\n%s" % (run.returncode, output[-800:]))
    for name in runs:
        check("corpus.gd: %s ran" % name, "RAN %s\n" % name in output, output[-800:])
    # And did what it does: the checksums of those whose work does not depend on the time between frames.
    with open(os.path.join(tests, "corpus", "checksums.txt")) as listed:
        wanted = [line.split() for line in listed if line.strip() and not line.startswith("#")]
    for name, value in wanted:
        check("corpus.gd: %s worked out %s" % (name, value), "CHECKSUM %s %s\n" % (name, value) in output,
              re.findall(r"CHECKSUM %s .*" % name, output))
    check("corpus.gd: no AS3 error", "Error" not in output and "ERROR" not in output and "ActionScript:" not in output, output[-1500:])

# Scripts edited while the game runs. In a copy of the project: the test rewrites a script.
HOT = """package { import godot.*; public class Hot extends Node2D {
	private var n:int = 0; private var h:int = 0; public static var made:int = 0; private var bag:*;%s
	public function bagText():String { return String(bag.length); }
	private var seen:Vector.<int> = new Vector.<int>();
	public function value():int { return %s; }
	public function bump():int { n++; made++; seen.push(n); return n; }
	public function seenText():String { return seen.join(","); }
	public function count():int { return n; }
	public function heard():int { return h; }
	public function total():int { return made; }
	public function listen(other:Node):void { other.renamed.add(onRenamed); }
	public function unlisten(other:Node):void { other.renamed.remove(onRenamed); }
	private function onRenamed():void { h += %s; }%s
	public function listenJoy():void { Input.joyConnectionChanged.add(onJoy); }
	public function clearJoy():void { Input.joyConnectionChanged.removeAll(); }
	private var kind:Class; private var pusher:Function; private var table:Object;
	public function hold():void { kind = Hot; pusher = seen.push; table = {a: 1, b: 2}; table.setPropertyIsEnumerable("a", false); }
	public function held():String { var keys:Array = []; for (var k:String in table) { keys.push(k); } return (kind == Hot) + " " + (this is kind) + " " + pusher(99) + " " + keys.join() + " " + table.a; }
	private function onJoy(device:int, connected:Boolean):void { onRenamed(); }
	private var keptNode:Node2D; private var crowd:Vector.<Node2D> = new Vector.<Node2D>();
	public function holdNodes():void { keptNode = new Node2D(); keptNode.position = new Vector2(1, 2); for (var i:int = 0; i < 3; i++) { var made:Node2D = new Node2D(); made.position = new Vector2(i, i); crowd.push(made); } }
	public function nodesText():String { var out:Array = []; try { out.push(keptNode.position.x); } catch (e:Error) { out.push(e.message); }
		for (var i:int = 0; i < crowd.length; i++) { try { var each:Node2D = crowd[i]; each.position = each.position + new Vector2(1, 0); out.push(each.position.x); } catch (e:Error) { out.push("gone"); } } return out.join(" "); }
	public function freeNode(which:int):void { if (which < 0) { keptNode.free(); } else { crowd[which].free(); } }
	public function goneLine():Number { return keptNode.position.x; }
	public function dropNodes():void { for (var i:int = 0; i < crowd.length; i++) { if (crowd[i]) { crowd[i].free(); } } crowd.length = 0; }
} }
"""
ADDED = "\n\tpublic function added():String { return \"new\"; }"

HOLD = " public function holdLater():void { bag = new Later(); }"

def reload_test(label, elsewhere, native=False):
    """Runs reload.gd in a copy of the project. With `elsewhere`, the scripts a person edits are in a
    folder outside the project, which the game is told of: what the project itself holds is stale. With
    `native`, the game starts as native code, by the library Play built in a run before: the first edit
    moves it onto the interpreter, and everything is as it is for a game that started there."""
    with tempfile.TemporaryDirectory() as edited:
        copy = os.path.join(edited, "project")
        shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios", "kas3"))
        versions = {
            # A vector of a type that only this version names, kept in an untyped field.
            "Hot.as": HOT % (" public function fill():void { bag = new <Vector.<Hot>>[new Vector.<Hot>(), new Vector.<Hot>()]; }", "1", "1", ""),
            "hot_v2.txt": HOT % (" private var extra:String = \"x\";", "2", "10", ADDED),
            # Later is a class that reload.gd adds, alone, in a folder it makes: the watch must see it.
            "hot_broken.txt": HOT % (" private var extra:String = \"x\";" + HOLD, "3 + Later.zero()", "10", ADDED),
            "hot_v4.txt": HOT % (" private var extra:String = \"x\";" + HOLD, "4 + Later.zero()", "10", ADDED),
            # Later is removed with it, while an untyped field holds one: the reload is refused.
            "hot_v5.txt": HOT % (" private var extra:String = \"x\";", "5", "10", ADDED),
        }
        for name, text in versions.items():
            with open(os.path.join(copy, name), "w") as file:
                file.write(text)
        environment = dict(os.environ)
        environment.pop("KAS3_SOURCE_DIR", None)
        environment["KAS3_REPORT_PREPARED"] = "1"
        for library in glob.glob(os.path.join(copy, "bin", "test_counts", "libkas3_godot.*")):
            shutil.copy2(library, os.path.join(copy, "bin"))
        if elsewhere:
            sources = os.path.join(edited, "sources")
            shutil.copytree(copy, sources, ignore=shutil.ignore_patterns("bin", ".godot"))
            with open(os.path.join(copy, "Hot.as"), "w") as file:
                file.write(HOT % ("", "99", "1", ""))
            environment["KAS3_SOURCE_DIR"] = sources
        if native:
            environment[play_godot.SWITCH] = "1"
            first = subprocess.run([godot, "--headless", "--path", copy, "--quit-after", "5"], capture_output=True, text=True, timeout=300, env=environment)
            check("%s: a run before builds the library" % label, play_godot.BUILDS in first.stdout + first.stderr and play_godot.settled(copy) and
                  len(play_godot.libraries(copy)) == 1, (first.stdout + first.stderr)[-900:])
        run = subprocess.run([godot, "--headless", "--path", copy, "-s", "reload.gd"], capture_output=True, text=True,
                             timeout=300, env=environment)
        output = run.stdout + run.stderr
        values = re.findall(r"VALUE (.+?)=(.*)\|(.*)", output)
        check("%s ran to its end" % label, run.returncode == 0 and len(values) == 27, output[-1500:])
        check("%s: the game %s" % (label, "starts as native code" if native else "runs on the interpreter"), (play_godot.USED in output) == native, output[:900])
        check("%s: %s" % (label, "the first reload says the interpreter runs the scripts from then on, and no other does" if native else
                          "no reload speaks of native code"), output.count(play_godot.FALLS_BACK) == (1 if native else 0), output[-1500:])
        if native:
            # Each reload has the library of the new scripts built for the next run: none is still being
            # written as the folder goes, and none of them failed.
            check("%s: the libraries built ahead end, and none fails" % label, play_godot.settled(copy) and
                  not glob.glob(os.path.join(play_godot.cache(copy), "*.failed")) and len(play_godot.libraries(copy)) >= 2,
                  str(play_godot.held(copy)))
        # A node held across reloads, freed and used again: the error Godot is told of names the line.
        check("%s: a node held across two reloads and freed since is AS3's error where it is used, with its line" % label,
              re.search(r"SCRIPT ERROR: TypeError: Error #1009[^\n]*Godot has freed the object\.\n\s+at Hot/goneLine\(\)\[[^\]]*Hot\.as:\d+\]", output) is not None,
              output[-1500:])
        check("%s: a reload that would drop an object whose class is gone is refused, and names it" % label,
              "an object of the class 'Later' is alive" in output, output[-1500:])
        check("%s: a reload asked for while AS3 runs says it was not made" % label,
              "can't be reloaded while AS3 runs" in output, output[-1500:])
        for name, got, want in values:
            check("%s: %s" % (label, name), got == want, "got %r, expected %r" % (got, want))
        check("%s: Godot is told the scripts were reloaded" % label, output.count("ActionScript: reloaded") == 5, output[-1500:])
        check("%s: a script with an error is reported and not loaded" % label,
              "were not reloaded" in output and "Hot.as:" in output, output[-1500:])
        # What the host worked out for a call at its own place (one that takes a StringName keeps the name
        # it made) is let go of with the program it was worked out for: after each reload, none is kept
        # that the program now running does not use.
        kept = re.findall(r"ActionScript: (\d+) calls keep a name of their own, (\d+) of them the running program's", output)
        check("%s: a reload keeps no call's name the running program does not use" % label,
              len(kept) == 5 and all(each[0] == each[1] for each in kept) and any(int(each[0]) > 0 for each in kept), str(kept))


reload_test("reload.gd", False)
# On a simulator the game runs from a pack, and is told where on this computer its scripts are edited.
reload_test("reload.gd, scripts in a folder elsewhere", True)
# A game that started as native code, by the library Play built: an edit moves it onto the interpreter.
reload_test("reload.gd, from native code", False, native=True)
restart_test("restart.gd, from native code", native=True)
# A reload asked for in the middle of AS3 while the game runs as native code: refused, as it is under the
# interpreter. The library is not let go of beneath a function of it that is running.
with tempfile.TemporaryDirectory() as edited:
    copy = os.path.join(edited, "project")
    shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios", "kas3"))
    environment = dict(os.environ, **{play_godot.SWITCH: "1"})
    first = subprocess.run([godot, "--headless", "--path", copy, "--quit-after", "5"], capture_output=True, text=True, timeout=300, env=environment)
    check("native_guard.gd: a run before builds the library", play_godot.BUILDS in first.stdout + first.stderr and play_godot.settled(copy) and
          len(play_godot.libraries(copy)) == 1, (first.stdout + first.stderr)[-900:])
    run = subprocess.run([godot, "--headless", "--path", copy, "-s", "native_guard.gd"], capture_output=True, text=True, timeout=300, env=environment)
    output = run.stdout + run.stderr
    check("native_guard.gd: a reload asked for while native code runs is refused, and says so", run.returncode == 0 and play_godot.USED in output and
          "can't be reloaded while AS3 runs" in output and "ActionScript: reloaded" not in output and "GUARD fired" in output,
          "exit %d\n%s" % (run.returncode, output[-1500:]))
    play_godot.settled(copy)

# An exported game keeps its files in a pack, not in a folder: the scripts must be found and read there.
import tempfile
with tempfile.TemporaryDirectory() as packed:
    subprocess.run([godot, "--headless", "--path", project, "-s", "pack.gd", "--", os.path.join(packed, "game.pck")],
                   capture_output=True, text=True, timeout=300)
    shutil.copytree(os.path.join(project, "bin"), os.path.join(packed, "bin"))
    for driver, wanted in (("driver.gd", "RESULT steps=30"), ("statics.gd", "VALUE cleared local=true|true"),
                           ("signals.gd", None), ("wide.gd", "VALUE heard=5000000000|5000000000")):
        run = subprocess.run([godot, "--headless", "--main-pack", "game.pck", "-s", driver], capture_output=True, text=True,
                             timeout=300, cwd=packed)
        output = run.stdout + run.stderr
        check("from a pack: %s runs" % driver, run.returncode == 0 and "were not loaded" not in output and "no script" not in output and
              (wanted is None or wanted in output), output[-700:])
    # A game in a pack has no folder to build its library in, and whoever plays it no compiler: Play builds
    # nothing there, and says nothing of it.
    run = subprocess.run([godot, "--headless", "--main-pack", "game.pck", "-s", "driver.gd"], capture_output=True, text=True, timeout=300, cwd=packed,
                         env=dict(os.environ, **{play_godot.SWITCH: "1"}))
    output = run.stdout + run.stderr
    check("from a pack: nothing is built, and nothing is said of native code", run.returncode == 0 and "RESULT steps=30" in output and
          "native code" not in output and sorted(os.listdir(packed)) == ["bin", "game.pck"], "%s\n%s" % (output[-700:], os.listdir(packed)))


# A project's tests are not part of the game that is run: a mistake in one of their scripts can't stop it.
# They are part of a run that starts in a test folder (a scene or script of the tests'), or is told so.
BROKEN = "package { public class Broken { public function f():void { nonsense(; } } }\n"
with tempfile.TemporaryDirectory() as edited:
    copy = os.path.join(edited, "project")
    shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios"))
    os.makedirs(os.path.join(copy, "tests", "framework"))
    for place in ("tests/Broken.as", "tests/framework/Deep.as"):
        with open(os.path.join(copy, place), "w") as file:
            file.write(BROKEN.replace("Broken", os.path.splitext(os.path.basename(place))[0]))
    shutil.copy(os.path.join(copy, "game_runs.gd"), os.path.join(copy, "tests", "run.gd"))

    def game(arguments, environment=None):
        done = subprocess.run([godot, "--headless", "--path", copy, *arguments], capture_output=True, text=True, timeout=300,
                              env=dict(os.environ, **(environment or {})))
        return done.stdout + done.stderr

    output = game(["-s", "game_runs.gd"])
    check("test folders: the game runs with a mistake in a test's script",
          "VALUE the game's scripts run=6|6" in output and "have errors" not in output, output[-800:])
    generated = os.path.join(copy, "android", "build")
    os.makedirs(generated)
    open(os.path.join(generated, ".gdignore"), "w").close()
    shutil.copy(os.path.join(copy, "Player.as"), generated)
    output = game(["-s", "game_runs.gd"])
    check("ignored Gradle output can contain a second copy of a game script",
          "VALUE the game's scripts run=6|6" in output and "already defined" not in output, output[-800:])
    output = game(["-s", "tests/run.gd"])
    check("test folders: a run that starts in a test folder has the tests' scripts", "have errors" in output and "Broken.as" in output, output[-800:])
    output = game(["-s", "game_runs.gd"], {"KAS3_TESTS": "1"})
    check("test folders: a run told it is the tests' has their scripts", "have errors" in output and "Deep.as" in output, output[-800:])
    # The folders are the project's to name (actionscript/tests/folders): with another named, `tests` is the game's.
    with open(os.path.join(copy, "project.godot"), "a") as file:
        file.write('\n[actionscript]\n\ntests/folders=PackedStringArray("res://checks")\n')
    output = game(["-s", "game_runs.gd"])
    check("test folders: a folder the project does not name is the game's", "have errors" in output and "Broken.as" in output, output[-800:])
    shutil.move(os.path.join(copy, "tests"), os.path.join(copy, "checks"))
    output = game(["-s", "game_runs.gd"])
    check("test folders: the folder the project names is left out",
          "VALUE the game's scripts run=6|6" in output and "have errors" not in output, output[-800:])

test_web_export.check_export(godot)

native = native_godot.Runner(godot, project, tests, os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None, check, counted=True)
native.probe()
native.workloads()
native.frames()
play_godot.Runner(native, check).play()

for failure in failures:
    print("FAIL " + failure)
print("godot: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
