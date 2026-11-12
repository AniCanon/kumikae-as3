"""Golden-file tests for kas3c.

Usage: run_golden.py <path to kas3c> <tests dir> [--update]

  golden/ok/X.as      must parse cleanly; `--dump-ast` output must equal X.ast
  golden/errors/X.as  must fail; the diagnostics must equal X.diag
  corpus/X.as         must parse cleanly

  golden/names/ok/X.as      `check --dump-names` must be clean and equal X.names
  golden/names/errors/X.as  `check` must fail; the diagnostics must equal X.diag
  golden/types/ok/X.as      `check --dump-types` must be clean and equal X.types
  golden/types/errors/X.as  `check` must fail; the diagnostics must equal X.diag
  corpus/*.as               checked together as one program, must be clean
  Name checks are given stubs/godot.as, a stand-in for Godot's classes, as a declaration file.

--update rewrites the .ast and .diag files from what kas3c prints now. Read the result before committing it:
a golden file that records a bug is worse than no test.
"""
import difflib
import glob
import os
import subprocess
import sys

kas3c = os.path.abspath(sys.argv[1])
tests = os.path.abspath(sys.argv[2])
update = "--update" in sys.argv[3:]
passed = 0
failed = []


def run_check(*arguments):
    return subprocess.run([kas3c, "check", "--declarations", "stubs/godot.as", *arguments], capture_output=True,
                          text=True, cwd=tests, timeout=60)


def run(relative):
    # Run from the tests directory so that paths in diagnostics are short and stable.
    return subprocess.run([kas3c, "parse", "--dump-ast", relative], capture_output=True, text=True, cwd=tests,
                          timeout=60)


def compare(name, actual, expected_path):
    global passed
    if update:
        with open(expected_path, "w") as handle:
            handle.write(actual)
        passed += 1
        return
    expected = open(expected_path).read() if os.path.exists(expected_path) else None
    if expected == actual:
        passed += 1
        return
    diff = "".join(difflib.unified_diff((expected or "").splitlines(True), actual.splitlines(True),
                                        "expected", "actual"))
    failed.append("%s\n%s" % (name, diff if expected is not None else "  no expected file: run with --update"))


def relative_paths(pattern):
    return sorted(os.path.relpath(path, tests) for path in glob.glob(os.path.join(tests, pattern)))


for source in relative_paths("golden/ok/*.as"):
    result = run(source)
    if result.returncode != 0 or result.stderr:
        failed.append("%s: expected a clean parse, got exit %d\n%s" % (source, result.returncode, result.stderr))
        continue
    compare(source, result.stdout, os.path.join(tests, source[:-3] + ".ast"))

for source in relative_paths("golden/errors/*.as"):
    result = run(source)
    if result.returncode != 1:
        failed.append("%s: expected exit 1, got %d\n%s" % (source, result.returncode, result.stderr))
        continue
    compare(source, result.stderr, os.path.join(tests, source[:-3] + ".diag"))

for source in relative_paths("corpus/*.as"):
    result = run(source)
    if result.returncode != 0 or result.stderr:
        failed.append("%s: expected a clean parse, got exit %d\n%s" % (source, result.returncode, result.stderr))
    else:
        passed += 1

for source in relative_paths("golden/names/ok/*.as"):
    result = run_check("--dump-names", source)
    if result.returncode != 0 or result.stderr:
        failed.append("%s: expected a clean check, got exit %d\n%s" % (source, result.returncode, result.stderr))
        continue
    compare(source, result.stdout, os.path.join(tests, source[:-3] + ".names"))

for source in relative_paths("golden/names/errors/*.as"):
    result = run_check(source)
    if result.returncode != 1:
        failed.append("%s: expected exit 1, got %d\n%s" % (source, result.returncode, result.stderr))
        continue
    compare(source, result.stderr, os.path.join(tests, source[:-3] + ".diag"))

for source in relative_paths("golden/types/ok/*.as"):
    result = run_check("--dump-types", source)
    if result.returncode != 0 or result.stderr:
        failed.append("%s: expected a clean check, got exit %d\n%s" % (source, result.returncode, result.stderr))
        continue
    compare(source, result.stdout, os.path.join(tests, source[:-3] + ".types"))

for source in relative_paths("golden/types/errors/*.as"):
    result = run_check(source)
    if result.returncode != 1:
        failed.append("%s: expected exit 1, got %d\n%s" % (source, result.returncode, result.stderr))
        continue
    compare(source, result.stderr, os.path.join(tests, source[:-3] + ".diag"))

result = run_check(*relative_paths("corpus/*.as"))
if result.returncode != 0 or result.stderr:
    failed.append("corpus: expected a clean check, got exit %d\n%s" % (result.returncode, result.stderr))
else:
    passed += 1

for failure in failed:
    print("FAIL " + failure)
print("%d passed, %d failed" % (passed, len(failed)))
sys.exit(1 if failed else 0)
