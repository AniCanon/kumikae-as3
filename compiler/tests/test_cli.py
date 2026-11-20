"""The kas3c command line: exit codes and where output goes. Usage: test_cli.py <path to kas3c>"""
import os
import resource
import re
import subprocess
import sys
import tempfile
import time

KAS3C = sys.argv[1]
failures = []


def run(*args):
    return subprocess.run([KAS3C, *args], capture_output=True, text=True, timeout=60)


def children_cpu():
    """The processor time, in seconds, that the processes this test ran and waited for have used: to the
    microsecond, and the same however busy the machine is."""
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def check(name, condition, detail=""):
    if not condition:
        failures.append("%s %s" % (name, detail))


with tempfile.TemporaryDirectory() as folder:
    good = os.path.join(folder, "Good.as")
    bad = os.path.join(folder, "Bad.as")
    with open(good, "w") as handle:
        handle.write("package { public class Good { public var x:int = 1; } }\n")
    with open(bad, "w") as handle:
        handle.write("package { public class Bad { var = ; } }\n")

    result = run("--version")
    check("version", result.returncode == 0 and result.stdout == "kas3c 0.1.0\n", repr(result.stdout))

    result = run("--help")
    check("help", result.returncode == 0 and result.stdout.startswith("usage: kas3c parse [--dump-ast] <file>..."))
    # What the language server answers (lsp/README.md), each said in the help.
    served = result.stdout[result.stdout.find("\nlsp    "):]
    for answer in ("errors as you type", "go to definition", "find references", "completion", "signature help", "hover", "outline",
                   "symbol search"):
        check("help says the language server answers: " + answer, answer in " ".join(served.split()))

    result = run()
    check("no arguments", result.returncode == 2 and result.stderr.startswith("usage: kas3c parse") and
          result.stdout == "", repr(result))

    result = run("frobnicate")
    check("unknown command", result.returncode == 2 and "usage: kas3c parse" in result.stderr)

    # check: names across files.
    undefined = os.path.join(folder, "Undefined.as")
    with open(undefined, "w") as handle:
        handle.write("package { public class Undefined { var x:Missing; } }\n")
    stub = os.path.join(folder, "lib.as")
    with open(stub, "w") as handle:
        handle.write("package lib { public native class Missing {} }\n")
    uses = os.path.join(folder, "Uses.as")
    with open(uses, "w") as handle:
        handle.write("package { import lib.Missing; public class Uses { var x:Missing; } }\n")

    result = run("check", good)
    check("check clean", result.returncode == 0 and result.stdout == "" and result.stderr == "", repr(result))
    result = run("check", bad)
    check("check syntax error", result.returncode == 1 and "Bad.as:1:" in result.stderr, repr(result))
    # A syntax error in one file does not hide a certain error in a file that uses nothing of it.
    typed = os.path.join(folder, "Typed.as")
    with open(typed, "w") as handle:
        handle.write("package { public class Typed { var n:int = \"s\"; } }\n")
    result = run("check", bad, typed)
    check("check beside a syntax error", result.returncode == 1 and "Bad.as:1:" in result.stderr and
          "%s:1:44: error: can't use a value of type 'String' where 'int' is expected\n" % typed in result.stderr,
          repr(result))
    result = run("check", undefined)
    check("check name error", result.returncode == 1 and
          result.stderr == "%s:1:42: error: unknown type 'Missing'\n" % undefined and result.stdout == "",
          repr(result))
    result = run("check", "--declarations", stub, uses)
    check("check with declarations", result.returncode == 0 and result.stderr == "", repr(result))
    result = run("check", uses)
    check("check without declarations", result.returncode == 1 and "can't find 'lib.Missing'" in result.stderr,
          repr(result))
    result = run("check")
    check("check no files", result.returncode == 2 and "usage: kas3c parse" in result.stderr)
    result = run("check", "--declarations")
    check("check dangling option", result.returncode == 2)
    result = run("check", "--declarations", os.path.join(folder, "nope.as"), good)
    check("check unreadable declarations", result.returncode == 2 and "cannot read" in result.stderr)
    result = run("check", os.path.join(folder, "nope.as"))
    check("check unreadable", result.returncode == 2 and "cannot read" in result.stderr)
    result = run("check", "--bogus", good)
    check("check unknown option", result.returncode == 2)
    result = run("check", "--dump-names", uses, "--declarations", stub)
    check("check dump", result.returncode == 0 and
          result.stdout == "1:57 Missing -> class lib.Missing @%s:1:35\n" % stub, repr(result))

    result = run("parse")
    check("parse without files", result.returncode == 2 and "usage: kas3c parse" in result.stderr)

    missing = os.path.join(folder, "Missing.as")
    result = run("parse", missing)
    check("missing file", result.returncode == 2 and result.stderr == "kas3c: cannot read %s\n" % missing,
          repr(result.stderr))

    result = run("parse", good)
    check("good file", result.returncode == 0 and result.stdout == "" and result.stderr == "", repr(result))

    result = run("parse", bad)
    check("bad file exit", result.returncode == 1, str(result.returncode))
    check("bad file message", re.match(r"^.+:\d+:\d+: error: ", result.stderr) is not None, repr(result.stderr))
    check("bad file stdout", result.stdout == "")

    result = run("parse", "--dump-ast", good)
    check("dump one", result.returncode == 0 and result.stdout.startswith("Program @1:1\n"), repr(result.stdout[:40]))

    result = run("parse", "--dump-ast", good, bad)
    headers = [line for line in result.stdout.splitlines() if line.startswith("# ")]
    check("dump two", result.returncode == 1 and headers == ["# " + good, "# " + bad], repr(headers))

    result = run("parse", missing, bad)
    check("unreadable plus bad", result.returncode == 2 and "cannot read" in result.stderr and
          ": error: " in result.stderr, repr(result.stderr))

    # A long expression must print without exhausting the stack, and one past the cap is an error.
    deep = os.path.join(folder, "Deep.as")
    with open(deep, "w") as handle:
        handle.write("var x:int = 1" + "+1" * 400 + ";\n")
    result = run("parse", "--dump-ast", deep)
    check("long sum prints", result.returncode == 0 and result.stdout.count("NumberLit") == 401,
          "%d %d" % (result.returncode, result.stdout.count("NumberLit")))
    with open(deep, "w") as handle:
        handle.write("var x:int = 1" + "+1" * 100000 + ";\n")
    result = run("parse", "--dump-ast", deep)
    check("too long sum", result.returncode == 1 and result.stderr.count("expression is too long") == 1,
          "%d %r" % (result.returncode, result.stderr[:200]))

    # Shapes that once crashed: each must exit 1 with a diagnostic, never die on a signal.
    crashers = {
        "new chain": "x = " + "new " * 50000 + "A;",
        "at chain": "x = " + "@" * 300000 + "a;",
        "nested functions": "var f:Function = " + "function():* { return 1" + "+1" * 300 + "+ " * 0 +
                            ";};" * 0 + "",
    }
    nested = "1" + "+1" * 400
    for _ in range(60):
        nested = "function():* { return " + nested + "+1" + "+1" * 400 + "; }"
    crashers["nested functions"] = "var f:Function = " + nested + ";"
    for name, text in crashers.items():
        with open(deep, "w") as handle:
            handle.write(text + "\n")
        result = run("parse", "--dump-ast", deep)
        check(name, result.returncode == 1 and ": error: " in result.stderr, "exit %d" % result.returncode)

    # cpp: the program as C++ in a folder, or refused by what it lacks with nothing written.
    native = os.path.join(folder, "native")
    result = run("cpp", good, "-o", native)
    written = sorted(os.listdir(native)) if os.path.isdir(native) else []
    check("cpp writes", result.returncode == 0 and result.stdout == "" and result.stderr == "" and
          "program.h" in written and "program.cpp" in written and "Good.cpp" in written, "%d %r %r" % (result.returncode, result.stderr, written))
    # --unit: the whole program in program.cpp, and a class's file of an earlier run taken away, so that
    # building every .cpp in the folder builds the program once.
    result = run("cpp", "--unit", good, "-o", native)
    written = sorted(os.listdir(native)) if os.path.isdir(native) else []
    whole = open(os.path.join(native, "program.cpp")).read() if "program.cpp" in written else ""
    check("cpp --unit writes one file of C++", result.returncode == 0 and result.stderr == "" and written == ["program.cpp", "program.h"] and
          "kas3_program" in whole and "Good" in whole, "%d %r %r" % (result.returncode, result.stderr, written))
    result = run("cpp", good)
    check("cpp without a folder", result.returncode == 2 and "usage: kas3c parse" in result.stderr)
    result = run("cpp", bad, "-o", os.path.join(folder, "never"))
    check("cpp of a program with errors", result.returncode == 1 and "Bad.as:1:" in result.stderr and
          not os.path.exists(os.path.join(folder, "never")), repr(result))
    mover = os.path.join(folder, "Mover.as")
    with open(mover, "w") as handle:
        handle.write("package {\n\timport godot.*;\n\tpublic class Mover extends Node2D {\n"
                     "\t\tpublic function step():void { position = new Vector2(1, 2); }\n\t}\n}\n")
    script = os.path.join(folder, "script")
    result = run("cpp", "--godot", mover, "-o", script)
    written = sorted(os.listdir(script)) if os.path.isdir(script) else []
    check("cpp writes a script for Godot", result.returncode == 0 and result.stderr == "" and "Mover.cpp" in written and "program.cpp" in written,
          "%d %r %r" % (result.returncode, result.stderr, written))
    result = run("cpp", mover, "-o", os.path.join(folder, "never2"))
    check("cpp of a Godot script without --godot", result.returncode == 1 and not os.path.exists(os.path.join(folder, "never2")), repr(result))

    # check: Godot's API is known without any option, under its AS3 names.
    player = os.path.join(folder, "Player.as")
    program = """package {
	import godot.*;

	public class Player extends CharacterBody2D {
		[Export] public var maxSpeed:Number = 300;
		[Signal] public var died:Signal;
		private var timer:Timer = new Timer();

		override public function _ready():void {
			timer.timeout.add(onTimeout);
			(getNode("Hitbox") as Area2D).bodyEntered.add(onBodyEntered);
			processMode = Node.PROCESS_MODE_PAUSABLE;
		}

		override public function _physicsProcess(delta:Number):void {
			var direction:Number = Input.getAxis("left", "right");
			velocity.x = direction * maxSpeed;
			if (isOnFloor() && Input.isKeyPressed(Key.ESCAPE)) {
				velocity.y = -400;
			}
			moveAndSlide();
			position = new Vector2(lerp(position.x, 0, 0.5), Vector2.ZERO.y);
			var ticks:Number = Time.getTicksUsec();
			var data:GodotDictionary = new GodotDictionary();
			if (!data.isEmpty() || GodotJSON.stringify(data) == "") { print("ticks", ticks); }
		}

		private function onTimeout():void {}

		private function onBodyEntered(body:Node2D):void {
			if (body.isInGroup("enemies")) {
				died.emit();
				queueFree();
			}
		}
	}
}
"""
    with open(player, "w") as handle:
        handle.write(program)
    result = run("check", player)
    check("godot program", result.returncode == 0 and result.stderr == "", repr(result.stderr))

    wrong = program.replace("_physicsProcess(delta:Number)", "_physicsProcess(delta:int)")
    wrong = wrong.replace("moveAndSlide();", "move_and_slide();").replace("new Timer()", "new Timer(5)")
    wrong = wrong.replace("died.emit();", "body.name = 5;")
    with open(player, "w") as handle:
        handle.write(wrong)
    result = run("check", player)
    messages = [line.split(": error: ")[1] for line in result.stderr.splitlines()]
    check("godot errors", result.returncode == 1 and messages == [
        "'Timer' takes no arguments but 1 was given",
        "'_physicsProcess' must have the same parameters and return type as '_physicsProcess' in Node",
        "undefined name 'move_and_slide'",
        "can't use a value of type 'int' where 'String' is expected",
    ], repr(result.stderr))

    with open(player, "w") as handle:
        handle.write(program)
    result = run("check", "--no-godot", player)
    check("no godot", result.returncode == 1 and "can't find package 'godot'" in result.stderr, repr(result.stderr))

    # A program that defines a name Godot has is told so at its own line, once.
    clash = os.path.join(folder, "Node.as")
    with open(clash, "w") as handle:
        handle.write("package godot {\n\tpublic class Node {}\n}\n")
    result = run("check", clash)
    check("godot clash", result.returncode == 1 and
          result.stderr == "%s:2:15: error: 'Node' is already defined in Godot's API\n" % clash,
          repr(result.stderr[:300]))

# A warning is said, and the program still checks and runs.
with tempfile.TemporaryDirectory() as warned:
    rounded = os.path.join(warned, "Rounded.as")
    with open(rounded, "w") as handle:
        handle.write("package { public class Rounded { public function Rounded() { var n:Number = 9007199254740993; trace(n); } } }\n")
    result = run("check", rounded)
    check("a warning does not fail the check", result.returncode == 0 and
          result.stderr == "%s:1:77: warning: 9007199254740993 is rounded: a Number can't hold every digit of it. Store it in an Int64 to keep them\n" % rounded,
          repr(result.stderr[:300]))
    result = run("run", rounded)
    check("a program with a warning runs", result.returncode == 0 and result.stdout == "9007199254740992\n", repr(result.stdout[:200]))

# An endless loop is stopped after Flash's time, or the one KAS3_TIME_LIMIT_MS gives, with Flash's error.
with tempfile.TemporaryDirectory() as looping:
    spin = os.path.join(looping, "Spin.as")
    with open(spin, "w") as handle:
        handle.write("package { public class Spin { public function Spin() { try { while (true) {} } catch (e:*) { trace(\"caught\"); } } } }\n")
    started = time.monotonic()
    before = children_cpu()
    result = subprocess.run([KAS3C, "run", spin], capture_output=True, text=True, timeout=60,
                            env=dict(os.environ, KAS3_TIME_LIMIT_MS="200"))
    took = time.monotonic() - started
    used = children_cpu() - before
    # At least the limit by the clock; and by the processor time it used, which a busy machine does not
    # stretch, not much more than the limit.
    check("an endless loop is stopped", result.returncode == 1 and result.stdout == "" and 0.2 <= took and used < 5 and
          result.stderr.startswith("Error: Error #1502: A script has executed for longer than the timeout period of 0.2 seconds.\n\tat Spin()"),
          repr((result.returncode, result.stdout, result.stderr[:300], took, used)))

# A finally is written once, however many ways lead out of its try.
once = os.path.join(os.path.dirname(os.path.abspath(__file__)), "run", "finally_once.as")
result = run("ir", once)
check("finally written once", result.returncode == 0 and result.stdout.count('"cleanup marker"') == 1,
      "%d copies" % result.stdout.count('"cleanup marker"'))

# Godot's API is loaded only for files that name its package: a file that doesn't is checked far faster.
# Measured as the processor time the compiler itself used, which does not move with what else the machine does.
def best_time(*args):
    best = 1e9
    for _ in range(3):
        before = children_cpu()
        run(*args)
        best = min(best, children_cpu() - before)
    return best

plain = os.path.join(os.path.dirname(os.path.abspath(__file__)), "run", "small_fixes.as")
with_godot = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus", "Empty.as")
plain_time, godot_time = best_time("check", plain), best_time("check", with_godot)
check("check skips Godot's API when no file names it", plain_time * 2 < godot_time,
      "plain %.3fs, with Godot %.3fs" % (plain_time, godot_time))

# A script for Godot is lowered with Godot's API in reach, as the extension does it.
empty = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus", "Empty.as")
result = run("ir", empty)
check("ir without godot", result.returncode == 1 and "can't find 'godot.Node2D'" in result.stderr, repr(result.stderr[:200]))
result = run("ir", "--godot", empty)
check("ir --godot", result.returncode == 0 and "Empty" in result.stdout and result.stderr == "", repr(result.stderr[:200]))

with tempfile.TemporaryDirectory() as pack_dir:
    packed = os.path.join(pack_dir, "program.kas3")
    result = run("pack", "--godot", empty, "-o", packed)
    check("pack --godot", result.returncode == 0 and result.stderr == "" and os.path.isfile(packed) and
          open(packed, "rb").read(4) == b"KAS3", repr(result.stderr[:200]))
    result = run("pack", "--godot", empty)
    check("pack requires -o", result.returncode == 2 and "usage: kas3c parse" in result.stderr)

for failure in failures:
    print("FAIL " + failure)
print("cli: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
