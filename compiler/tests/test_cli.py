"""The kas3c command line: exit codes and where output goes. Usage: test_cli.py <path to kas3c>"""
import os
import re
import subprocess
import sys
import tempfile

KAS3C = sys.argv[1]
failures = []


def run(*args):
    return subprocess.run([KAS3C, *args], capture_output=True, text=True, timeout=60)


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

for failure in failures:
    print("FAIL " + failure)
print("cli: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
