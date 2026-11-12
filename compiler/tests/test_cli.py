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

for failure in failures:
    print("FAIL " + failure)
print("cli: %s" % ("OK" if not failures else "%d failed" % len(failures)))
sys.exit(1 if failures else 0)
