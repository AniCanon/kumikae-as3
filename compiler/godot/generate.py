"""Writes Godot's API as AS3 declarations for the `godot` package.

Usage: generate.py <extension_api.json> <out.as>

The input is Godot's own API description (`godot --dump-extension-api`). The output is one declaration
file: `native` classes and functions with no bodies, which the compiler reads to learn which names exist.
The naming rules are in naming.py and docs/superpowers/specs/2026-11-12-godot-naming-design.md.

The generator stops (Refused) on anything it does not understand. It never guesses a type.
"""
import glob
import json
import os
import re
import sys

import naming

HERE = os.path.dirname(os.path.abspath(__file__))

# Godot's built-in classes that AS3's own types stand for. They are not declared.
AS3_STANDS_FOR = {
    "Nil": "*",
    "bool": "Boolean",
    "int": "int",
    "float": "Number",
    "String": "String",
    "StringName": "String",
    "NodePath": "String",
    "Callable": "Function",
    "Variant": "*",
}

# Global functions that AS3 already has: `typeof` is an operator and `isFinite` a top-level function.
FUNCTIONS_LEFT_OUT = {"typeof", "is_finite"}

NUMBER = re.compile(r"-?\d+(\.\d+)?(e[-+]?\d+)?$")
INT32 = range(-(2 ** 31), 2 ** 31)
NUMERIC = {"int", "uint", "Number"}


class Refused(Exception):
    """The description has something the generator has no rule for."""


class Pointer(Exception):
    """A raw pointer type: the member that has one is left out."""


def as3_names():
    """Every class and package-level function, variable and constant that AS3's own declarations define."""
    names = set()
    for path in glob.glob(os.path.join(HERE, "..", "builtins", "*.as")):
        with open(path) as file:
            for line in file:
                found = re.match(r"\t(?:\w+ )*(?:class|interface|function|var|const) (\w+)", line)
                if found:
                    names.add(found.group(1))
    return names


def wide(values):
    return any(value["value"] not in INT32 for value in values)


class Generator:
    def __init__(self, api):
        self.api = api
        self.classes = {entry["name"]: entry for entry in api["classes"]}
        self.builtins = {entry["name"]: entry for entry in api["builtin_classes"]
                         if entry["name"] not in AS3_STANDS_FOR}
        self.singletons = {entry["type"] for entry in api["singletons"]}
        self.as3 = as3_names()
        self.lines = []
        self.defined = set()
        self.names = {}  # Godot class name -> {AS3 member name: what it is}
        self.left_out = []  # Members with raw pointers.
        # Every enum, by the name type strings use, with whether it needs more than 32 bits.
        self.enums = {}
        for entry in api["global_enums"]:
            self.enums[entry["name"]] = wide(entry["values"])
        for group in (api["classes"], api["builtin_classes"]):
            for entry in group:
                for enum in entry.get("enums", []):
                    self.enums[entry["name"] + "." + enum["name"]] = wide(enum["values"])

    # Types.

    def known(self, name):
        if name in AS3_STANDS_FOR:
            return AS3_STANDS_FOR[name]
        if name in self.classes or name in self.builtins:
            return naming.class_name(name)
        return None

    def enum_note(self, kind, name, where):
        if name not in self.enums:
            raise Refused("unknown enum '%s' in %s" % (name, where))
        owner, dot, enum = name.rpartition(".")
        if dot and (owner in self.classes or owner in self.builtins):
            return "%s:%s.%s" % (kind, naming.class_name(owner), enum)
        return "%s:%s" % (kind, naming.class_name(name))

    def type(self, text, meta, where):
        """(AS3 type, what the AS3 type does not say or None) for one of the description's type strings."""
        if "*" in text:
            raise Pointer()
        prefix, separator, rest = text.partition("::")
        if separator:
            if prefix in ("enum", "bitfield"):
                note = self.enum_note(prefix, rest, where)
                return ("Number" if self.enums[rest] else "int"), note
            if prefix == "typedarray":
                return "GodotArray", "Array[%s]" % self.element(rest, text, where)
            if prefix == "typeddictionary":
                key, _, value = rest.partition(";")
                return "GodotDictionary", "Dictionary[%s, %s]" % (self.element(key, text, where),
                                                                  self.element(value, text, where))
            raise Refused("unknown type '%s' in %s" % (text, where))
        if "," in text:  # A property that takes one of several classes; the leading "-" ones are excluded.
            text = [part for part in text.split(",") if not part.startswith("-")][0]
        if text == "int":
            if meta in ("int64", "uint64"):
                return "Number", None
            if meta in ("uint8", "uint16", "uint32"):
                return "uint", None
            return "int", None
        result = self.known(text)
        if result is None:
            raise Refused("unknown type '%s' in %s" % (text, where))
        return result, None

    def element(self, text, whole, where):
        # An element may carry a property hint before its class: "24/17:Texture2D".
        name = text.rpartition(":")[2]
        result = self.known(name)
        if result is None:
            raise Refused("unknown type '%s' in %s" % (whole, where))
        return result

    # Members.

    def default(self, value, as3, where):
        text = None
        if value.startswith('"'):
            text = value
        elif value.startswith(('&"', '^"')):
            text = value[1:]
        elif value.startswith('NodePath("') and value.endswith('")'):
            text = value[len("NodePath("):-1]
        compound = value in ("[]", "{}") or re.match(r"[A-Za-z_][\w\[\], ]*\(.*\)$", value)
        if as3 == "Boolean" and value in ("true", "false"):
            return value
        if as3 in NUMERIC and NUMBER.match(value):
            return value
        if as3 == "String" and text is not None:
            return text
        if as3 == "*" and (value in ("null", "true", "false") or NUMBER.match(value)):
            return value
        if as3 == "*" and text is not None:
            return text
        if as3 not in NUMERIC and as3 not in ("Boolean", "String") and (value == "null" or compound):
            return "null"  # AS3 defaults are constants. The binding supplies the real value.
        raise Refused("%s: can't write the default '%s' for a parameter of type %s" % (where, value, as3))

    def parameters(self, arguments, vararg, where, notes):
        written = []
        seen = set()
        for argument in arguments:
            name = naming.member(argument["name"])
            if name in seen or name == "returns":
                raise Refused("%s: two parameters named '%s'" % (where, name))
            seen.add(name)
            as3, note = self.type(argument["type"], argument.get("meta"), where)
            if note:
                notes.append('%s="%s"' % (name, note))
            text = "%s:%s" % (name, as3)
            if "default_value" in argument:
                text += " = " + self.default(argument["default_value"], as3, where)
            written.append(text)
        if vararg:
            written.append("...args" if "rest" in seen else "...rest")
        return ", ".join(written)

    def function(self, where, name, arguments, result, result_meta, vararg, modifiers):
        """The lines of one function declaration, or raises Pointer."""
        notes = []
        parameters = self.parameters(arguments, vararg, where, notes)
        returns = "void"
        if result is not None:
            returns, note = self.type(result, result_meta, where)
            if note:
                notes.append('returns="%s"' % note)
        lines = ["[Godot(%s)]" % ", ".join(notes)] if notes else []
        lines.append("public %snative function %s(%s):%s;" % (modifiers, name, parameters, returns))
        return lines

    def claim(self, owner, name, what, inherited):
        """Records that `owner` has a member `name`. False if it only repeats an inherited method."""
        own = self.names[owner]
        other = own.get(name) or inherited.get(name)
        if other is not None:
            if name not in own and other == what and what.startswith("the method "):
                return False  # A subclass describes a method its base already has.
            raise Refused("%s: '%s' names both %s and %s" % (owner, name, other, what))
        own[name] = what
        return True

    def constants(self, entry, owner, inherited, out):
        for enum in entry.get("enums", []):
            kind = "Number" if wide(enum["values"]) else "int"
            for value in enum["values"]:
                self.claim(owner, value["name"], "the constant " + value["name"], inherited)
                out.append("public static const %s:%s = %d;" % (value["name"], kind, value["value"]))
        for constant in entry.get("constants", []):
            self.claim(owner, constant["name"], "the constant " + constant["name"], inherited)
            if "type" not in constant:  # A class's constants are integers.
                kind = "int" if constant["value"] in INT32 else "Number"
                out.append("public static const %s:%s = %d;" % (constant["name"], kind, constant["value"]))
                continue
            as3, _ = self.type(constant["type"], None, owner + "." + constant["name"])
            value = " = " + constant["value"] if as3 in NUMERIC and NUMBER.match(constant["value"]) else ""
            out.append("public static const %s:%s%s;" % (constant["name"], as3, value))

    def enum_lines(self, entry):
        return ['\t[GodotEnum(name="%s", values="%s")]' % (enum["name"],
                                                         ",".join(value["name"] for value in enum["values"]))
                for enum in entry.get("enums", [])]

    def inherited(self, name):
        result = {}
        entry = self.classes.get(name)
        while entry is not None and entry.get("inherits"):
            entry = self.classes[entry["inherits"]]
            result.update(self.names[entry["name"]])
        return result

    def find_method(self, entry, name):
        while entry is not None:
            for method in entry.get("methods", []):
                if method["name"] == name:
                    return method
            entry = self.classes.get(entry.get("inherits"))
        return None

    def define(self, name):
        if name in self.as3:
            raise Refused("'%s' is also an AS3 name" % name)
        if name in self.defined:
            raise Refused("two definitions named '%s'" % name)
        self.defined.add(name)

    def godot_class(self, entry):
        owner = entry["name"]
        as3_name = naming.class_name(owner)
        self.define(as3_name)
        self.names[owner] = {}
        inherited = self.inherited(owner)
        static = "static " if owner in self.singletons else ""
        out = []
        self.constants(entry, owner, inherited, out)
        if entry["is_instantiable"]:
            out.append("public native function %s();" % as3_name)

        own_methods = {method["name"]: method for method in entry.get("methods", [])}
        hidden = set()
        for prop in entry.get("properties", []):
            where = "%s.%s" % (owner, prop["name"])
            name = naming.member(prop["name"])
            if not re.match(r"[A-Za-z_]\w*$", name):
                raise Refused("%s: not a name" % where)
            getter, setter = prop.get("getter"), prop.get("setter")
            if not getter and not setter:
                raise Refused("%s: no accessor" % where)
            self.claim(owner, name, "the property " + prop["name"], inherited)
            described = self.find_method(entry, getter) if getter else None
            if described is not None and "return_value" in described:
                as3, note = self.type(described["return_value"]["type"], described["return_value"].get("meta"), where)
            else:
                as3, note = self.type(prop["type"], None, where)
            # An accessor that takes an index serves several properties and stays a method.
            if getter in own_methods and not own_methods[getter].get("arguments"):
                hidden.add(getter)
            if setter in own_methods and len(own_methods[setter].get("arguments", [])) == 1:
                hidden.add(setter)
            if getter:
                if note:
                    out.append('[Godot(returns="%s")]' % note)
                out.append("public %snative function get %s():%s;" % (static, name, as3))
            if setter:
                out.append("public %snative function set %s(value:%s):void;" % (static, name, as3))

        for signal in entry.get("signals", []):
            name = naming.member(signal["name"])
            self.claim(owner, name, "the signal " + signal["name"], inherited)
            out.append("public %snative function get %s():Signal;" % (static, name))

        for method in entry.get("methods", []):
            if method["name"] in hidden:
                continue
            where = "%s.%s" % (owner, method["name"])
            name = naming.member(method["name"])
            result = method.get("return_value", {})
            modifiers = "static " if (static and not method["is_virtual"]) or method["is_static"] else ""
            try:
                lines = self.function(where, name, method.get("arguments", []), result.get("type"),
                                      result.get("meta"), method["is_vararg"], modifiers)
            except Pointer:
                self.left_out.append(where)
                continue
            if self.claim(owner, name, "the method " + method["name"], inherited):
                out.extend(lines)

        self.lines.extend(self.enum_lines(entry))
        extends = " extends " + naming.class_name(entry["inherits"]) if entry.get("inherits") else ""
        self.block("public native class %s%s" % (as3_name, extends), out)

    def constructor(self, entry, as3_name):
        """AS3 has no overloading, so the overloads merge by position."""
        where = entry["name"] + " constructor"
        overloads = [overload.get("arguments", []) for overload in entry["constructors"]]
        longest = max(overloads, key=len)
        shortest = min(len(overload) for overload in overloads)
        written = []
        for index, argument in enumerate(longest):
            kinds = {self.type(overload[index]["type"], None, where)[0]
                     for overload in overloads if len(overload) > index}
            if len(kinds) == 1:
                as3 = kinds.pop()
            else:
                as3 = "Number" if kinds <= NUMERIC else "*"
            text = "%s:%s" % (naming.member(argument["name"]), as3)
            if index >= shortest:
                text += " = " + ("0" if as3 in NUMERIC else "false" if as3 == "Boolean" else "null")
            written.append(text)
        return "public native function %s(%s);" % (as3_name, ", ".join(written))

    def builtin_class(self, entry):
        owner = entry["name"]
        as3_name = naming.class_name(owner)
        self.define(as3_name)
        self.names[owner] = {}
        out = []
        self.constants(entry, owner, {}, out)
        for member in entry.get("members", []):
            name = naming.member(member["name"])
            self.claim(owner, name, "the field " + member["name"], {})
            out.append("public var %s:%s;" % (name, self.type(member["type"], None, owner + "." + name)[0]))
        out.append(self.constructor(entry, as3_name))
        for method in entry.get("methods", []):
            where = "%s.%s" % (owner, method["name"])
            name = naming.member(method["name"])
            self.claim(owner, name, "the method " + method["name"], {})
            out.extend(self.function(where, name, method.get("arguments", []), method.get("return_type"), None,
                                     method["is_vararg"], "static " if method["is_static"] else ""))
        self.lines.extend(self.enum_lines(entry))
        self.block("public final native class %s" % as3_name, out)

    def global_enum(self, entry):
        as3_name = naming.class_name(entry["name"])
        self.define(as3_name)
        kind = "Number" if wide(entry["values"]) else "int"
        names = naming.enum_values([value["name"] for value in entry["values"]])
        if len(set(names)) != len(names):
            raise Refused("%s: two values with one name" % entry["name"])
        self.block("public final native class %s" % as3_name,
                   ["public static const %s:%s = %d;" % (name, kind, value["value"])
                    for name, value in zip(names, entry["values"])])

    def global_function(self, entry):
        if entry["name"] in FUNCTIONS_LEFT_OUT:
            return
        name = naming.member(entry["name"])
        self.define(name)
        for line in self.function(entry["name"], name, entry.get("arguments", []), entry.get("return_type"),
                                  None, entry["is_vararg"], ""):
            self.lines.append("\t" + line)

    def block(self, head, members):
        self.lines.append("\t%s {" % head)
        self.lines.extend("\t\t" + member for member in members)
        self.lines.append("\t}")

    def run(self):
        version = self.api["header"]["version_full_name"]
        self.lines += [
            "// Godot's API as AS3 declarations. Generated by compiler/godot/generate.py from the API description",
            "// of %s. Do not edit: change the generator and run it again." % version,
            "//",
            "// The API it describes is Godot's: copyright (c) 2014-present Godot Engine contributors and",
            "// (c) 2007-2014 Juan Linietsky, Ariel Manzur; MIT licence. See compiler/godot/NOTICE.md.",
            "package godot {",
        ]
        for entry in self.api["builtin_classes"]:
            if entry["name"] in self.builtins:
                self.builtin_class(entry)
        # A class's inherited names must be known before its own are claimed.
        done = set()

        def in_order(entry):
            if entry["name"] in done:
                return
            if entry.get("inherits"):
                if entry["inherits"] not in self.classes:
                    raise Refused("%s extends '%s', which is not described" % (entry["name"], entry["inherits"]))
                in_order(self.classes[entry["inherits"]])
            done.add(entry["name"])
            self.godot_class(entry)

        for entry in self.api["classes"]:
            in_order(entry)
        for entry in self.api["global_enums"]:
            self.global_enum(entry)
        for entry in self.api["utility_functions"]:
            self.global_function(entry)
        self.lines.append("}")
        return "\n".join(self.lines) + "\n"


def generate(api):
    return Generator(api).run()


def main(arguments):
    if len(arguments) != 3:
        sys.stderr.write(__doc__)
        return 2
    with open(arguments[1]) as file:
        api = json.load(file)
    generator = Generator(api)
    try:
        text = generator.run()
    except Refused as refusal:
        sys.stderr.write("generate.py: %s\n" % refusal)
        return 1
    with open(arguments[2], "w") as file:
        file.write(text)
    sys.stderr.write("%d lines; %d members with raw pointers left out\n" % (len(generator.lines),
                                                                           len(generator.left_out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
