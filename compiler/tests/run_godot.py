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
    "returned script object": "<null>",
}
for name, value in expected.items():
    check("check: " + name, lines.get(name) == value, "got %r, expected %r" % (lines.get(name), value))
check("a call on a freed object is an AS3 error", "CHECK survived freed object" in output and "freed" in output, output[-800:])
check("says why a script won't attach to the wrong kind of object", "can only be attached to a CharacterBody2D" in output, output[-900:])
check("says why a plain class is not a script", "Plain.as" in output and "does not extend a Godot class" in output, output[-900:])
check("says a script object can't be returned to Godot", output.count("returns a script object") == 1, output[-900:])
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
    "add", "sub", "times", "times int", "number first", "divide", "negate", "cells", "cells by number", "cells over",
    "tint", "same", "not same", "differs", "same node", "not same node", "other node", "chain", "compound", "moved",
    "cells over zero", "after the error",
    "text", "text after", "cast", "own", "color text", "node text", "node joined", "null text", "null node", "null own",
    "freed text",
    "set x", "add x", "bump x", "nested", "held part", "local part", "other part", "from itself",
    "first", "color part", "set second", "add element", "position element", "packed", "set packed",
    "packed out of range", "out of range",
    "clamp default", "clamp one", "clamp trailing null", "clamp both null", "clamp middle null", "looking",
    "looking null", "whole numbers", "to string", "to string in AS3", "no to string", "no to string in AS3",
    "negative cells", "cells plus", "cells compound", "plain order", "compound order", "mixed", "big hash", "decode",
])
check("a null that is not the last argument is an AS3 error", "at Values/clampMiddleNull()" in output, output[-800:])
check("an index out of range is an AS3 error", "at Values/outOfRange()" in output and "at Values/packed()" in output, output[-800:])
traced = re.search(r"EXPECT TRACE (.*)", output)
check("trace shows Godot's text", traced is not None and ("\n" + traced.group(1) + "\n") in output, output[-800:])
check("toString() on a missing node is an AS3 error", "at Values/nullOwn()" in output, output[-800:])
check("dividing whole numbers by zero is an AS3 error", "at Values/cellsOver()" in output, output[-800:])

# Signals, the AS3 way.
output = compare("signals.gd", [
    "add", "has", "one listener", "still once", "emit", "remove", "add once", "remove all", "counts what scripts added", "remove all leaves others", "lasting or once, not both",
    "null argument and order", "arguments", "removes itself",
    "after a failing listener", "another object's signal", "by name", "by a wrong name", "user signal", "filter",
    "valid while its owner lives", "gone with its owner", "deferred not yet", "deferred after the frame",
    "deferred with its owner freed", "own signal field by name", "a loop lives until the frame ends", "a loop is freed", "a longer loop is freed",
    "a removed listener that is still held keeps what it holds",
    "a loop whose callable is held stays", "a loop goes when its callable is let go",
    "an unheld listener stays",
])
check("a listener given the wrong kind of argument is an AS3 error that names it",
      "TypeError: Error #1034" in output and "Signals/onHit" in output, output[-1200:])
check("a listener given too many arguments is an AS3 error that names it",
      "ArgumentError: Error #1063: Argument count mismatch on Signals/onHit(). Expected 1, got 2." in output, output[-1200:])
check("a listener's argument error says where the listener is", re.search(r"Error #1063[^\n]*\n\s*at Signals/onHit\(\)\[.*Signals\.as:\d+\]", output) is not None, output[-1200:])
check("a field that is not a signal, by name, is an AS3 error", "'count' is not a signal" in output, output[-1200:])
check("an error inside a listener is reported", "at Signals/fails()" in output, output[-800:])
check("a wrong signal name is an AS3 error", output.count("at Signals/listenByName()") == 2, output[-800:])
check("add after addOnce, and the reverse, are AS3 errors", "at Signals/mixed()" in output and "at Signals/mixedBack()" in output,
      output[-800:])
check("an empty signal says so", "the signal is empty" in output and "at Signals/empty()" in output, output[-800:])

for failure in failures:
    print("FAIL " + failure)
print("godot: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
