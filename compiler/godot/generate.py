"""Writes Godot's API as AS3 declarations for the `godot` package.

Usage: generate.py <extension_api.json> <out.as> [<names.txt>]

The input is Godot's own API description (`godot --dump-extension-api`). The output is one declaration
file: `native` classes and functions with no bodies, which the compiler reads to learn which names exist.
The naming rules are in naming.py and docs/superpowers/specs/2026-11-12-godot-naming-design.md.

With a third argument it also writes the Godot spelling of every declared class and member, one
"AS3 name=Godot name" line each, for the bindings.

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

# What Godot's description leaves out of Object, because the engine handles these itself. Scripts override
# the virtuals and call free(), so they are declared here. The generator refuses if the description ever
# gains one of them.
def _extra(name, arguments=(), result=None, virtual=True):
    entry = {"name": name, "is_const": False, "is_vararg": False, "is_static": False, "is_virtual": virtual,
             "arguments": [dict({"name": argument, "type": kind}, **({"meta": "int32"} if kind == "int" else {}))
                           for argument, kind in arguments]}
    if result:
        entry["return_value"] = {"type": result}
    return entry


OBJECT_EXTRAS = [
    _extra("_notification", [("what", "int")]),
    _extra("_get", [("property", "StringName")], "Variant"),
    _extra("_set", [("property", "StringName"), ("value", "Variant")], "bool"),
    _extra("_get_property_list", result="typedarray::Dictionary"),
    _extra("_validate_property", [("property", "Dictionary")]),
    _extra("_property_can_revert", [("property", "StringName")], "bool"),
    _extra("_property_get_revert", [("property", "StringName")], "Variant"),
    _extra("free", virtual=False),
]

# Listening to a signal is spelled the AS3 way, with the names AS3 programmers know from as3-signals, and
# Godot's own spelling is not declared: one way to do it. The binding maps them:
#   add(f)            connect(f); adding a listener that is already there does nothing, as in AS3
#   add(f, true)      connect(f, CONNECT_DEFERRED): the listener is called at the end of the frame
#   addOnce(f)        connect(f, CONNECT_ONE_SHOT)
#   remove(f)         disconnect(f)
#   removeAll()       disconnect for every connection
#   has(f)            is_connected(f)
#   numListeners      get_connections().size()
# A signal whose name is known only at run time is reached as any AS3 property is: `timer[name].add(f)`.
SIGNAL_EXTRAS = [
    ("add", "public native function add(listener:Function, deferred:Boolean = false):void;"),
    ("addOnce", "public native function addOnce(listener:Function, deferred:Boolean = false):void;"),
    ("remove", "public native function remove(listener:Function):void;"),
    ("removeAll", "public native function removeAll():void;"),
    ("has", "public native function has(listener:Function):Boolean;"),
    ("numListeners", "public native function get numListeners():int;"),
]
SIGNAL_REPLACED = {"connect", "disconnect", "is_connected", "get_connections", "has_connections"}

# Members that AS3's own Object stands for. A script writes toString() as in any AS3 class; the binding
# answers Godot's _to_string with it and calls Godot's to_string for it, so neither is declared.
AS3_HAS_METHOD = {("Object", "to_string")}

# Listening to a signal by its name in a string. A signal is listened to through its property
# (`timer.timeout.add(f)`), so these are not declared: one way to do it.
LISTEN_BY_NAME = {("Object", "connect"), ("Object", "disconnect"), ("Object", "is_connected")}

# Godot's integer has 64 bits, and so does every integer of its API unless the description says it is
# narrower: by a width mark, or by belonging to a built-in type that stores 32-bit integers. The
# description says which those are for the vectors (their members are marked int32); this one says it only
# in its name. Godot cuts a wide value off inside such a type without an error, so its integers stay
# `int` and a wide value never reaches it. A test checks that it still exists.
# (PackedByteArray is not one: it also reads and writes 64-bit integers, decode_s64 and the like.)
NARROW_BY_NAME = {"PackedInt32Array"}

# Godot's operators that AS3 writes the same way. A unary one is described as "unary-".
OPERATORS = {"+", "-", "*", "/", "%", "<", ">", "<=", ">="}
UNARY = {"unary-": "-", "unary+": "+"}
# Operators that are not recorded: every type has == and !=, and AS3 has no such operator on these types
# for the rest (the bitwise ones exist only on Godot's int).
OPERATORS_LEFT_OUT = {"==", "!=", "not", "in", "and", "or", "xor", "**", "~", "<<", ">>", "&", "|", "^"}

NUMBER = re.compile(r"-?\d+(\.\d+)?(e[-+]?\d+)?$")
NAME = re.compile(r"[A-Za-z_]\w*$")
NUMERIC = {"int", "uint", "Number", "Int64"}
INT_WIDTHS = {None: "Int64", "int8": "int", "int16": "int", "int32": "int", "char16": "int", "char32": "int",
              "uint8": "uint", "uint16": "uint", "uint32": "uint", "int64": "Int64", "uint64": "Int64",
              "required": "Int64"}


# Godot's description writes a few defaults in a way Godot's own expression reader does not take. These
# are the same values in a way it does. A test (tests/godot_project/defaults.gd) has Godot read every
# default that is recorded, so a new shape cannot slip by.
IDENTITIES = {
    "Transform2D(1, 0, 0, 1, 0, 0)": "Transform2D()",
    "Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)": "Transform3D()",
}


def evaluable(default):
    if default in IDENTITIES:
        return IDENTITIES[default]
    if re.match(r"Array\[[\w.]+\]\(\[\]\)$", default):
        return "[]"  # An empty array of a declared element type: an empty array is taken for it.
    return default


def narrow_classes(api):
    """The built-in types that store 32-bit integers: every member is one, or is such a type."""
    members = {}
    for layout in api.get("builtin_class_member_offsets", [])[:1]:
        for entry in layout["classes"]:
            members[entry["name"]] = [member["meta"] for member in entry["members"]]
    narrow = set(NARROW_BY_NAME)
    changed = True
    while changed:
        changed = False
        for name, metas in members.items():
            if name not in narrow and metas and all(meta == "int32" or meta in narrow for meta in metas):
                narrow.add(name)
                changed = True
    return narrow


def is_int32(value):
    return -(2 ** 31) <= value < 2 ** 31


class Refused(Exception):
    """The description has something the generator has no rule for."""


class Pointer(Exception):
    """A raw pointer type: the method that has one is left out."""


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
    return any(not is_int32(value["value"]) for value in values)


class Generator:
    def __init__(self, api):
        self.api = api
        self.classes = {entry["name"]: entry for entry in api["classes"]}
        self.builtins = {entry["name"]: entry for entry in api["builtin_classes"]
                         if entry["name"] not in AS3_STANDS_FOR}
        self.singletons = {entry["type"] for entry in api["singletons"]}
        self.as3 = as3_names()
        self.lines = []
        self.godot_names = {}  # "Class.member" or "Class" in AS3 -> the Godot spelling.
        self.defined = set()
        self.names = {}  # Godot class name -> {AS3 member name: what it is}
        self.left_out = []  # Members with raw pointers.
        # Operators whose left operand is a number (`2 * v`), by the class of the right operand.
        self.number_on_left = {}
        for entry in api["builtin_classes"]:
            if entry["name"] in ("int", "float"):
                for operator in entry.get("operators", []):
                    if operator["name"] in OPERATORS and operator.get("right_type") in self.builtins:
                        self.number_on_left.setdefault(operator["right_type"], []).append(
                            (operator["name"], entry["name"], operator["return_type"]))
        self.narrow = narrow_classes(api) & {entry["name"] for entry in api["builtin_classes"]}
        self.in_narrow = False  # Writing a type that stores 32-bit integers.
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
            raise Pointer(where)
        prefix, separator, rest = text.partition("::")
        if separator:
            if prefix in ("enum", "bitfield"):
                note = self.enum_note(prefix, rest, where)
                return ("Int64" if self.enums[rest] else "int"), note
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
            if meta not in INT_WIDTHS:
                raise Refused("unknown width '%s' in %s" % (meta, where))
            if meta is None and self.in_narrow:
                return "int", None
            return INT_WIDTHS[meta], None
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

    def name(self, name, owner):
        """A name that is about to be written, checked to be one AS3 can read."""
        if not NAME.match(name) or name in naming.KEYWORDS:
            raise Refused("%s: '%s' is not a name" % (owner, name))
        return name

    def parameters(self, arguments, vararg, where, notes):
        written = []
        seen = set()
        for argument in arguments:
            name = self.name(naming.member(argument["name"]), where)
            if name in seen or name == "returns":
                raise Refused("%s: two parameters named '%s'" % (where, name))
            seen.add(name)
            as3, note = self.type(argument["type"], argument.get("meta"), where)
            if note:
                notes.append('%s="%s"' % (name, note))
            text = "%s:%s" % (name, as3)
            if "default_value" in argument:
                written_default = self.default(argument["default_value"], as3, where)
                text += " = " + written_default
                if written_default == "null" and argument["default_value"] != "null" and as3 != "*" and "." in where:
                    # AS3 can't write this default. Godot's own text for it is kept, so that a `null`
                    # written in its place, with arguments after it, can be given the real value.
                    self.godot_names["@default.%s.%d" % (where, len(written))] = evaluable(argument["default_value"])
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
        """Records that `owner` has a member `name`, which no other member of it or of a base may have."""
        self.name(name, owner)
        own = self.names[owner]
        other = own.get(name) or inherited.get(name)
        if other is not None:
            raise Refused("%s: '%s' names both %s and %s" % (owner, name, other, what))
        own[name] = what
        # "the method move_and_slide": the last word is Godot's own name for it.
        if what.startswith("the "):
            self.godot_names["%s.%s" % (naming.class_name(owner), name)] = what.split(" ")[-1]

    def integer(self, value, where):
        if not isinstance(value, int) or isinstance(value, bool):
            raise Refused("%s: the value %r is not an integer" % (where, value))
        return value

    def constants(self, entry, owner, inherited, out):
        for enum in entry.get("enums", []):
            for value in enum["values"]:
                self.integer(value["value"], "%s.%s" % (owner, value["name"]))
            kind = "Int64" if wide(enum["values"]) else "int"
            for value in enum["values"]:
                self.claim(owner, value["name"], "the constant " + value["name"], inherited)
                out.append("public static const %s:%s = %d;" % (value["name"], kind, value["value"]))
        for constant in entry.get("constants", []):
            self.claim(owner, constant["name"], "the constant " + constant["name"], inherited)
            if "type" not in constant:  # A class's constants are integers.
                self.integer(constant["value"], "%s.%s" % (owner, constant["name"]))
                kind = "int" if is_int32(constant["value"]) else "Int64"
                out.append("public static const %s:%s = %d;" % (constant["name"], kind, constant["value"]))
                continue
            as3, _ = self.type(constant["type"], None, owner + "." + constant["name"])
            value = " = " + constant["value"] if as3 in NUMERIC and NUMBER.match(constant["value"]) else ""
            if as3 == "Int64" and value and is_int32(int(constant["value"])):
                as3 = "int"  # Its value is written here, and fits.
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
        # Nothing but a virtual can be overridden: the engine would never call the override.
        sealed = static or "final "
        methods = entry.get("methods", []) + (OBJECT_EXTRAS if owner == "Object" else [])
        out = []
        self.constants(entry, owner, inherited, out)
        if entry["is_instantiable"]:
            out.append("public native function %s();" % as3_name)

        own_methods = {method["name"]: method for method in entry.get("methods", [])}
        hidden = set()
        for prop in entry.get("properties", []):
            where = "%s.%s" % (owner, prop["name"])
            name = naming.member(prop["name"])
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
                out.append("public %snative function get %s():%s;" % (sealed, name, as3))
            if setter:
                out.append("public %snative function set %s(value:%s):void;" % (sealed, name, as3))

        for signal in entry.get("signals", []):
            name = naming.member(signal["name"])
            self.claim(owner, name, "the signal " + signal["name"], inherited)
            out.append("public %snative function get %s():Signal;" % (sealed, name))

        if static:
            # The singleton itself, for the places that want an object. The name is AS3's only.
            self.claim(owner, "singleton", "the property @singleton", inherited)
            out.append("public static native function get singleton():%s;" % as3_name)

        for method in methods:
            if method["name"] in hidden or (owner, method["name"]) in AS3_HAS_METHOD | LISTEN_BY_NAME:
                continue
            where = "%s.%s" % (owner, method["name"])
            name = naming.member(method["name"])
            result = method.get("return_value", {})
            if method["is_virtual"]:
                modifiers = ""
            else:
                modifiers = "static " if static or method["is_static"] else "final "
            try:
                lines = self.function(where, name, method.get("arguments", []), result.get("type"),
                                      result.get("meta"), method["is_vararg"], modifiers)
            except Pointer:
                self.left_out.append(where)
                continue
            self.claim(owner, name, "the method " + method["name"], inherited)
            out.extend(lines)

        self.lines.extend(self.enum_lines(entry))
        if static:
            self.lines.append("\t[Singleton]")
        if not entry["is_instantiable"]:
            self.lines.append("\t[Abstract]")
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

    def operand(self, name):
        """How an operator's operand or result type is written in a tag, or None if AS3 has no such operand."""
        if name in ("int", "float", "bool"):
            return AS3_STANDS_FOR[name]
        return naming.class_name(name) if name in self.builtins else None

    def operator_lines(self, entry):
        owner = entry["name"]
        # Says that the tags after it are every operator the type has: any other use is an error.
        lines = ["\t[Operators]"]
        for operator in entry.get("operators", []):
            name = operator["name"]
            if name in OPERATORS_LEFT_OUT:
                continue
            returns = self.operand(operator["return_type"])
            if name in UNARY:
                if returns:
                    lines.append('\t[Operator(op="%s", returns="%s")]' % (UNARY[name], returns))
                continue
            if name not in OPERATORS:
                raise Refused("%s: unknown operator '%s'" % (owner, name))
            right = self.operand(operator.get("right_type"))
            if right and returns:
                lines.append('\t[Operator(op="%s", right="%s", returns="%s")]' % (name, right, returns))
        for name, left, result in self.number_on_left.get(owner, []):
            returns = self.operand(result)
            if returns:
                lines.append('\t[Operator(op="%s", left="%s", returns="%s")]' % (name, self.operand(left), returns))
        if entry.get("indexing_return_type"):
            element = self.type(entry["indexing_return_type"], None, owner + " indexing")[0]
            lines.append('\t[Indexed(element="%s"%s)]' % (element, ', keyed="true"' if entry["is_keyed"] else ""))
        return lines

    def builtin_class(self, entry):
        owner = entry["name"]
        as3_name = naming.class_name(owner)
        self.define(as3_name)
        self.names[owner] = {}
        self.in_narrow = owner in self.narrow
        out = []
        self.constants(entry, owner, {}, out)
        for member in entry.get("members", []):
            name = naming.member(member["name"])
            self.claim(owner, name, "the field " + member["name"], {})
            out.append("public var %s:%s;" % (name, self.type(member["type"], None, owner + "." + name)[0]))
        out.append(self.constructor(entry, as3_name))
        for method in entry.get("methods", []):
            if owner == "Signal" and method["name"] in SIGNAL_REPLACED:
                continue
            where = "%s.%s" % (owner, method["name"])
            name = naming.member(method["name"])
            self.claim(owner, name, "the method " + method["name"], {})
            out.extend(self.function(where, name, method.get("arguments", []), method.get("return_type"), None,
                                     method["is_vararg"], "static " if method["is_static"] else ""))
        if owner == "Signal":
            for name, line in SIGNAL_EXTRAS:
                self.claim(owner, name, "AS3's " + name, {})
                # Godot has no one method for it: the binding composes it. '@' marks such a name.
                self.godot_names["Signal.%s" % name] = "@" + name
                out.append(line)
        self.lines.extend(self.enum_lines(entry))
        self.lines.extend(self.operator_lines(entry))
        self.block("public final native class %s" % as3_name, out)
        self.in_narrow = False

    def global_enum(self, entry):
        as3_name = naming.class_name(entry["name"])
        self.define(as3_name)
        for value in entry["values"]:
            self.integer(value["value"], "%s.%s" % (entry["name"], value["name"]))
        kind = "Int64" if wide(entry["values"]) else "int"
        names = naming.enum_values([value["name"] for value in entry["values"]])
        if len(set(names)) != len(names):
            raise Refused("%s: two values with one name" % entry["name"])
        for name in names:
            self.name(name, entry["name"])
        self.lines.append("\t[Abstract]")
        self.block("public final native class %s" % as3_name,
                   ["public static const %s:%s = %d;" % (name, kind, value["value"])
                    for name, value in zip(names, entry["values"])])

    def global_function(self, entry):
        if entry["name"] in FUNCTIONS_LEFT_OUT:
            return
        name = self.name(naming.member(entry["name"]), "godot")
        self.define(name)
        arguments = entry.get("arguments", [])
        # Godot describes print(arg1, ...) though print() is allowed. For a function that takes any number
        # of arguments and returns nothing (the print and push_error families), a lone leading argument is
        # not a minimum. str(), which returns a value, does need one, and max and min need two.
        if (entry["is_vararg"] and len(arguments) == 1 and arguments[0]["type"] == "Variant" and
                entry.get("return_type") is None):
            arguments = []
        described = arguments
        # A global function is called through an entry that takes arguments of exact types, so the binding
        # needs its signature: Godot's name, the hash Godot asks for, the result, the arguments, and
        # "varargs" for one that takes any number. (No dot anywhere: the interpreter splits a static call's
        # text at its last dot.)
        self.godot_names["@global.%s" % name] = " ".join(
            [entry["name"], str(entry["hash"]), entry.get("return_type", "void"),
             ",".join(argument["type"] for argument in described) or "-"] + (["varargs"] if entry["is_vararg"] else []))
        for line in self.function(entry["name"], name, arguments, entry.get("return_type"),
                                  None, entry["is_vararg"], ""):
            self.lines.append("\t" + line)

    def block(self, head, members):
        self.lines.append("\t%s {" % head)
        self.lines.extend("\t\t" + member for member in members)
        self.lines.append("\t}")

    def unused_entries(self):
        """Entries of the hand-kept lists that matched nothing in the description."""
        described = {(entry["name"], method["name"]) for entry in self.api["classes"]
                     for method in entry.get("methods", [])}
        builtins = {entry["name"] for entry in self.api["builtin_classes"]}
        return sorted(((AS3_HAS_METHOD | LISTEN_BY_NAME) - described) | {("", name) for name in NARROW_BY_NAME - builtins})

    def names_text(self):
        """One "AS3 name=Godot name" line per declared class and member, sorted. Call after run()."""
        names = dict(self.godot_names)
        for group in ("classes", "builtin_classes"):
            for entry in self.api[group]:
                as3_name = naming.class_name(entry["name"])
                if as3_name in self.defined:
                    names[as3_name] = entry["name"]
        return "".join("%s=%s\n" % item for item in sorted(names.items()))

    def run(self):
        try:
            return self.write()
        except Pointer as pointer:
            raise Refused("raw pointer type in %s, where nothing can be left out" % pointer.args[0])

    def write(self):
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


def names_text(generator):
    return generator.names_text()


def main(arguments):
    if len(arguments) not in (3, 4):
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
    except (KeyError, IndexError, TypeError, ValueError, AttributeError, RecursionError) as error:
        sys.stderr.write("generate.py: the description is not shaped as expected (%s: %s)\n" %
                         (type(error).__name__, error))
        return 1
    with open(arguments[2], "w") as file:
        file.write(text)
    if len(arguments) == 4:
        with open(arguments[3], "w") as file:
            file.write(generator.names_text())
    sys.stderr.write("%d lines; %d members with raw pointers left out\n" % (len(generator.lines),
                                                                           len(generator.left_out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
