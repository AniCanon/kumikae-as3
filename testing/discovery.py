"""Discover fixture methods in the compiler's syntax tree and generate CLI or Godot Suite drivers."""
from collections import defaultdict
import json
import os
from pathlib import Path
import re
import subprocess


def missing_framework_tiers(root):
    """Find framework tiers the runner must supply, skipping generated Godot and build folders."""
    missing = {"TestCase.as": "as3", "EngineTestRunner.as": "godot"}
    for folder, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if not d.startswith(".") and d != "build"]
        if Path(folder).parts[-4:] == ("app", "anicanon", "kumikae", "testing"):
            for name in files:
                missing.pop(name, None)
        if not missing:
            break
    return missing.values()


def _valid_method(item, label, class_name, require_static=False):
    if label == class_name or " getter " in item or " setter " in item:
        return False
    mods_match = re.search(r"\bmods=([^\s]+)", item)
    mods = set(mods_match.group(1).split(",")) if mods_match else set()
    if "public" not in mods or "native" in mods:
        return False
    is_static = "static" in mods
    return is_static if require_static else not is_static


def _parse_metadata_args(item):
    marker = " args=["
    start = item.find(marker)
    if start < 0:
        return [], {}
    i = start + len(marker)
    positional = []
    keyed = {}
    while i < len(item) and item[i] != "]":
        while i < len(item) and item[i] in " ,":
            i += 1
        if i >= len(item) or item[i] == "]":
            break
        key = None
        if item[i] != '"':
            eq = item.find("=", i)
            if eq < 0:
                break
            key = item[i:eq].strip()
            i = eq + 1
        if i >= len(item) or item[i] != '"':
            break
        i += 1
        chars = []
        while i < len(item) and item[i] != '"':
            if item[i] == "\\" and i + 1 < len(item):
                i += 1
            chars.append(item[i])
            i += 1
        if i < len(item) and item[i] == '"':
            i += 1
        value = "".join(chars)
        if key is None:
            positional.append(value)
        else:
            keyed[key] = value
    return positional, keyed


def _parse_test_options(path, keyed):
    allowed = {"timeout", "expects", "allowEngineErrors"}
    unknown = sorted(set(keyed) - allowed)
    if unknown:
        raise ValueError(f"{path}: Unknown [Test] option(s): {', '.join(unknown)}.")
    timeout = 0
    if "timeout" in keyed:
        try:
            timeout = int(keyed["timeout"])
        except ValueError as error:
            raise ValueError(f"{path}: [Test(timeout=...)] must be a positive integer in milliseconds.") from error
        if timeout <= 0:
            raise ValueError(f"{path}: [Test(timeout=...)] must be a positive integer in milliseconds.")
    expects = keyed.get("expects", "")
    if expects and not re.fullmatch(r"[A-Za-z_]\w*", expects):
        raise ValueError(f"{path}: [Test(expects=...)] must name an error class.")
    allow_engine_errors = False
    if "allowEngineErrors" in keyed:
        raw = keyed["allowEngineErrors"].lower()
        if raw not in ("true", "false"):
            raise ValueError(f"{path}: [Test(allowEngineErrors=...)] must be true or false.")
        allow_engine_errors = raw == "true"
    return timeout, expects, allow_engine_errors


def _ignore_reason(positional, keyed):
    if positional:
        return positional[0]
    if "reason" in keyed:
        return keyed["reason"]
    return ""


_METADATA_TOKENS = ("Test", "Before", "After", "Ignore")


def inspect_file(compiler, path):
    """Return discovered tests and lifecycle hooks for one AS3 file."""
    text = Path(path).read_text(encoding="utf-8", errors="ignore")
    if not any(token in text for token in _METADATA_TOKENS):
        return [], {}
    result = subprocess.run([compiler, "parse", "--dump-ast", str(path)], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or "Cannot parse " + str(path))
    package = ""
    fixture = None
    method = None
    discovered = []
    hooks = defaultdict(lambda: {"before": [], "after": [], "beforeClass": [], "afterClass": []})

    def flush_method():
        if not method:
            return
        fqcn = method["qualified"]
        if method["before"]:
            if not method["valid_instance"] or method["has_params"]:
                raise ValueError(f"{path}: [Before] requires a public no-argument instance method.")
            hooks[fqcn]["before"].append(method["name"])
        if method["after"]:
            if not method["valid_instance"] or method["has_params"]:
                raise ValueError(f"{path}: [After] requires a public no-argument instance method.")
            hooks[fqcn]["after"].append(method["name"])
        if method["before_class"]:
            if not method["valid_static"] or method["has_params"]:
                raise ValueError(f"{path}: [BeforeClass] requires a public static no-argument method.")
            hooks[fqcn]["beforeClass"].append(method["name"])
        if method["after_class"]:
            if not method["valid_static"] or method["has_params"]:
                raise ValueError(f"{path}: [AfterClass] requires a public static no-argument method.")
            hooks[fqcn]["afterClass"].append(method["name"])
        if method["annotated"]:
            if not method["valid_instance"] or method["has_params"]:
                raise ValueError(f"{path}: [Test] requires a public no-argument instance method.")
            ignored = method["ignored"] or fixture["ignored"]
            reason = method["ignore_reason"] if method["ignored"] else fixture["ignore_reason"]
            discovered.append({
                "fixture": method["fixture"],
                "qualified": fqcn,
                "name": method["name"],
                "ignored": ignored,
                "ignoreReason": reason,
                "timeoutMs": method["timeout"],
                "expects": method["expects"],
                "allowEngineErrors": method["allow_engine_errors"],
            })

    for line in result.stdout.splitlines():
        item = line.lstrip()
        indent = len(line) - len(item)
        if method and indent <= method["indent"]:
            flush_method()
            method = None
        if fixture and indent <= fixture["indent"]:
            fixture = None
        name = re.match(r'(\w+) name="([^"]*)"', item)
        if not name:
            continue
        kind, label = name.groups()
        if kind == "PackageDecl":
            package = label
        elif kind == "ClassDecl":
            fixture = {
                "indent": indent,
                "name": label,
                "qualified": ".".join(filter(None, [package, label])),
                "ignored": False,
                "ignore_reason": "",
            }
        elif fixture and kind == "Metadata" and indent == fixture["indent"] + 2:
            if label == "Ignore":
                pos, keyed = _parse_metadata_args(item)
                fixture["ignored"] = True
                fixture["ignore_reason"] = _ignore_reason(pos, keyed)
        elif fixture and kind == "FunctionDecl" and indent == fixture["indent"] + 2:
            method = {
                "indent": indent,
                "name": label,
                "fixture": fixture["name"],
                "qualified": fixture["qualified"],
                "valid_instance": _valid_method(item, label, fixture["name"], require_static=False),
                "valid_static": _valid_method(item, label, fixture["name"], require_static=True),
                "annotated": False,
                "has_params": False,
                "ignored": False,
                "ignore_reason": "",
                "timeout": 0,
                "expects": "",
                "allow_engine_errors": False,
                "before": False,
                "after": False,
                "before_class": False,
                "after_class": False,
            }
        elif method and indent == method["indent"] + 2:
            if kind == "Metadata":
                pos, keyed = _parse_metadata_args(item)
                if label == "Test":
                    timeout, expects, allow_engine = _parse_test_options(path, keyed)
                    method["annotated"] = True
                    method["timeout"] = timeout
                    method["expects"] = expects
                    method["allow_engine_errors"] = allow_engine
                elif label == "Ignore":
                    method["ignored"] = True
                    method["ignore_reason"] = _ignore_reason(pos, keyed)
                elif label == "Before":
                    method["before"] = True
                elif label == "After":
                    method["after"] = True
                elif label == "BeforeClass":
                    method["before_class"] = True
                elif label == "AfterClass":
                    method["after_class"] = True
            elif kind == "Parameter":
                method["has_params"] = True
    flush_method()
    return discovered, dict(hooks)


def methods(compiler, path):
    discovered, _ = inspect_file(compiler, path)
    for item in discovered:
        yield item["fixture"], item["qualified"], item["name"]


def expand_target(root, target, compiler):
    root = Path(root).resolve()
    folder = (root / target["discover"]).resolve()
    if not folder.is_relative_to(root) or not folder.is_dir():
        raise ValueError("Choose a fixture directory inside the project.")
    if "cases" in target:
        raise ValueError("A discovered target cannot also register explicit cases.")
    runtime = target.get("runtime", "cli")
    if runtime not in ("cli", "godot", "ios-simulator", "android", "web"):
        raise ValueError("Test runtime must be 'cli', 'godot', 'ios-simulator', 'android', or 'web'.")
    headless = target.get("headless", True)
    if not isinstance(headless, bool):
        raise ValueError("Test headless option must be true or false.")
    cases = []
    all_hooks = {}
    for path in sorted(folder.rglob("*.as")):
        discovered, file_hooks = inspect_file(compiler, path)
        for fqcn, hook_set in file_hooks.items():
            all_hooks[fqcn] = hook_set
        for item in discovered:
            prefix = target.get("suitePackage", "")
            qualified = item["qualified"]
            suite_name = qualified.removeprefix(prefix + ".") if prefix else qualified
            entry_case = dict(
                source=path.relative_to(root).as_posix(),
                suite=suite_name,
                fixture=qualified,
                method=item["name"],
                name=item["name"],
            )
            if item["ignored"]:
                entry_case["ignored"] = True
                entry_case["ignoreReason"] = item["ignoreReason"]
            if item["timeoutMs"]:
                entry_case["timeoutMs"] = item["timeoutMs"]
            if item["expects"]:
                entry_case["expects"] = item["expects"]
            if item["allowEngineErrors"]:
                entry_case["allowEngineErrors"] = True
            cases.append(entry_case)
    if not cases:
        raise ValueError(f"No [Test] methods found in {target['discover']}.")
    if runtime in ("godot", "ios-simulator", "android", "web"):
        command = ["{godot}"]
        if runtime == "godot" and headless:
            command.append("--headless")
        if runtime == "godot":
            command.extend(["--path", "{project}", "--script", "{driver}"])
        return dict(
            target,
            runtime=runtime,
            headless=headless,
            cases=cases,
            _hooks=all_hooks,
            events=True,
            entryClass="Kas3EngineDriver",
            marker="PASS discovered suite",
            command=command,
            source=target["discover"],
        )
    return dict(
        target,
        runtime="cli",
        cases=cases,
        _hooks=all_hooks,
        events=True,
        entryClass="Kas3DiscoveredSuite",
        marker="PASS discovered suite",
        command=["{compiler}", "run", "{sources}"],
        source=target["discover"],
    )


def _generate_adapters(folder, case):
    groups = defaultdict(list)
    seen_suites = {}
    for index, item in enumerate(case["cases"]):
        suite = item["suite"]
        fqcn = item["fixture"]
        if suite in seen_suites and seen_suites[suite] != fqcn:
            raise ValueError(f"Duplicate suite name '{suite}' across {seen_suites[suite]} and {fqcn}.")
        seen_suites[suite] = fqcn
        groups[(suite, fqcn)].append((index, item))
    hooks_by_class = case.get("_hooks", {})
    adapters = []
    suite_setup = []
    for number, ((suite, fqcn), items) in enumerate(groups.items()):
        fixture = fqcn.rsplit(".", 1)[-1]
        class_hooks = hooks_by_class.get(fqcn, {})
        before_calls = "".join(f"instance.{name}();\n" for name in class_hooks.get("before", []))
        after_calls = "".join(f"instance.{name}();\n" for name in class_hooks.get("after", []))
        before_class_calls = "".join(f"{fqcn}.{name}(); " for name in class_hooks.get("beforeClass", []))
        after_class_calls = "".join(f"{fqcn}.{name}(); " for name in class_hooks.get("afterClass", []))
        adapter_name = f"Kas3DiscoveredCase{number}"
        cases_switch = []
        setup_lines = [f"suite = new Suite({json.dumps(suite)});"]
        if before_class_calls:
            setup_lines.append(f"suite.beforeClass = function():void {{ {before_class_calls}}};")
        if after_class_calls:
            setup_lines.append(f"suite.afterClass = function():void {{ {after_class_calls}}};")
        for index, item in items:
            cases_switch.append(f"case {index}: instance.{item['method']}(); break;")
            var_name = f"testCase{index}"
            setup_lines.append(f"var {var_name}:{adapter_name} = new {adapter_name}({json.dumps(item['name'])}, {index});")
            if item.get("ignored"):
                setup_lines.append(f"{var_name}.ignored = true;")
                if item.get("ignoreReason"):
                    setup_lines.append(f"{var_name}.ignoreReason = {json.dumps(item['ignoreReason'])};")
            if item.get("expects"):
                setup_lines.append(f"{var_name}.expectedError = {json.dumps(item['expects'])};")
            if item.get("timeoutMs"):
                setup_lines.append(f"{var_name}.timeoutMs = {int(item['timeoutMs'])};")
            if item.get("allowEngineErrors"):
                setup_lines.append(f"{var_name}.allowEngineErrors = true;")
            setup_lines.append(f"suite.add({var_name});")
        imports = ("import " + fqcn + ";\n") if "." in fqcn else ""
        adapter_code = (
            "package {\n"
            + imports
            + f"public class {adapter_name} extends app.anicanon.kumikae.testing.TestCase {{\n"
            + "    private var index:int;\n"
            + f"    private var instance:{fixture};\n"
            + f"    public function {adapter_name}(label:String, value:int) {{ super(label); index = value; }}\n"
            + f"    override public function setUp():void {{ instance = new {fixture}();\n{before_calls}    }}\n"
            + "    override public function run():void {\n"
            + "        switch (index) {\n"
            + "\n".join("            " + line for line in cases_switch)
            + "\n        }\n    }\n"
            + f"    override public function tearDown():void {{\n        if (instance != null) {{\n{after_calls}        }}\n        instance = null;\n    }}\n"
            + "}\n}\n"
        )
        adapter_path = folder / f"{adapter_name}.as"
        adapter_path.write_text(adapter_code)
        adapters.append(str(adapter_path))
        suite_setup.append(("", setup_lines))
    return adapters, suite_setup


def entry(folder, case, root):
    adapters, suite_setup = _generate_adapters(folder, case)
    imports = sorted({imp for imp, _ in suite_setup if imp})
    registration = []
    for _, lines in suite_setup:
        registration.extend(lines)
        registration.append("if (!suite.run()) ok = false;")
    driver = folder / "Kas3DiscoveredSuite.as"
    driver.write_text(
        "package {\n"
        + "import app.anicanon.kumikae.testing.Suite;\n"
        + "\n".join(imports)
        + "\npublic class Kas3DiscoveredSuite {\n"
        + "    public function Kas3DiscoveredSuite() {\n"
        + "        var ok:Boolean = true;\n"
        + "        var suite:Suite;\n"
        + "\n".join("        " + line for line in registration)
        + '\n        if (!ok) throw new Error("Test failed");\n'
        + '        trace("PASS discovered suite");\n'
        + "    }\n}\n}\n"
    )
    return [str(driver)] + adapters + sorted({str(root / item["source"]) for item in case["cases"]})


def godot_entry(folder, case, filters=None):
    """Generate EngineTestRunner subclass, adapters, and SceneTree bootstrap script in a temp folder."""
    folder = folder.resolve()
    adapters, suite_setup = _generate_adapters(folder, case)
    imports = sorted({imp for imp, _ in suite_setup if imp})
    registration = []
    for value in (filters or []):
        registration.append(f"Suite.only.push({json.dumps(value, ensure_ascii=True)});")
    for _, lines in suite_setup:
        registration.extend(lines)
        registration.append("addSuite(suite);")
    driver = folder / "Kas3EngineDriver.as"
    driver.write_text(
        "package {\n"
        + "import app.anicanon.kumikae.testing.EngineTestRunner;\n"
        + "import app.anicanon.kumikae.testing.Suite;\n"
        + "\n".join(imports)
        + "\npublic class Kas3EngineDriver extends EngineTestRunner {\n"
        + "    override protected function registerSuites():void {\n"
        + "        var suite:Suite;\n"
        + "\n".join("        " + line for line in registration)
        + "\n    }\n}\n}\n"
    )
    bootstrap = folder / "run_tests.gd"
    bootstrap.write_text(
        "extends SceneTree\n\n"
        "func _initialize():\n"
        f"\tvar script = ResourceLoader.load({json.dumps(str(driver.resolve()))})\n"
        "\tif script == null or not script.can_instantiate():\n"
        "\t\tquit(1)\n"
        "\t\treturn\n"
        "\tvar runner = Node.new()\n"
        "\trunner.set_script(script)\n"
        "\troot.add_child(runner)\n"
    )
    return bootstrap, driver, adapters
