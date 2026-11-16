"""Version 1 AS3 test manifest runner. Only explicitly configured commands execute."""
import argparse
from collections import namedtuple
import json
import os
from pathlib import Path
import signal
import re
import tempfile
import subprocess
import sys
import time

# This sibling module is shipped together with the runner.
import importlib.util
_protocol_spec = importlib.util.spec_from_file_location("kas3_protocol", Path(__file__).with_name("protocol.py"))
_protocol = importlib.util.module_from_spec(_protocol_spec)
_protocol_spec.loader.exec_module(_protocol)

Result = namedtuple("Result", "passed output duration")


def escape(value):
    result = str(value)
    for original, replacement in [("|", "||"), ("'", "|'"), ("\n", "|n"), ("\r", "|r"), ("[", "|["), ("]", "|]")]:
        result = result.replace(original, replacement)
    return result


def message(event, **attributes):
    print("##teamcity[" + event + " " + " ".join(key + "='" + escape(value) + "'" for key, value in attributes.items()) + "]", flush=True)


def validate(manifest):
    if manifest.get("version") != 1:
        raise ValueError("Unsupported AS3 test manifest version; expected 1")
    tests = manifest.get("tests")
    if not isinstance(tests, list) or not tests:
        raise ValueError("Manifest must register at least one test")
    names = set()
    for case in tests:
        name = case.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise ValueError("Test names must be nonempty and unique")
        if "::" in name or "\n" in name or "\r" in name:
            raise ValueError("Test names must be single-line and cannot contain ::")
        names.add(name)
        command = case.get("command")
        if not isinstance(command, list) or not command or any(not isinstance(arg, str) for arg in command):
            raise ValueError("Each test needs a command argument array")
        if not isinstance(case.get("marker"), str) or not case["marker"]:
            raise ValueError("Each test needs a completion marker")
        timeout = case.get("timeout", 120)
        if not isinstance(timeout, (int, float)) or not 0 < timeout <= 3600:
            raise ValueError("Test timeout must be between 0 and 3600 seconds")
    return tests


def select(tests, names):
    if not names:
        return tests
    registered = {case["name"]: case for case in tests}
    chosen = {}
    for name in names:
        base, separator, child = name.partition("::")
        if base not in registered:
            raise ValueError("Unknown test: " + name)
        case = chosen.setdefault(base, dict(registered[base], _filters=[]))
        if not separator:
            case["_all"] = True
            case["_filters"] = []
        elif not case.get("_all"):
            if not case.get("events") or not case.get("entryClass"):
                raise ValueError("This test does not support individual case selection: " + base)
            case["_filters"].append(child)
    return list(chosen.values())


def stop(process):
    # The owned group can outlive its leader while descendants hold our output pipe.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        if process.poll() is None:
            raise
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        if process.poll() is None:
            raise
    process.wait(timeout=2)


def run_case(root, case):
    start = time.monotonic()
    process = None
    stopped = False
    try:
        process = subprocess.Popen(case["command"], cwd=root, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, start_new_session=True)
        try:
            output, _ = process.communicate(timeout=case.get("timeout", 120))
        except subprocess.TimeoutExpired:
            stop(process)
            stopped = True
            output, _ = process.communicate(timeout=2)
            return Result(False, output + "\nTest timed out", time.monotonic() - start)
        complete = case["marker"] in output.splitlines()
        failed = process.returncode != 0 or not complete
        failed = failed or re.search(r"^(?:ERROR:|SCRIPT ERROR:|Error:|FAIL(?: |$)|.+?:\d+:\d+: error:)", output, re.MULTILINE) is not None
        try:
            events = _protocol.parse(output)
            failed = failed or any(event[0] == "testFailed" for event in events)
            started = set()
            suite_name = ""
            for kind, name, _ in events:
                if kind == "suiteStarted":
                    suite_name = name
                elif kind == "testStarted":
                    started.add(suite_name + "::" + name)
            missing = set(case.get("_filters", [])) - started
            if missing:
                failed = True
                output += "\nRequested cases did not run: " + ", ".join(sorted(missing))
            if case.get("events") and not events:
                failed = True
                output += "\nMissing AS3 suite events"
        except ValueError as error:
            failed = True
            output += "\n" + str(error)
        if process.returncode:
            output += "\nProcess exited with code " + str(process.returncode)
        if not complete:
            output += "\nMissing completion marker: " + case["marker"]
        return Result(not failed, output, time.monotonic() - start)
    except OSError as error:
        return Result(False, str(error), time.monotonic() - start)
    finally:
        if process is not None:
            if not stopped:
                stop(process)
            if process.stdout is not None:
                process.stdout.close()


def expand(root, case, compiler, godot):
    values = {"{compiler}": compiler, "{godot}": godot, "{project}": str(root),
              "{library}": str(Path(__file__).resolve().parent / "as3")}
    sources = set()
    for pattern in case.get("sources", []):
        sources.update(str(path) for path in root.glob(pattern) if path.is_file())
    for pattern in case.get("exclude", []):
        sources.difference_update(str(path) for path in root.glob(pattern))
    arguments = []
    for argument in case["command"]:
        for key, value in values.items():
            argument = argument.replace(key, value)
        arguments.append(argument)
    explicit = {(root / value).resolve() for value in arguments if value.endswith(".as")}
    sources = sorted({str(Path(value).resolve()) for value in sources if Path(value).resolve() not in explicit})
    command = []
    for argument in arguments:
        command.extend(sources if argument == "{sources}" else [argument])
    return dict(case, command=command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--compiler", default="kas3c")
    parser.add_argument("--godot", default="godot")
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    root = args.manifest.resolve().parent
    try:
        tests = select(validate(json.loads(args.manifest.read_text())), args.case)
        if args.list:
            print(json.dumps(tests))
            return 0
        message("testSuiteStarted", name="AS3 Tests")
        failures = 0
        for case in tests:
            location = (root / case.get("source", "")).as_uri()
            structured = case.get("events", False)
            message("testSuiteStarted" if structured else "testStarted", name=case["name"], locationHint=location)
            command_case = expand(root, case, args.compiler, args.godot)
            with tempfile.TemporaryDirectory(prefix="kas3-tests-") as temporary:
                filters = case.get("_filters", [])
                if filters:
                    command = command_case["command"]
                    entry = case["entryClass"]
                    if not re.fullmatch(r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*", entry) or len(command) < 3 or command[1] != "run":
                        raise ValueError("Filtered suites require an AS3 CLI entryClass and compiler run command")
                    bootstrap = Path(temporary) / "Kas3TestSelection.as"
                    statements = "".join("Suite.only.push(" + json.dumps(value, ensure_ascii=True) + ");" for value in filters)
                    bootstrap.write_text("package { import kumikae.testing.Suite; import " + entry + "; public class Kas3TestSelection { public function Kas3TestSelection() {" + statements + "new " + entry.rsplit(".", 1)[-1] + "(); } } }")
                    command_case["command"] = command[:2] + [str(bootstrap)] + command[2:]
                result = run_case(root, command_case)
            if structured:
                try:
                    events = _protocol.parse(result.output)
                except ValueError:
                    events = []
                suite_name = ""
                for kind, name, details in events:
                    if kind == "suiteStarted":
                        suite_name = name
                    full_name = case["name"] + "::" + suite_name + "::" + name if kind.startswith("test") else name
                    event = "testSuiteStarted" if kind == "suiteStarted" else "testSuiteFinished" if kind == "suiteFinished" else kind
                    attributes = {"name": full_name}
                    if kind == "testStarted":
                        attributes["locationHint"] = location
                    if kind == "testFailed":
                        attributes.update(message=details or "Assertion failed", details=details)
                    message(event, **attributes)
                if not result.passed and not any(event[0] == "testFailed" for event in events):
                    message("testStarted", name=case["name"], locationHint=location)
                    message("testFailed", name=case["name"], message="Suite did not complete", details=result.output)
                    message("testFinished", name=case["name"])
                message("testSuiteFinished", name=case["name"])
            else:
                message("testStdOut", name=case["name"], out=result.output)
                if not result.passed:
                    message("testFailed", name=case["name"], message="AS3 test failed", details=result.output)
                message("testFinished", name=case["name"], duration=round(result.duration * 1000))
            failures += int(not result.passed)
        message("testSuiteFinished", name="AS3 Tests")
        print(f"AS3 tests: {len(tests) - failures} passed, {failures} failed", flush=True)
        return int(failures != 0)
    except (ValueError, OSError) as error:
        print("AS3 test runner error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("AS3 tests cancelled", file=sys.stderr)
        sys.exit(130)
