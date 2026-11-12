#!/usr/bin/env python3
"""Writes a made-up AS3 project of about the size asked for, to measure how long a whole check takes.

    python3 generate_project.py <directory> [lines]

Every file is one class in package `game` that extends a Godot class and uses its neighbours.
"""
import os
import sys

BASES = ["Node2D", "CharacterBody2D", "Sprite2D", "Area2D", "Control", "Node"]


def class_text(index, count, methods):
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
    directory = sys.argv[1]
    wanted = int(sys.argv[2]) if len(sys.argv) > 2 else 100000
    methods = 32  # About 500 lines a file.
    per_file = class_text(0, 1, methods).count("\n")
    count = max(1, wanted // per_file)
    os.makedirs(os.path.join(directory, "game"), exist_ok=True)
    total = 0
    for index in range(count):
        text = class_text(index, count, methods)
        total += text.count("\n")
        with open(os.path.join(directory, "game", "Thing%d.as" % index), "w", encoding="utf-8") as f:
            f.write(text)
    print("%d files, %d lines" % (count, total))


if __name__ == "__main__":
    main()
