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
    "negative uint": "<null>",
    "number from text": "<null>",
    "node from int": "<null>",
    "string from null": "<null>",
    "flag": "true",
    "counted references": "1 hello=hello",
    "counted freed": "true",
    "things": "a.Thing b.Thing",
    "wrong node has script": "false",
    "plain can attach": "false",
    "returned script object": "<null>",
}
for name, value in expected.items():
    check("check: " + name, lines.get(name) == value, "got %r, expected %r" % (lines.get(name), value))
check("a call on a freed object is an AS3 error", "CHECK survived freed object" in output and "freed" in output, output[-800:])
check("says why a script won't attach to the wrong kind of object", "can only be attached to a CharacterBody2D" in output, output[-900:])
check("says why a plain class is not a script", "Plain.as" in output and "does not extend a Godot class" in output, output[-900:])
check("says a script object can't be returned to Godot", "returns a script object" in output, output[-900:])
check("checks: no crash", run.returncode == 0, "exit %d\n%s" % (run.returncode, output[-800:]))
check("checks: nothing leaked", "leaked" not in output.lower(), output[-600:])

for failure in failures:
    print("FAIL " + failure)
print("godot: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
