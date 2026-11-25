"""Build isolated inference mutations and compare IR and program output with the committed oracles."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import shutil

COMPILER = Path(__file__).resolve().parents[1]
RUN = COMPILER / "tests/run"
USES = "typer_local_uses.cpp"
STORAGE = "typer_storage.cpp"
# Each substitution removes one proof or introduces one incorrect assumption about storage.
CONTROLS = {
    "peer-operands": (USES, "return at == assumptions.end() ? p_expr->type : at->second;",
                      "(void)at; return p_expr->type;", ["LocalOperands.ir"]),
    "arithmetic": (USES, "const Type *result = binary_result(binary, p_expr, p_type);",
                   "const Type *result = nullptr;", ["LocalArithmetic.ir", "godot/LocalStorage.ir"]),
    "comparison": (USES, "return boolean;\n\t\t\tdefault:", "return nullptr;\n\t\t\tdefault:", ["LocalArithmetic.ir"]),
    "condition": (USES, "case NodeKind::If: case NodeKind::While:", "case NodeKind::If: return false; case NodeKind::While:",
                  ["LocalArithmetic.ir"]),
    "argument": (USES, "return argument(static_cast<Call *>(use), p_expr, p_type);", "return false;",
                 ["LocalArithmetic.ir"]),
    "member": (USES, "if (p_consumers.receives(access, result))", "if (false && p_consumers.receives(access, result))",
               ["LocalArithmetic.ir"]),
    "property-writes": (USES, "if (binding.member->kind != SymbolKind::Property)",
                        "if (binding.operation != binding.access || binding.member->kind != SymbolKind::Property)",
                        ["godot/Configure.ir"]),
    "escape-prefix": (USES, "safe = bindings.size() == p_uses.size();",
                      "safe = bindings.size() == p_uses.size(); if (!safe) return {};", ["godot/Configure.ir"]),
    "narrow-storage": (USES, "p_local.symbol->storage = p_local.type;", "p_local.symbol->storage = p_local.initial->type;",
                       ["LocalArithmetic.out", "LocalOperands.out"]),
    "conflicting-write": (STORAGE, "stored_type(assign->value) != it->second.value->type",
                          "false", ["dynamic_local_tags.out"]),
}


def build(folder):
    subprocess.run(["cmake", "--build", folder, "--target", "kas3c", "-j4"], check=True)


def observe(binary, oracle):
    path = RUN / oracle
    ir = path.suffix == ".ir"
    args = ["ir" if ir else "run"]
    if path.parent.name == "godot":
        args.append("--godot")
    result = subprocess.run([binary, *args, path.with_suffix(".as").name], cwd=path.parent,
                            text=True, capture_output=True, timeout=60, check=True)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("controls", nargs="*", help="control names (default: all)")
    args = parser.parse_args()
    selected = args.controls or list(CONTROLS)
    for name in selected:
        if name not in CONTROLS:
            parser.error(f"unknown control {name}; choose from {', '.join(CONTROLS)}")
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "compiler"
        source.mkdir()
        shutil.copytree(COMPILER / "src", source / "src")
        for path in COMPILER.iterdir():
            if path.name != "src" and not path.name.startswith("build"):
                (source / path.name).symlink_to(path, target_is_directory=path.is_dir())
        folder = Path(tmp) / "build"
        binary = folder / "kas3c"
        subprocess.run(["cmake", "-S", source, "-B", folder], check=True)
        build(folder)
        for oracle in sorted({o for name in selected for o in CONTROLS[name][3]}):
            if observe(binary, oracle) != (RUN / oracle).read_text():
                raise SystemExit(f"unmodified compiler differs from {oracle}")
        previous = None
        for name in selected:
            filename, old, replacement, oracles = CONTROLS[name]
            path = source / "src" / filename
            original = path.read_text()
            if original.count(old) != 1:
                raise SystemExit(f"{name}: expected exactly one mutation site")
            path.write_text(original.replace(old, replacement))
            # Some Make generators compare timestamps only to the second.
            for changed in {filename, previous} - {None}:
                for obj in (folder / "CMakeFiles/kas3front.dir").rglob(changed + ".*"):
                    if obj.suffix in (".o", ".obj"):
                        obj.unlink()
            previous = filename
            build(folder)
            for oracle in oracles:
                observed = observe(binary, oracle)
                if observed == (RUN / oracle).read_text():
                    raise SystemExit(f"{name}: mutation survived {oracle}")
                detail = observed.strip() if oracle.endswith(".out") else "IR differs"
                print(f"{name}: {oracle}: {detail}", flush=True)
            path.write_text(original)


if __name__ == "__main__":
    main()
