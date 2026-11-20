"""Version 1 AS3 test manifest runner with live streaming and CLI/Godot runtime support."""
import argparse
from collections import namedtuple
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time

# These sibling modules are shipped together with the runner.
_protocol_spec = importlib.util.spec_from_file_location("kas3_protocol", Path(__file__).with_name("protocol.py"))
_protocol = importlib.util.module_from_spec(_protocol_spec)
_protocol_spec.loader.exec_module(_protocol)

_mapping_spec = importlib.util.spec_from_file_location("kas3_mapping", Path(__file__).with_name("case_mapping.py"))
_mapping = importlib.util.module_from_spec(_mapping_spec)
_mapping_spec.loader.exec_module(_mapping)

_discovery_spec = importlib.util.spec_from_file_location("kas3_discovery", Path(__file__).with_name("discovery.py"))
_discovery = importlib.util.module_from_spec(_discovery_spec)
_discovery_spec.loader.exec_module(_discovery)

Result = namedtuple("Result", "passed output duration")

ERROR_LINE = re.compile(
    r"^(?:ERROR:|SCRIPT ERROR:|(?:[A-Z][A-Za-z]*)?Error:|FAIL(?: |$)|.+?:\d+:\d+: error:).*",
    re.MULTILINE,
)
PRIMARY_DIAGNOSTIC = re.compile(
    r"^(?:SCRIPT ERROR:|(?:[A-Z][A-Za-z]*)?Error:|FAIL(?: |$)|.+?:\d+:\d+: error:).*",
    re.MULTILINE,
)
ENGINE_ERROR_HEADER = re.compile(r"^(?:ERROR:|SCRIPT ERROR:)")
ENGINE_TRACE_LINE = re.compile(r"^(?:\s+(?:at:|\[\d+\])|GDScript backtrace\b)")


def process_failure(output):
    diagnostic = PRIMARY_DIAGNOSTIC.search(output) or ERROR_LINE.search(output)
    if diagnostic:
        return diagnostic.group().strip()
    lines = output.rstrip().splitlines()
    return lines[-1].strip() if lines else "Test failed without output"


def summary(details):
    """A failure's one line for the results tree: the first of its details that says anything."""
    return next((line.strip() for line in details.splitlines() if line.strip()), "Assertion failed")


def escape(value):
    result = str(value)
    for original, replacement in [("|", "||"), ("'", "|'"), ("\n", "|n"), ("\r", "|r"), ("[", "|["), ("]", "|]")]:
        result = result.replace(original, replacement)
    return result


def message(event, **attributes):
    print(
        "##teamcity["
        + event
        + " "
        + " ".join(key + "='" + escape(value) + "'" for key, value in attributes.items())
        + "]",
        flush=True,
    )


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
        timeout = case.get("timeout", 120)
        if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or not 0 < timeout <= 3600:
            raise ValueError("Test timeout must be between 0 and 3600 seconds")
        if "discover" in case:
            if not isinstance(case["discover"], str) or not case["discover"]:
                raise ValueError("Choose a fixture directory inside the project.")
            runtime = case.get("runtime", "cli")
            if runtime not in ("cli", "godot", "ios-simulator", "android"):
                raise ValueError("Test runtime must be 'cli', 'godot', 'ios-simulator', or 'android'.")
            if "headless" in case and not isinstance(case["headless"], bool):
                raise ValueError("Test headless option must be true or false.")
            continue
        _mapping.validate(case)
        command = case.get("command")
        if not isinstance(command, list) or not command or any(not isinstance(arg, str) for arg in command):
            raise ValueError("Each test needs a command argument array")
        if not isinstance(case.get("marker"), str) or not case["marker"]:
            raise ValueError("Each test needs a completion marker")
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
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass


class _StreamReporter:
    """Streams @kas3-test-v1/v2 events and per-test stdout/stderr to TeamCity in real time."""

    def __init__(self, root, case):
        self.root = root
        self.case = case
        self.suite_name = ""
        self.active = None
        self.passes = 0
        self.failures = 0
        self.ignored = 0
        self.started_any_suite = False
        self.outside_errors = []
        self._capturing_outside = False

    def on_event(self, event):
        self._capturing_outside = False
        kind = event.kind
        if kind == "suiteStarted":
            self.started_any_suite = True
            self.suite_name = event.name
            message("testSuiteStarted", name=self.suite_name)
        elif kind == "testStarted":
            self.active = {
                "name": event.name,
                "allow_engine_errors": bool(event.meta.get("allowEngineErrors", False)),
                "engine_errors": [],
                "capturing_trace": False,
                "failed": False,
                "started_at": time.monotonic(),
            }
            location = _mapping.location(self.root, self.case, self.suite_name, event.name)
            message("testStarted", name=event.name, locationHint=location)
        elif kind == "testIgnored":
            self.ignored += 1
            attrs = {"name": event.name}
            reason = event.meta.get("reason", "")
            if reason:
                attrs["message"] = reason
            message("testIgnored", **attrs)
        elif kind == "testFailed":
            if self.active is not None:
                self.active["failed"] = True
            self.failures += 1
            attrs = {
                "name": event.name,
                "message": summary(event.details),
                "details": event.details,
            }
            if "expected" in event.meta and "actual" in event.meta:
                attrs.update(
                    type="comparisonFailure",
                    expected=event.meta["expected"],
                    actual=event.meta["actual"],
                )
            message("testFailed", **attrs)
        elif kind == "testFinished":
            if self.active is not None:
                if (
                    self.active["engine_errors"]
                    and not self.active["allow_engine_errors"]
                    and not self.active["failed"]
                ):
                    self.active["failed"] = True
                    self.failures += 1
                    detail = "\n".join(self.active["engine_errors"])
                    message("testFailed", name=event.name, message=summary(detail), details=detail)
                if not self.active["failed"]:
                    self.passes += 1
                elapsed = round((time.monotonic() - self.active["started_at"]) * 1000)
                self.active = None
            else:
                elapsed = 0
            duration = event.meta.get("duration")
            message("testFinished", name=event.name, duration=int(elapsed if duration is None else duration))
        elif kind == "suiteFinished":
            message("testSuiteFinished", name=self.suite_name)
            self.suite_name = ""

    def on_output(self, line):
        marker = self.case.get("marker", "")
        if line == marker or line.startswith("KAS3 SUITE COMPLETE "):
            return
        is_error = ENGINE_ERROR_HEADER.match(line) is not None
        is_trace = ENGINE_TRACE_LINE.match(line) is not None
        if self.active is not None:
            if is_error or (self.active["capturing_trace"] and is_trace):
                self.active["capturing_trace"] = True
                self.active["engine_errors"].append(line)
                message("testStdErr", name=self.active["name"], out=line + "\n")
            else:
                self.active["capturing_trace"] = False
                message("testStdOut", name=self.active["name"], out=line + "\n")
        elif self.started_any_suite:
            if is_error or (self._capturing_outside and is_trace):
                self._capturing_outside = True
                self.outside_errors.append(line)
            else:
                self._capturing_outside = False

    def close_aborted(self, output):
        """Close any open test/suite node when a process crashes or times out mid-run."""
        if self.active is not None:
            if not self.active["failed"]:
                self.failures += 1
                detail = "\n".join(self.active["engine_errors"]) if self.active["engine_errors"] else output
                message("testFailed", name=self.active["name"], message=process_failure(detail), details=detail)
            elapsed = round((time.monotonic() - self.active["started_at"]) * 1000)
            message("testFinished", name=self.active["name"], duration=int(elapsed))
            self.active = None
        if self.suite_name:
            message("testSuiteFinished", name=self.suite_name)
            self.suite_name = ""


def run_case(root, case, env=None, reporter=None):
    start = time.monotonic()
    process = None
    stopped = False
    timed_out = threading.Event()
    timer = None
    chunks = []
    events = []
    parser = _protocol.StreamParser()
    protocol_error = None
    try:
        process = subprocess.Popen(
            case["command"],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            start_new_session=True,
            env=env,
        )

        def on_timeout():
            timed_out.set()
            stop(process)

        timer = threading.Timer(case.get("timeout", 120), on_timeout)
        timer.daemon = True
        timer.start()

        for raw_line in process.stdout:
            chunks.append(raw_line)
            if protocol_error is None:
                try:
                    for item_kind, item in parser.feed(raw_line):
                        if item_kind == "event":
                            events.append(item)
                            if reporter is not None:
                                reporter.on_event(item)
                        elif reporter is not None:
                            reporter.on_output(item)
                except ValueError as error:
                    protocol_error = error
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            stop(process)
            stopped = True
        if timer is not None:
            timer.cancel()
        output = "".join(chunks)
        if timed_out.is_set():
            stopped = True
            return Result(False, output + "\nTest timed out", time.monotonic() - start)
        if protocol_error is None:
            try:
                for item_kind, item in parser.finish():
                    if item_kind == "output" and reporter is not None:
                        reporter.on_output(item)
            except ValueError as error:
                protocol_error = error

        complete = case["marker"] in output.splitlines()
        failed = process.returncode != 0 or not complete
        structured = bool(case.get("events"))
        if not structured:
            failed = failed or ERROR_LINE.search(output) is not None
        elif reporter is not None:
            failed = failed or reporter.failures > 0 or bool(reporter.outside_errors)
            if not events:
                failed = failed or ERROR_LINE.search(output) is not None
        else:
            failed = failed or ERROR_LINE.search(output) is not None

        if protocol_error is not None:
            failed = True
            output += "\n" + str(protocol_error)
        else:
            failed = failed or any(event.kind == "testFailed" for event in events)
            seen = set()
            suite_name = ""
            for event in events:
                if event.kind == "suiteStarted":
                    suite_name = event.name
                elif event.kind in ("testStarted", "testIgnored"):
                    seen.add(suite_name + "::" + event.name)
            missing = set(case.get("_filters", [])) - seen
            if missing:
                failed = True
                output += "\nRequested cases did not run: " + ", ".join(sorted(missing))
            if structured and not events:
                failed = True
                output += "\nMissing AS3 suite events"
        if process.returncode:
            output += "\nProcess exited with code " + str(process.returncode)
        if not complete:
            output += "\nMissing completion marker: " + case["marker"]
        return Result(not failed, output, time.monotonic() - start)
    except OSError as error:
        return Result(False, str(error), time.monotonic() - start)
    finally:
        if timer is not None:
            timer.cancel()
        if process is not None:
            if not stopped:
                stop(process)
            if process.stdout is not None:
                process.stdout.close()


def expand(root, case, compiler, godot):
    values = {
        "{compiler}": compiler,
        "{godot}": godot,
        "{project}": str(root),
        "{library}": str(Path(__file__).resolve().parent / "as3"),
    }
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


def _count_planned_cases(tests):
    total = 0
    for case in tests:
        filters = case.get("_filters")
        if filters:
            total += len(filters)
        elif case.get("cases"):
            total += len(case["cases"])
        else:
            total += 1
    return total


def _godot_env(root, temp_dir, native):
    here = Path(__file__).resolve().parent
    extra_dirs = [str(temp_dir.resolve())]
    extra_dirs.extend(str((here / tier).resolve()) for tier in _discovery.missing_framework_tiers(root))
    env = dict(os.environ)
    env["KAS3_TESTS"] = "1"
    env["KAS3_NATIVE_PLAY"] = "1" if native else "0"
    env["KAS3_TEST_SOURCES"] = os.pathsep.join(extra_dirs)
    return env


def _strip_internal_fields(tests):
    return [{k: v for k, v in case.items() if not k.startswith("_")} for case in tests]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--compiler", default="kas3c")
    parser.add_argument("--godot", default="godot")
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--require-pass", action="store_true", help="Fail if no test actually passed")
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--device", default="")
    parser.add_argument("--preset", default="")
    parser.add_argument("--runtime", choices=("godot", "ios-simulator", "android"),
                        help="Run the selected discovery fixtures on this engine target")
    parser.add_argument("--android-sdk", default="")
    args = parser.parse_args()
    root = args.manifest.resolve().parent
    try:
        manifest = json.loads(args.manifest.read_text())
        registered = validate(manifest)
        selected_names = {value.partition("::")[0] for value in args.case}
        for index, target in enumerate(registered):
            if selected_names and target["name"] not in selected_names:
                continue
            if args.runtime:
                if "discover" not in target:
                    raise ValueError("--runtime requires a discovery target. Select one with --case.")
                target = dict(target, runtime=args.runtime)
            if "discover" in target:
                manifest["tests"][index] = _discovery.expand_target(root, target, args.compiler)
                _mapping.validate(manifest["tests"][index])
        tests = select(registered, args.case)
        if args.list:
            print(json.dumps(_strip_internal_fields(tests)))
            return 0
        message("testSuiteStarted", name="AS3 Tests")
        message("testCount", count=_count_planned_cases(tests))
        passes = failures = ignored = 0
        for case in tests:
            location = (root / case.get("source", "")).as_uri()
            structured = case.get("events", False)
            message("testSuiteStarted" if structured else "testStarted", name=case["name"], locationHint=location)
            command_case = expand(root, case, args.compiler, args.godot)
            run_env = None
            with tempfile.TemporaryDirectory(prefix="kas3-tests-") as temporary:
                temp_path = Path(temporary).resolve()
                filters = case.get("_filters", [])
                if "discover" in case and case.get("runtime") in ("ios-simulator", "android"):
                    payload_file = temp_path / "mobile-case.json"
                    payload_file.write_text(json.dumps({"case": case, "filters": filters}))
                    mobile_cmd = [
                        sys.executable,
                        "-u",
                        str(Path(__file__).resolve().with_name("mobile_runner.py")),
                        case["runtime"],
                        str(root),
                        str(payload_file),
                        "--godot",
                        args.godot,
                    ]
                    if args.device:
                        mobile_cmd.extend(["--device", args.device])
                    if args.preset or case.get("preset"):
                        mobile_cmd.extend(["--preset", args.preset or case["preset"]])
                    if args.android_sdk:
                        mobile_cmd.extend(["--android-sdk", args.android_sdk])
                    command_case["command"] = mobile_cmd
                elif "discover" in case and case.get("runtime") == "godot":
                    bootstrap, _, _ = _discovery.godot_entry(temp_path, case, filters=filters)
                    command_case["command"] = [
                        str(bootstrap) if arg == "{driver}" else arg for arg in command_case["command"]
                    ]
                    run_env = _godot_env(root, temp_path, args.native)
                else:
                    if "discover" in case:
                        generated = _discovery.entry(temp_path, case, root)
                        command = command_case["command"]
                        files = list(dict.fromkeys(generated + command[2:]))
                        if not any(
                            Path(file).as_posix().endswith("/app/anicanon/kumikae/testing/TestCase.as")
                            for file in files
                        ):
                            files.extend(
                                sorted(str(path) for path in (Path(__file__).resolve().parent / "as3").rglob("*.as"))
                            )
                        command_case["command"] = command[:2] + files
                    if filters:
                        command = command_case["command"]
                        entry = case["entryClass"]
                        if (
                            not re.fullmatch(r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*", entry)
                            or len(command) < 3
                            or command[1] != "run"
                        ):
                            raise ValueError("Filtered suites require an AS3 CLI entryClass and compiler run command")
                        bootstrap = temp_path / "Kas3TestSelection.as"
                        statements = "".join(
                            "Suite.only.push(" + json.dumps(value, ensure_ascii=True) + ");" for value in filters
                        )
                        bootstrap.write_text(
                            "package { import app.anicanon.kumikae.testing.Suite; import "
                            + entry
                            + "; public class Kas3TestSelection { public function Kas3TestSelection() {"
                            + statements
                            + "new "
                            + entry.rsplit(".", 1)[-1]
                            + "(); } } }"
                        )
                        command_case["command"] = command[:2] + [str(bootstrap)] + command[2:]
                reporter = _StreamReporter(root, case) if structured else None
                result = run_case(root, command_case, env=run_env, reporter=reporter)
            if structured:
                reporter.close_aborted(result.output)
                passes += reporter.passes
                failures += reporter.failures
                ignored += reporter.ignored
                if not result.passed and reporter.failures == 0:
                    failures += 1
                    message("testStarted", name=case["name"], locationHint=location)
                    message(
                        "testFailed",
                        name=case["name"],
                        message=process_failure(result.output),
                        details=result.output,
                    )
                    message("testFinished", name=case["name"])
                message("testSuiteFinished", name=case["name"])
            else:
                message("testStdOut", name=case["name"], out=result.output)
                if not result.passed:
                    message(
                        "testFailed",
                        name=case["name"],
                        message=process_failure(result.output),
                        details=result.output,
                    )
                message("testFinished", name=case["name"], duration=round(result.duration * 1000))
                passes += int(result.passed)
                failures += int(not result.passed)
        message("testSuiteFinished", name="AS3 Tests")
        summary_line = f"AS3 tests: {passes} passed, {failures} failed"
        if ignored:
            summary_line += f", {ignored} ignored"
        print(summary_line, flush=True)
        return int(failures != 0 or (args.require_pass and passes == 0))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print("AS3 test runner error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("AS3 tests cancelled", file=sys.stderr)
        sys.exit(130)
