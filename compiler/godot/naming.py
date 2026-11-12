"""How Godot's names are written in AS3.

The rules are in docs/superpowers/specs/2026-11-12-godot-naming-design.md. Everything that needs a Godot
name in AS3 (the declaration generator here, the bindings later) asks this module, so that there is one
answer.
"""
import re

# The compiler's reserved words (compiler/src/token.h). A test holds this list to that file.
KEYWORDS = frozenset("""
as break case catch class const continue default delete do else extends false finally for function if
implements import in instanceof interface internal is new null package private protected public return
super switch this throw true try typeof use var void while with
""".split())

# Godot names that are also AS3 built-ins. `import godot.*` would make the bare name ambiguous in every
# file, so the Godot one gets a prefix. The generator fails on a clash that is not listed here.
CLASS_RENAMES = {
    "Object": "GodotObject",
    "Array": "GodotArray",
    "Dictionary": "GodotDictionary",
    "JSON": "GodotJSON",
    "Error": "GodotError",
}


def camel(name):
    """move_and_slide -> moveAndSlide. Leading underscores stay; an all-capitals name is left alone."""
    if name.upper() == name:
        return name
    body = name.lstrip("_")
    lead = name[: len(name) - len(body)]
    parts = [part for part in body.split("_") if part]
    if not parts:
        return name
    return lead + parts[0] + "".join(part[0].upper() + part[1:] for part in parts[1:])


def safe(name):
    """A name that AS3 reserves gets a trailing underscore."""
    return name + "_" if name in KEYWORDS else name


def member(name):
    """The AS3 name of a Godot method, property, signal or parameter."""
    return safe(camel(name))


def class_name(name):
    """The AS3 name of a Godot class or enum."""
    return CLASS_RENAMES.get(name, name.replace(".", ""))


def enum_values(names):
    """The AS3 names of a global enum's values: the common prefix goes, on underscore boundaries.

    A value that would then start with a digit keeps its full name.
    """
    if len(names) < 2:
        return list(names)
    split = [name.split("_") for name in names]
    shared = 0
    # A value must keep at least one word, so the prefix is shorter than the shortest name.
    limit = min(len(words) for words in split) - 1
    while shared < limit and all(words[shared] == split[0][shared] for words in split):
        shared += 1
    result = []
    for name, words in zip(names, split):
        short = "_".join(words[shared:])
        result.append(name if short[0].isdigit() else short)
    return result


def snake(name):
    """maxSpeed -> max_speed: how a script's own member is shown to the engine."""
    body = name.lstrip("_")
    lead = name[: len(name) - len(body)]
    body = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", body)
    body = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", body)
    return lead + body.lower()
