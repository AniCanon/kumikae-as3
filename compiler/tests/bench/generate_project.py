#!/usr/bin/env python3
"""Writes a made-up AS3 project of about the size asked for, to measure how long a whole check takes.

    python3 generate_project.py <directory> [lines] [--hub]

Every file is one class in package `game` that extends a Godot class and uses its neighbours. With --hub,
`game.Hub` has a static method that every class calls, as a project's central class is used everywhere.
"""
import os
import sys

BASES = ["Node2D", "CharacterBody2D", "Sprite2D", "Area2D", "Control", "Node"]


HUB = "package game {\n\npublic class Hub {\n\tpublic static function tick(n:int):int { return n; }\n}\n}\n"


def class_text(index, count, methods, hub=False):
    name = "Thing%d" % index
    other = "Thing%d" % ((index + 1) % count)
    lines = [
        "package game {",
        "import godot.*;",
        "",
        "public class %s extends %s {" % (name, BASES[index % len(BASES)]),
        "\tprivate var speed:Number = %d;" % (100 + index),
        "\tprivate var count:int = 0;",
        "\tprivate var label:String = \"%s\";" % name,
        "\tprivate var next:%s;" % other,
        "\tprivate var shift:Vector2 = new Vector2(1, 2);",
        "\tprivate var items:Array = [];",
        "",
    ]
    for m in range(methods):
        lines += [
            "\tpublic function step%d(delta:Number, amount:int):Number {" % m,
            "\t\tvar total:Number = speed * delta + amount;",
        ] + (["\t\tcount += Hub.tick(amount);"] if hub and m == 0 else []) + [
            "\t\tvar moved:Vector2 = shift * total + new Vector2(amount, count);",
            "\t\tfor (var i:int = 0; i < amount; i++) {",
            "\t\t\tcount += i %% %d;" % (m + 2),
            "\t\t\titems.push(label + i);",
            "\t\t}",
            "\t\tif (next != null && total > %d) {" % m,
            "\t\t\ttotal += next.step%d(delta, count);" % ((m + 1) % methods),
            "\t\t} else {",
            "\t\t\tlabel = name + \":\" + moved.length();",
            "\t\t}",
            "\t\treturn total;",
            "\t}",
            "",
        ]
    lines += ["}", "}", ""]
    return "\n".join(lines)


def main():
    arguments = [argument for argument in sys.argv[1:] if argument != "--hub"]
    hub = "--hub" in sys.argv
    directory = arguments[0]
    wanted = int(arguments[1]) if len(arguments) > 1 else 100000
    methods = 32  # About 500 lines a file.
    per_file = class_text(0, 1, methods).count("\n")
    count = max(1, wanted // per_file)
    os.makedirs(os.path.join(directory, "game"), exist_ok=True)
    total = 0
    for index in range(count):
        text = class_text(index, count, methods, hub)
        total += text.count("\n")
        with open(os.path.join(directory, "game", "Thing%d.as" % index), "w", encoding="utf-8") as f:
            f.write(text)
    if hub:
        with open(os.path.join(directory, "game", "Hub.as"), "w", encoding="utf-8") as f:
            f.write(HUB)
    print("%d files, %d lines" % (count, total))


if __name__ == "__main__":
    main()
