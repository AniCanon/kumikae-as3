#!/usr/bin/env python3
"""Generates the SVG diagrams in this folder.

Run from anywhere:  python3 docs/diagrams/build.py

To move the roadmap forward, change a status in ROADMAP below and run the script again.
Statuses: "done", "now", "next", "planned", "later".
"""
import html
import os

FONT = "Helvetica Neue, Helvetica, Arial, sans-serif"
INK = "#202124"
MUTED = "#5F6368"
LINE = "#5F6368"

# status -> (fill, stroke, label)
STATUS = {
    "done": ("#DDF4E4", "#1E8E3E", "Built"),
    "now": ("#FFF1CC", "#C77700", "In progress"),
    "next": ("#FFF8E6", "#C77700", "Next"),
    "planned": ("#E6EEFA", "#3D6FB6", "Planned"),
    "later": ("#F1F3F4", "#80868B", "Later"),
    "plain": ("#FFFFFF", "#5F6368", ""),
}

ROADMAP = [
    ("0", "Speed spike", "Hand-written C++ in the compiler's output shape, measured against C#.",
     "Result: ahead of C# on 7 of 9 benchmarks, level on 2. Accepted by the owner.", "done"),
    ("1a", "Parser", "kas3c parse reads AS3 and reports syntax errors.",
     "Every syntax error has a file, line and column.", "done"),
    ("1b", "Names", "kas3c check resolves packages, imports, classes and scopes.",
     "Unknown and duplicate names are reported before running.", "now"),
    ("1c", "Types", "The checker gives every expression a type.",
     "Type errors and definite null misuse are reported before running.", "next"),
    ("L1", "Language server, first slice (proposed)", "kas3c lsp: errors as you type and go to definition.",
     "A typing mistake shows up in VS Code and Rider at once.", "planned"),
    ("2", "Interpreter in Godot", "Scripts run in the editor, with exports, signals and hot reload.",
     "An AS3 script on a CharacterBody2D moves a character.", "planned"),
    ("3", "Native export on desktop", "Release builds compile AS3 to native code through C++.",
     "The speed gate passes on real compiled AS3.", "planned"),
    ("4", "Mobile", "iOS and Android, debug and release builds.",
     "The same game runs on both, and the speed gate passes on devices.", "planned"),
    ("5", "Console", "Nintendo Switch through the Kumikae build.",
     "The same game runs on hardware, debug and release.", "planned"),
    ("6", "Tooling", "The rest of the language server, the debugger and the JetBrains plugin.",
     "Breakpoints and stepping from Rider, on a device too.", "planned"),
    ("+", "Later, designed separately", "An await form for signals, the Flash API, SWF loading, web export.",
     "", "later"),
]


def esc(s):
    return html.escape(s, quote=True)


class Svg:
    def __init__(self, w, h, title):
        self.w, self.h = w, h
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
            f'font-family="{FONT}" role="img" aria-label="{esc(title)}">',
            f"<title>{esc(title)}</title>",
            '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" '
            f'orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="{LINE}"/></marker></defs>',
            f'<rect x="0.5" y="0.5" width="{w - 1}" height="{h - 1}" rx="14" fill="#FFFFFF" stroke="#DADCE0"/>',
        ]

    def text(self, x, y, s, size=13, anchor="start", weight="normal", fill=INK):
        self.parts.append(
            f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" '
            f'fill="{fill}">{esc(s)}</text>')

    def frame(self, x, y, w, h, title):
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="#F8F9FA" stroke="#BDC1C6" '
            'stroke-dasharray="6 4"/>')
        self.text(x + 14, y + 24, title, size=13, weight="bold", fill=MUTED)

    def box(self, x, y, w, h, lines, status="plain"):
        fill, stroke, _ = STATUS[status]
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" '
            'stroke-width="1.5"/>')
        gap = 16
        top = y + h / 2 - (len(lines) - 1) * gap / 2 + 5
        for i, line in enumerate(lines):
            if i == 0:
                self.text(x + w / 2, top, line, size=14, anchor="middle", weight="bold")
            else:
                self.text(x + w / 2, top + i * gap, line, size=11.5, anchor="middle", fill=MUTED)

    def arrow(self, points):
        d = " ".join(f"{x},{y}" for x, y in points)
        self.parts.append(
            f'<polyline points="{d}" fill="none" stroke="{LINE}" stroke-width="1.6" marker-end="url(#arrow)"/>')

    def pill(self, x, y, w, h, status):
        fill, stroke, label = STATUS[status]
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{h / 2}" fill="{stroke}"/>')
        self.text(x + w / 2, y + h / 2 + 4.5, label, size=12, anchor="middle", weight="bold", fill="#FFFFFF")

    def legend(self, x, y, statuses):
        for status in statuses:
            fill, stroke, label = STATUS[status]
            self.parts.append(
                f'<rect x="{x}" y="{y - 12}" width="18" height="14" rx="4" fill="{fill}" stroke="{stroke}" '
                'stroke-width="1.5"/>')
            self.text(x + 26, y, label, size=12, fill=MUTED)
            x += 44 + len(label) * 7

    def save(self, name):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), name)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(self.parts) + "\n</svg>\n")


def status_of(stage):
    for number, _, _, _, status in ROADMAP:
        if number == stage:
            return status
    raise KeyError(stage)


def pipeline():
    s = Svg(1290, 540, "How an .as file becomes a running game")
    s.text(40, 48, "How an .as file becomes a running game", size=20, weight="bold")
    s.text(40, 72, "One front end, one shared form of the program, two ways to run it.", size=13, fill=MUTED)

    s.box(40, 210, 120, 64, ["Player.as", "your source"])

    s.frame(200, 130, 420, 224, "Front end: kas3c   (stage 1)")
    parts = [("Lexer", "text to tokens", "1a"), ("Parser", "tokens to tree", "1a"),
             ("Names", "resolve names", "1b"), ("Types", "check types", "1c")]
    x = 216
    s.arrow([(160, 242), (214, 242)])
    for i, (name, note, stage) in enumerate(parts):
        s.box(x, 210, 86, 64, [name, note], status_of(stage))
        if i < len(parts) - 1:
            s.arrow([(x + 86, 242), (x + 97, 242)])
        x += 98
    s.text(410, 316, "Reports every error as file:line:column", size=12, anchor="middle", fill=MUTED)

    s.arrow([(596, 242), (658, 242)])
    s.box(660, 210, 120, 64, ["Typed IR", "one meaning for", "both backends"], status_of("2"))

    s.arrow([(780, 242), (810, 242), (810, 152), (838, 152)])
    s.arrow([(810, 242), (810, 332), (838, 332)])

    s.text(905, 108, "stage 2", size=12, anchor="middle", fill=MUTED)
    s.box(840, 120, 130, 64, ["Interpreter", "runs the IR"], status_of("2"))
    s.arrow([(970, 152), (1128, 152)])
    s.text(1049, 144, "no C++ toolchain", size=12, anchor="middle", fill=MUTED)
    s.box(1130, 120, 120, 64, ["Editor and", "debug builds"])
    s.text(1190, 204, "hot reload, debugger", size=12, anchor="middle", fill=MUTED)

    s.text(905, 288, "stage 3", size=12, anchor="middle", fill=MUTED)
    s.box(840, 300, 130, 64, ["C++ generator", "IR to C++"], status_of("3"))
    s.arrow([(970, 332), (998, 332)])
    s.box(1000, 300, 110, 64, ["C++ compiler", "the platform's own"])
    s.arrow([(1110, 332), (1138, 332)])
    s.box(1140, 300, 110, 64, ["Release", "builds"])
    s.text(1195, 384, "native code, no JIT", size=12, anchor="middle", fill=MUTED)

    s.box(280, 410, 260, 60, ["Godot API bindings", "generated from Godot's own API description"],
          status_of("2"))
    s.arrow([(410, 410), (410, 356)])
    s.text(556, 436, "Also generates the call code", size=12, fill=MUTED)
    s.text(556, 453, "that both backends use.", size=12, fill=MUTED)

    s.legend(40, 510, ["done", "now", "next", "planned"])
    s.save("pipeline.svg")


def calls():
    s = Svg(1000, 430, "The three kinds of call")
    s.text(40, 48, "The three kinds of call", size=20, weight="bold")
    s.text(40, 72, "Where the time goes when a script runs, in a release build.", size=13, fill=MUTED)

    s.frame(40, 100, 280, 280, "Godot engine")
    s.box(70, 190, 220, 100, ["Nodes, physics, rendering", "the engine's own C++"])

    s.frame(600, 100, 360, 280, "Your AS3 code, compiled to native code")
    s.box(640, 150, 150, 64, ["Player", "extends CharacterBody2D"])
    s.box(640, 290, 150, 64, ["Enemy", "a plain AS3 class"])

    s.arrow([(320, 166), (638, 166)])
    s.text(460, 156, "1   _physics_process, signals", size=13, anchor="middle", weight="bold")
    s.arrow([(640, 198), (322, 198)])
    s.text(460, 220, "2   move_and_slide()", size=13, anchor="middle", weight="bold")
    s.arrow([(715, 214), (715, 288)])
    s.text(730, 256, "3   enemy.hit()", size=13, weight="bold")
    s.save("calls.svg")


def roadmap():
    row_h, gap, top = 68, 12, 100
    h = top + len(ROADMAP) * (row_h + gap) + 40
    s = Svg(1000, h, "Roadmap")
    s.text(40, 48, "Roadmap", size=20, weight="bold")
    s.text(40, 72, "Each stage ends in something you can run. The second line is how you know it's done.",
           size=13, fill=MUTED)
    cx = 76
    s.parts.append(
        f'<line x1="{cx}" y1="{top + row_h / 2}" x2="{cx}" '
        f'y2="{top + (len(ROADMAP) - 1) * (row_h + gap) + row_h / 2}" stroke="#BDC1C6" stroke-width="2"/>')
    y = top
    for number, name, what, exit_line, status in ROADMAP:
        fill, stroke, _ = STATUS[status]
        s.parts.append(
            f'<rect x="116" y="{y}" width="844" height="{row_h}" rx="10" fill="{fill}" stroke="{stroke}" '
            'stroke-width="1.5"/>')
        s.parts.append(f'<circle cx="{cx}" cy="{y + row_h / 2}" r="22" fill="{stroke}"/>')
        s.text(cx, y + row_h / 2 + 5, number, size=14, anchor="middle", weight="bold", fill="#FFFFFF")
        s.text(136, y + 22, name, size=15, weight="bold")
        s.text(136, y + 41, what, size=12.5)
        if exit_line:
            prefix = "" if exit_line.startswith("Result:") else "Done when: "
            s.text(136, y + 58, prefix + exit_line, size=12, fill=MUTED)
        s.pill(832, y + 10, 112, 24, status)
        y += row_h + gap
    s.save("roadmap.svg")


if __name__ == "__main__":
    pipeline()
    calls()
    roadmap()
