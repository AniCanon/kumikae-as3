"""Generate the small part of Godot's name table used for dynamic signal lookup and emission."""
import json
from pathlib import Path
import sys


def select(lines):
    members = {}
    owner = method = None
    for line in lines:
        key, value = line.rstrip("\n").split("=", 1)
        words = value.split()
        if key == "GodotObject":
            owner = words[0]
        if key == "GodotObject.emitSignal":
            method = (words[0], words[2])
        if words[1:] != ["signal"] and words[1:3] not in (["property", "Variant"], ["property", "Signal"]):
            continue
        name = key.rsplit(".", 1)[1]
        if members.setdefault(name, words[0]) != words[0]:
            raise ValueError(f"Ambiguous Godot spelling for signal member {name}")
    if not owner or not method:
        raise ValueError("Missing GodotObject.emitSignal binding")
    return members, (owner, *method)


if __name__ == "__main__":
    with Path(sys.argv[1]).open() as source:
        members, emit = select(source)
    lines = ["// Generated from Godot's name table.", '#include <string_view>', "",
             "namespace kas3 {", "struct SignalMember { std::string_view as3, godot; };",
             "inline constexpr SignalMember SIGNAL_MEMBERS[] = {"]
    lines.extend(f"\t{{ {json.dumps(key)}, {json.dumps(value)} }}," for key, value in sorted(members.items()))
    lines += ["};", "inline constexpr const char *EMIT_BIND[] = { " + ", ".join(map(json.dumps, emit)) + " };", "}"]
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
