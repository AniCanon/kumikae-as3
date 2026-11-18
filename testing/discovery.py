"""Discover fixture methods in the compiler's syntax tree and generate a temporary Suite entry."""
from collections import defaultdict
import json
from pathlib import Path
import re
import subprocess


def methods(compiler, path):
    result = subprocess.run([compiler, "parse", "--dump-ast", str(path)], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or "Cannot parse " + str(path))
    package, fixture, method = "", None, None
    for line in result.stdout.splitlines():
        item = line.lstrip()
        indent = len(line) - len(item)
        if method and indent <= method[0]:
            method = None
        if fixture and indent <= fixture[0]:
            fixture = None
        name = re.match(r'(\w+) name="([^"]*)"', item)
        if not name:
            continue
        kind, label = name.groups()
        if kind == "PackageDecl":
            package = label
        elif kind == "ClassDecl":
            fixture = indent, label
        elif fixture and kind == "FunctionDecl" and indent == fixture[0] + 2:
            method = indent, label, "mods=public " in item
        elif method and kind == "Metadata" and label == "Test" and indent == method[0] + 2:
            if not method[2]:
                raise ValueError(f"{path}: [Test] requires a public instance method.")
            yield fixture[1], ".".join(filter(None, [package, fixture[1]])), method[1]


def expand_target(root, target, compiler):
    folder = (root / target["discover"]).resolve()
    if not folder.is_relative_to(root) or not folder.is_dir():
        raise ValueError("Choose a fixture directory inside the project.")
    if "cases" in target:
        raise ValueError("A discovered target cannot also register explicit cases.")
    cases = []
    for path in sorted(folder.rglob("*.as")):
        for suite, qualified, method in methods(compiler, path):
            prefix = target.get("suitePackage", "")
            suite_name = qualified.removeprefix(prefix + ".") if prefix else qualified
            cases.append(dict(source=path.relative_to(root).as_posix(), suite=suite_name, fixture=qualified,
                              method=method, name=method))
    if not cases:
        raise ValueError(f"No [Test] methods found in {target['discover']}.")
    return dict(target, cases=cases, events=True, entryClass="Kas3DiscoveredSuite", marker="PASS discovered suite",
                command=["{compiler}", "run", "{sources}"], source=target["discover"])


def entry(folder, case, root):
    groups = defaultdict(list)
    for index, item in enumerate(case["cases"]):
        groups[item["suite"]].append((index, item))
    adapters = []
    registration = []
    for number, (suite, items) in enumerate(groups.items()):
        fqcn = items[0][1]["fixture"]
        fixture = fqcn.rsplit(".", 1)[-1]
        name = f"Kas3DiscoveredCase{number}"
        body = []
        registration.append("suite = new Suite(" + json.dumps(suite) + ");")
        for index, item in items:
            body.append(f"case {index}: new {fixture}().{item['method']}(); break;")
            registration.append(f"suite.add(new {name}({json.dumps(item['name'])}, {index}));")
        registration.append("if (!suite.run()) ok = false;")
        adapter = folder / (name + ".as")
        imports = ("import " + fqcn + ";") if "." in fqcn else ""
        adapter.write_text("package {\n" + imports +
                           f"\npublic class {name} extends app.anicanon.kumikae.testing.TestCase {{ private var index:int; " +
                           f"public function {name}(label:String, value:int) {{ super(label); index=value; }} " +
                           "override public function run():void { switch(index) {\n" + "\n".join(body) + "\n} } } }")
        adapters.append(str(adapter))
    driver = folder / "Kas3DiscoveredSuite.as"
    driver.write_text("package { import app.anicanon.kumikae.testing.Suite; public class Kas3DiscoveredSuite { "
                      "public function Kas3DiscoveredSuite() { var ok:Boolean=true; var suite:Suite;\n" +
                      "\n".join(registration) + '\nif (!ok) throw new Error("Test failed"); trace("PASS discovered suite"); } } }')
    return [str(driver)] + adapters + sorted({str(root / item["source"]) for item in case["cases"]})
