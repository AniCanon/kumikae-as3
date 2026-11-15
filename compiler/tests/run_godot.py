"""AS3 inside Godot: runs the test project in headless Godot and checks what the script did.

Usage: run_godot.py <tests dir>

Needs the extension library (built when CMake is given -DKAS3_GODOT_CPP=<godot-cpp checkout>) and Godot 4.7
(`godot` on the PATH, or the KAS3_GODOT environment variable). Without either, the test is skipped and says
so: it does not fail.

The project is compiler/tests/godot_project: Player.as on a CharacterBody2D sets its velocity and calls
moveAndSlide() for 30 physics frames. At 200 units a second and 60 frames a second that is 100 units.
"""
import glob
import os
import re
import shutil
import subprocess
import sys

tests = os.path.abspath(sys.argv[1])
project = os.path.join(tests, "godot_project")
godot = os.environ.get("KAS3_GODOT") or shutil.which("godot")


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


# Godot learns of a project's extensions when the editor first scans it. Known fault: that first scan ends
# in a crash on exit when an extension registers a scripting language (the speed spike's does the same),
# after the scan has done its work. So only the list it leaves behind is checked.
listed = os.path.join(project, ".godot", "extension_list.cfg")
if not os.path.exists(listed):
    subprocess.run([godot, "--headless", "--path", project, "--import"], capture_output=True, text=True, timeout=300)
check("the extension is listed", os.path.exists(listed) and "kas3.gdextension" in open(listed).read())

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
    "after a failing listener", "another object's signal", "by name", "by a wrong name", "listeners and emit by name", "a listener's error comes back out of emit", "user signal", "declared signal", "declared signal listed", "filter",
    "valid while its owner lives", "gone with its owner", "deferred not yet", "deferred after the frame",
    "deferred with its owner freed", "own signal field by name", "a loop lives until the frame ends", "a loop is freed", "a longer loop is freed",
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
check("add after addOnce, and the reverse, are AS3 errors", "at Signals/mixed()" in output and "at Signals/mixedBack()" in output,
      output[-800:])
check("an empty signal says so", "the signal is empty" in output and "at Signals/empty()" in output, output[-800:])

# Singletons, constants and static methods, as GDScript works them out.
output = compare("statics.gd", [
    "pressed", "strength", "stick", "system", "an editor-only singleton is an AS3 error", "a class's static method",
    "orphan nodes printed", "a static call's arguments in order", "the value before the missing object", "a static call's result of the wrong type",
    "a singleton's members through a variable", "a singleton variable that is null", "slowed", "time scale", "faster", "faster in Godot", "at start", "mouse",
    "listening", "heard", "cleared", "others still listen", "not listening", "a function written in place listens", "and is removed through its value", "input class", "same singleton",
    "singleton object",
    "escape", "captured", "ready notice", "axis", "zero", "up", "red", "forward", "identity", "sum", "up y",
    "default key", "given key",
    "from angle", "hsv", "html", "euler", "image", "after orphans",
    "unset", "unset tint", "unset spot", "unset list", "cleared local",
    "declared twice", "declared twice, value first", "declared twice, other order", "declaration skipped",
    "field read early", "static read early", "race",
])
check("statics.gd: an error's source is where it was thrown, whatever its message says",
      re.search(r"at: Statics/fakeTrace\(\) \(res://Statics\.as:\d+\)", output) is not None and "at: fake()" not in output, output[-1500:])
check("statics.gd: Godot says nothing of an editor-only singleton", "Can't retrieve singleton" not in output, output[-900:])
check("statics.gd: a call from a worker thread is refused and says so",
      "Statics/spin() was called by Godot from another thread" in output, output[-900:])
check("statics.gd: no AS3 error", "Error #" not in output and "does not run" not in output and "Godot has no" not in output,
      output[-900:])


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
    "lerpf", "rest from Godot", "clampf", "clampi", "absi", "maxi", "sin", "snappedf", "wrapi", "posmod", "is_equal_approx", "randf",
    "randi_range", "floori and roundi", "hash", "str", "is_instance_valid", "instance_from_id", "instance_from_id counted",
    "lerp as a number", "max of three", "wrong argument", "in a loop", "no arguments", "whole numbers", "no answer",
    "anything as text", "a default is not shared",
    "max past what a Number holds", "lerp of vectors", "int of lerp cuts", "Boolean of max", "lerp and max as text",
    "lerp of anything", "lerp into the wrong type",
])
check("functions.gd: a wide whole number into a Number is a RangeError",
      "RangeError: 9007199254740993 does not fit a Number exactly.\n\tat Functions/widest()" in output, output[-1500:])

check("functions.gd: a function that could not answer is an AS3 error", "at Functions/noAnswer()" in output and "'max'" in output,
      output[-1200:])
check("functions.gd: print, printerr and pushWarning reach Godot's output",
      "said 4 (1.0, 2.0)" in output and "to the error output" in output and "a warning from AS3" in output, output[-1200:])
check("functions.gd: a wrong argument is an AS3 error that names the function", "at Functions/wrong()" in output and "lerp" in output,
      output[-1200:])

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
    "an int plus one stays an int", "a Godot value and an untyped one",
    "a value read above its line is its zero", "an untyped node is a Node", "an untyped value as a Node", "a part is written to the object named before the value", "the other object is untouched",
    "minusOne of an int stays an int", "twice of an int stays an int", "stepped of an int stays an int",
    "an Int64 minus one stays whole", "a half is a float", "a Godot value plus text joins them",
    "text plus a Godot value joins them", "text plus-assigned a Godot value joins them",
    "a Godot value plus a number joins them, as AS3 does", "a Godot value plus-assigned a Godot value",
])
check("untyped.gd: a value of another Godot type is AS3's coercion error, where it is stored",
      output.count("Error #1034") >= 5 and "at Untyped/asVector()" in output and "at Untyped/asNode()" in output and
      "at Untyped/place()" in output, output[-2000:])
check("untyped.gd: a script object reaching Godot is said", "giveScript returns a script object" in output and
      "'keep' holds a script object" in output, output[-2000:])
check("untyped.gd: a value of the wrong type is AS3's coercion error", "at Untyped/wrongType()" in output and "Error #1034" in output,
      output[-1500:])
check("untyped.gd: a thrown Godot value nothing catches is reported as its text", "(3.0, 4.0)\n\tat Untyped/throwValue()" in output,
      output[-2000:])
check("untyped.gd: a script object given to Godot is an AS3 error", "at Untyped/scriptObject()" in output, output[-1500:])

# Godot's own classes with `is` and `as`, and input events as a script receives them.
compare("events.gd", [
    "kind of InputEventScreenTouch", "kind of InputEventScreenDrag", "kind of InputEventMouseButton", "kind of InputEventKey",
    "touch position", "drag position", "neither", "is Node", "as Sprite2D", "null", "the script's own object", "delivered",
])

# AS3's vectors handed to Godot: packed arrays by the element's type, arrays for the rest.
output = compare("vectors.gd", [
    "points", "points type", "hull", "bytes", "empty", "too big for a byte", "below a byte", "offsets", "colors", "filters",
    "polygons", "whole", "whole are integers", "fractions", "fractions are floats", "words", "flags", "nodes", "wide",
    "keep", "spread", "moved", "words of values", "forget", "array to Godot", "array from Godot", "Godot's array as an Array", "made a node", "the node is in the tree", "made a resource", "no vector", "no vector where Godot takes points",
    "added to while it is shortened", "what it let go of is freed, what was added is held", "and let go of in the end",
])
check("vectors.gd: no vector where Godot takes a packed array is an AS3 error", "at Vectors/noPoints()" in output, output[-1500:])
check("vectors.gd: a value that does not fit a byte is an AS3 error that says so",
      output.count("does not fit a byte") == 2 and "at Vectors/tooBig()" in output and "at Vectors/negative()" in output, output[-1500:])

import tempfile

# A scene edited while it runs: an edit to `_ready` (code that runs once) starts the scene again; an edit
# elsewhere keeps the scene and its state. The versions differ in size: two saves of one size within a
# second are seen as one (see the backlog).
RESTART = """package { import godot.*; public class Restart extends Node2D {%s
	private var word:String;
	override public function _ready():void { word = "%s"; }
	public function said():String { return word%s; }
} }
"""
with tempfile.TemporaryDirectory() as edited:
    copy = os.path.join(edited, "project")
    shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios"))
    # The second version also adds a field before the one `_ready` sets: nothing that ran changed.
    unused = "\n\tpublic var unused:int;"
    for name, text in {"Restart.as": RESTART % ("", "one", ""), "restart_body.txt": RESTART % (unused, "one", ' + "!"'),
                       "restart_ready.txt": RESTART % (unused, "three", ' + "!"')}.items():
        with open(os.path.join(copy, name), "w") as file:
            file.write(text)
    run = subprocess.run([godot, "--headless", "--path", copy, "-s", "restart.gd"], capture_output=True, text=True, timeout=300)
    output = run.stdout + run.stderr
    values = re.findall(r"VALUE (.+?)=(.*)\|(.*)", output)
    check("restart.gd ran to its end", run.returncode == 0 and len(values) == 5, output[-1500:])
    for name, got, want in values:
        check("restart.gd: %s" % name, got == want, "got %r, expected %r" % (got, want))
    check("restart.gd: Godot is told why the scene started again", "so the scene starts again" in output, output[-1500:])

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
	private var n:int = 0; private var h:int = 0; public static var made:int = 0;%s
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
} }
"""
ADDED = "\n\tpublic function added():String { return \"new\"; }"

def reload_test(label, elsewhere):
    """Runs reload.gd in a copy of the project. With `elsewhere`, the scripts a person edits are in a
    folder outside the project, which the game is told of: what the project itself holds is stale."""
    with tempfile.TemporaryDirectory() as edited:
        copy = os.path.join(edited, "project")
        shutil.copytree(project, copy, ignore=shutil.ignore_patterns("ios"))
        versions = {
            "Hot.as": HOT % ("", "1", "1", ""),
            "hot_v2.txt": HOT % (" private var extra:String = \"x\";", "2", "10", ADDED),
            "hot_broken.txt": HOT % ("", "2 +", "10", ADDED),
            "hot_v3.txt": HOT % (" private var extra:String = \"x\";", "3", "10", ADDED),
            "hot_v4.txt": HOT % (" private var extra:String = \"x\";", "4", "10", ADDED),
        }
        for name, text in versions.items():
            with open(os.path.join(copy, name), "w") as file:
                file.write(text)
        environment = dict(os.environ)
        environment.pop("KAS3_SOURCE_DIR", None)
        if elsewhere:
            sources = os.path.join(edited, "sources")
            shutil.copytree(copy, sources, ignore=shutil.ignore_patterns("bin", ".godot"))
            with open(os.path.join(copy, "Hot.as"), "w") as file:
                file.write(HOT % ("", "99", "1", ""))
            environment["KAS3_SOURCE_DIR"] = sources
        run = subprocess.run([godot, "--headless", "--path", copy, "-s", "reload.gd"], capture_output=True, text=True,
                             timeout=300, env=environment)
        output = run.stdout + run.stderr
        values = re.findall(r"VALUE (.+?)=(.*)\|(.*)", output)
        check("%s ran to its end" % label, run.returncode == 0 and len(values) == 13, output[-1500:])
        for name, got, want in values:
            check("%s: %s" % (label, name), got == want, "got %r, expected %r" % (got, want))
        check("%s: Godot is told the scripts were reloaded" % label, output.count("ActionScript: reloaded") == 3, output[-1500:])
        check("%s: a script with an error is reported and not loaded" % label,
              "were not reloaded" in output and "Hot.as:" in output, output[-1500:])


reload_test("reload.gd", False)
# On a simulator the game runs from a pack, and is told where on this computer its scripts are edited.
reload_test("reload.gd, scripts in a folder elsewhere", True)

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

for failure in failures:
    print("FAIL " + failure)
print("godot: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
