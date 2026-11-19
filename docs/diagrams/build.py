#!/usr/bin/env python3
"""Generates the SVG diagrams in this folder.

Run from anywhere:  python3 docs/diagrams/build.py

To move the roadmap forward, change a status in ROADMAP below and run the script again.
Statuses: "done", "now", "next", "planned", "later".
"""
import html
import math
import os
import textwrap

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
    ("1", "Compiler front end", "kas3c parses and type-checks AS3 against Godot's whole API.",
     "Every error has a file, a line and a column.", "done"),
    ("2", "Language server", "Errors as you type, completion and navigation, in Rider and VS Code.",
     "A typing mistake shows up in the editor at once.", "done"),
    ("3", "Interpreter in Godot", "The AS3 language on nodes: [Export], [Signal], signals, Godot's values.",
     "An AS3 script on a CharacterBody2D moves a character.", "done"),
    ("4", "Hot reload", "Save a script and the running game changes, keeping its state.",
     "A constant edited during play changes the objects on screen.", "done"),
    ("5", "Native code", "kas3c cpp compiles the same scripts to native code through C++.",
     "Level with or ahead of Godot C# on macOS frame benchmarks.", "done"),
    ("6", "Play builds native code", "The extension builds and caches the program library by itself.",
     "Pressing Play runs native code with no manual step.", "done"),
    ("7", "Mobile native exports", "Godot and Rider build native AS3 into iOS and Android apps.",
     "iOS Simulator & Android APK pass. Hardware runs next.", "now"),
    ("8", "Debugger", "Breakpoints and stepping from Rider.",
     "You can step through AS3 in the editor and on a device.", "planned"),
    ("9", "Windows and Linux", "Desktop builds beyond macOS are not yet built or tested.",
     "The test suite passes on each platform.", "next"),
    ("10", "Consoles", "Console builds, kept in a separate repository.",
     "The same game runs on console hardware.", "planned"),
    ("+", "Later, designed separately", "async/await, Royale's language additions, the Flash API, SWF loading, web export.",
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


def pipeline():
    s = Svg(1290, 540, "How an .as file becomes a running game")
    s.text(40, 48, "How an .as file becomes a running game", size=20, weight="bold")
    s.text(40, 72, "One front end, one shared form of the program, two ways to run it.", size=13, fill=MUTED)

    s.box(40, 210, 120, 64, ["Player.as", "your source"])

    s.frame(200, 130, 420, 224, "Front end: kas3c")
    parts = [("Lexer", "text to tokens"), ("Parser", "tokens to tree"),
             ("Names", "resolve names"), ("Types", "check types")]
    x = 216
    s.arrow([(160, 242), (214, 242)])
    for i, (name, note) in enumerate(parts):
        s.box(x, 210, 86, 64, [name, note])
        if i < len(parts) - 1:
            s.arrow([(x + 86, 242), (x + 97, 242)])
        x += 98
    s.text(410, 316, "Reports every error as file:line:column", size=12, anchor="middle", fill=MUTED)

    s.arrow([(596, 242), (658, 242)])
    s.box(660, 210, 120, 64, ["Typed IR", "one meaning for", "both back ends"])

    s.arrow([(780, 242), (810, 242), (810, 152), (838, 152)])
    s.arrow([(810, 242), (810, 332), (838, 332)])

    s.box(840, 120, 130, 64, ["Interpreter", "runs the IR"])
    s.arrow([(970, 152), (1128, 152)])
    s.text(1049, 144, "no C++ toolchain", size=12, anchor="middle", fill=MUTED)
    s.box(1130, 120, 120, 64, ["While you", "work"])
    s.text(1190, 204, "hot reload", size=12, anchor="middle", fill=MUTED)

    s.box(840, 300, 130, 64, ["C++ backend", "IR to C++"])
    s.arrow([(970, 332), (998, 332)])
    s.box(1000, 300, 110, 64, ["C++ compiler", "the platform's own"])
    s.arrow([(1110, 332), (1138, 332)])
    s.box(1140, 300, 110, 64, ["Release", "builds"])
    s.text(1195, 384, "native code, no JIT", size=12, anchor="middle", fill=MUTED)

    s.box(280, 410, 260, 60, ["Godot's API as AS3", "generated from Godot's own API description"])
    s.arrow([(410, 410), (410, 356)])
    s.text(556, 436, "Checked like any other", size=12, fill=MUTED)
    s.text(556, 453, "AS3 source.", size=12, fill=MUTED)

    s.save("pipeline.svg")


def calls():
    s = Svg(1000, 430, "The three kinds of call")
    s.text(40, 48, "The three kinds of call", size=20, weight="bold")
    s.text(40, 72, "Where the time goes when a script runs as native code.", size=13, fill=MUTED)

    s.frame(40, 100, 280, 280, "Godot engine")
    s.box(70, 190, 220, 100, ["Nodes, physics, rendering", "the engine's own C++"])

    s.frame(600, 100, 360, 280, "Your AS3 code, compiled to native code")
    s.box(640, 150, 150, 64, ["Player", "extends CharacterBody2D"])
    s.box(640, 290, 150, 64, ["Enemy", "a plain AS3 class"])

    s.arrow([(320, 166), (638, 166)])
    s.text(460, 156, "1   _physicsProcess, signals", size=13, anchor="middle", weight="bold")
    s.arrow([(640, 198), (322, 198)])
    s.text(460, 220, "2   moveAndSlide()", size=13, anchor="middle", weight="bold")
    s.arrow([(715, 214), (715, 288)])
    s.text(730, 256, "3   enemy.hit()", size=13, weight="bold")
    s.save("calls.svg")


def hot_reload():
    s = Svg(1160, 400, "What happens when you save a script")
    s.text(40, 48, "What happens when you save a script", size=20, weight="bold")
    s.text(40, 72, "The running game takes the new code and keeps its state.", size=13, fill=MUTED)

    s.box(40, 150, 130, 64, ["You save", "Coin.as"])
    s.arrow([(170, 182), (208, 182)])
    s.box(210, 150, 150, 64, ["Compile again", "every script, as one", "program"])

    s.arrow([(360, 182), (400, 182), (400, 300), (438, 300)])
    s.text(408, 250, "errors", size=12, fill=MUTED)
    s.box(440, 268, 200, 64, ["Nothing changes", "errors are printed; the game", "keeps the code it had"])

    s.arrow([(360, 182), (438, 182)])
    s.text(399, 172, "ok", size=12, anchor="middle", fill=MUTED)
    s.frame(440, 110, 680, 130, "Reload, inside the running game")
    steps = [("Move objects", "fields keep", "their values"), ("New constants", "take their", "new values"),
             ("Update in place", "rerun safe", "constructors"), ("Report", "one line: what", "was updated")]
    x = 458
    for i, lines in enumerate(steps):
        s.box(x, 150, 148, 70, list(lines), "done")
        if i < len(steps) - 1:
            s.arrow([(x + 148, 185), (x + 164, 185)])
        x += 166

    s.text(440, 368, "A field the game changed since the object was built is never overwritten.", size=12, fill=MUTED)
    s.save("hot-reload.svg")


def roadmap():
    w, pad = 1160, 40
    navy_bg = "#131C28"
    card_bg = "#192434"
    card_border = "#28374B"
    ink_light = "#F1F5F9"
    ink_body = "#CBD5E1"
    ink_muted = "#7E90A6"
    brand = "#F0447B"

    # status -> (badge_bg, badge_stroke, accent_text, label)
    theme = {
        "done": ("#132E28", "#2B7A62", "#34D399", "BUILT"),
        "now": ("#381628", brand, "#FF6B9A", "IN PROGRESS"),
        "next": ("#2D2212", "#8F621B", "#FBBF24", "NEXT"),
        "planned": ("#16263B", "#325888", "#60A5FA", "PLANNED"),
        "later": ("#182230", "#334357", "#94A3B8", "LATER"),
    }

    def hex_points(cx, cy, r):
        pts = []
        for i in range(6):
            ang = math.radians(60 * i - 90)
            pts.append(f"{cx + r * math.cos(ang):.1f},{cy + r * math.sin(ang):.1f}")
        return " ".join(pts)

    built = [item for item in ROADMAP if item[4] == "done"]
    upcoming = [item for item in ROADMAP if item[4] not in ("done", "later")]
    later = [item for item in ROADMAP if item[4] == "later"]

    col_gap, row_gap = 16, 16
    col_w3 = (w - 2 * pad - 2 * col_gap) // 3
    built_h = 166
    built_rows = (len(built) + 2) // 3

    col_w4 = (w - 2 * pad - 3 * 14) // 4
    up_h = 190

    y_head_line = 104
    y_built_hdr = 132
    y_built = y_built_hdr + 18
    y_up_hdr = y_built + built_rows * (built_h + row_gap) + 18
    y_up = y_up_hdr + 18
    y_later = y_up + up_h + 22
    later_h = 64
    h = y_later + later_h + 34

    s = Svg(w, h, "Kumikae AS3 Roadmap")
    # Dark brand canvas overriding default white background
    s.parts.append(
        '<defs>'
        '<linearGradient id="rm-bg" x1="0" y1="0" x2="1" y2="1">'
        f'<stop offset="0%" stop-color="#0F1621"/><stop offset="100%" stop-color="{navy_bg}"/>'
        '</linearGradient>'
        '<linearGradient id="rm-glow" x1="0" y1="0" x2="1" y2="0">'
        f'<stop offset="0%" stop-color="#34D399"/><stop offset="70%" stop-color="#34D399"/>'
        f'<stop offset="100%" stop-color="{brand}"/>'
        '</linearGradient>'
        '</defs>'
        f'<rect x="0.5" y="0.5" width="{w - 1}" height="{h - 1}" rx="16" fill="url(#rm-bg)" stroke="{card_border}"/>'
    )

    # Subtle decorative hexagon motif in top-right corner
    for gx, gy, gr, op in [(w - 92, 46, 54, "0.05"), (w - 34, 80, 32, "0.04")]:
        s.parts.append(
            f'<polygon points="{hex_points(gx, gy, gr)}" fill="none" stroke="#CED5DF" '
            f'stroke-width="1.5" opacity="{op}"/>'
        )

    # Kumikae AS3 logo mark (from pluginIcon_dark.svg) in header
    s.parts.append(
        f'<g transform="translate({pad - 17}, 12) scale(0.088)">'
        '<path fill="#CED5DF" fill-rule="evenodd" d="M504 249 730 375 730 404 648 448 686 403 587 431 535 498 537 505 '
        '512 505 579 391 677 380 531 298 568 384 513 476 526 402 504 307 483 402 496 477 441 384 479 298 332 381 '
        '430 391 498 505 364 505 309 419 311 570 309 601 364 517 498 517 430 629 332 638 486 727 441 636 496 543 '
        '483 618 504 712 526 618 513 543 568 636 531 727 677 639 579 629 512 518 537 518 537 524 663 553 699 604 '
        '699 520 730 483 730 644 504 775 279 644 279 375 Z M322 404 396 473 478 499 422 431 Z M322 616 422 589 '
        '478 523 396 548 Z M535 527 587 589 686 615 613 553 Z"/>'
        f'<path fill="{brand}" d="M563 514 591 489 718 432 746 443 664 540 Z"/>'
        '</g>'
    )

    s.text(pad + 58, 52, "Kumikae AS3", size=21, weight="bold", fill=ink_light)
    s.text(pad + 200, 52, "Roadmap", size=21, weight="normal", fill=ink_muted)
    s.text(pad + 58, 74, "Every milestone ends in something you can run.", size=12.5, fill=ink_muted)

    # Compact legend + progress meter on top right
    lx = w - pad - 380
    for key in ["done", "now", "next", "planned"]:
        bg, stroke, fg, label = theme[key]
        s.parts.append(f'<circle cx="{lx}" cy="46" r="4.5" fill="{fg}"/>')
        s.text(lx + 11, 50, label, size=10.5, weight="bold", fill=ink_body)
        lx += 24 + len(label) * 7.2

    bar_x, bar_y, bar_w, bar_h = w - pad - 384, 66, 384, 6
    s.parts.append(f'<rect x="{bar_x}" y="{bar_y}" width="{bar_w}" height="{bar_h}" rx="3" fill="#223044"/>')
    s.parts.append(
        f'<rect x="{bar_x}" y="{bar_y}" width="{int(bar_w * 0.68)}" height="{bar_h}" rx="3" fill="url(#rm-glow)"/>'
    )
    s.parts.append(f'<line x1="{pad}" y1="{y_head_line}" x2="{w - pad}" y2="{y_head_line}" stroke="{card_border}"/>')

    def draw_card(x, y, cw, ch, item, wrap_w):
        number, name, what, exit_line, status = item
        bg, stroke, fg, label = theme[status]
        is_now = status == "now"
        border = brand if is_now else card_border
        bw = "1.6" if is_now else "1.1"
        s.parts.append(
            f'<rect x="{x}" y="{y}" width="{cw}" height="{ch}" rx="12" fill="{card_bg}" '
            f'stroke="{border}" stroke-width="{bw}"/>'
        )
        # Hexagonal milestone badge
        hx, hy = x + 28, y + 26
        s.parts.append(
            f'<polygon points="{hex_points(hx, hy, 14)}" fill="{bg}" stroke="{stroke}" stroke-width="1.4"/>'
        )
        s.text(hx, hy + 4, number.zfill(2) if number.isdigit() else number, size=10.5, anchor="middle",
               weight="bold", fill=fg)

        # Status chip (top-right)
        chip_w = max(58, len(label) * 6.8 + 18)
        s.parts.append(
            f'<rect x="{x + cw - chip_w - 14}" y="{y + 16}" width="{chip_w}" height="20" rx="10" '
            f'fill="{bg}" stroke="{stroke}" stroke-width="1"/>'
        )
        s.text(x + cw - chip_w / 2 - 14, y + 29.5, label, size=9.5, anchor="middle", weight="bold", fill=fg)

        s.text(x + 16, y + 58, name, size=14, weight="bold", fill=ink_light)

        body_lines = textwrap.wrap(what, width=wrap_w)[:2]
        for i, line in enumerate(body_lines):
            s.text(x + 16, y + 80 + i * 16, line, size=11.5, fill=ink_body)

        if exit_line:
            s.parts.append(
                f'<line x1="{x + 16}" y1="{y + ch - 46}" x2="{x + cw - 16}" y2="{y + ch - 46}" '
                'stroke="#233144" stroke-width="1"/>'
            )
            done_lines = textwrap.wrap("Done: " + exit_line, width=wrap_w + 4)[:2]
            for i, line in enumerate(done_lines):
                s.text(x + 16, y + ch - 28 + i * 15, line, size=10.5, fill=ink_muted)

    # Section 1: 3x2 grid for Built
    s.text(pad, y_built_hdr, "CORE & ENGINE  ·  BUILT", size=11, weight="bold", fill="#34D399")
    for idx, item in enumerate(built):
        col, row = idx % 3, idx // 3
        cx = pad + col * (col_w3 + col_gap)
        cy = y_built + row * (built_h + row_gap)
        draw_card(cx, cy, col_w3, built_h, item, wrap_w=46)

    # Section 2: 4-stage connected track for In Progress & Upcoming
    s.text(pad, y_up_hdr, "ACTIVE & UPCOMING", size=11, weight="bold", fill=brand)
    track_y = y_up + up_h / 2
    s.parts.append(
        f'<line x1="{pad + 40}" y1="{track_y}" x2="{w - pad - 40}" y2="{track_y}" '
        f'stroke="#2A394D" stroke-width="2" stroke-dasharray="5 4"/>'
    )
    for idx, item in enumerate(upcoming):
        cx = pad + idx * (col_w4 + 14)
        draw_card(cx, y_up, col_w4, up_h, item, wrap_w=32)

    # Section 3: Later horizon bar
    for item in later:
        number, name, what, _, status = item
        bg, stroke, fg, label = theme[status]
        s.parts.append(
            f'<rect x="{pad}" y="{y_later}" width="{w - 2 * pad}" height="{later_h}" rx="12" '
            f'fill="#101722" stroke="{card_border}" stroke-width="1.1"/>'
        )
        hx, hy = pad + 28, y_later + later_h / 2
        s.parts.append(
            f'<polygon points="{hex_points(hx, hy, 14)}" fill="{bg}" stroke="{stroke}" stroke-width="1.4"/>'
        )
        s.text(hx, hy + 4.5, number, size=13, anchor="middle", weight="bold", fill=fg)
        s.text(pad + 54, y_later + 27, name, size=13, weight="bold", fill=ink_light)
        s.text(pad + 54, y_later + 46, what, size=11.5, fill=ink_muted)
        s.parts.append(
            f'<rect x="{w - pad - 88}" y="{y_later + 22}" width="72" height="20" rx="10" '
            f'fill="{bg}" stroke="{stroke}" stroke-width="1"/>'
        )
        s.text(w - pad - 52, y_later + 35.5, label, size=9.5, anchor="middle", weight="bold", fill=fg)

    s.save("roadmap.svg")


def layers():
    s = Svg(1000, 470, "What each folder of the compiler may include")
    s.text(40, 48, "What each folder of the compiler may include", size=20, weight="bold")
    s.text(40, 72, "An arrow points from a folder to a folder it may include. Nothing else crosses.",
           size=13, fill=MUTED)

    s.box(40, 200, 160, 64, ["src/", "front end"])
    s.box(40, 90, 160, 64, ["lsp/", "language server"])
    s.box(290, 200, 160, 64, ["ir/", "typed IR and lowerer"])
    s.box(540, 120, 160, 64, ["vm/", "interpreter and library"])
    s.box(540, 280, 160, 64, ["backend_cpp/", "writes the IR as C++"])
    s.box(790, 120, 170, 64, ["runtime/", "what generated C++", "is built against"])
    s.box(790, 280, 170, 64, ["godot_ext/", "the Godot extension"])

    s.arrow([(120, 154), (120, 198)])
    s.arrow([(290, 232), (202, 232)])
    s.arrow([(540, 152), (495, 152), (495, 220), (452, 220)])
    s.arrow([(540, 312), (495, 312), (495, 244), (452, 244)])
    s.arrow([(790, 152), (702, 152)])
    s.arrow([(875, 280), (875, 186)])
    s.arrow([(790, 312), (702, 312)])

    s.box(790, 390, 170, 50, ["godot-cpp", "Godot's C++ bindings"])
    s.arrow([(875, 344), (875, 388)])
    s.text(40, 320, "Godot's API reaches src/ as AS3", size=12, fill=MUTED)
    s.text(40, 337, "declarations, never as a header.", size=12, fill=MUTED)
    s.text(540, 384, "Generated C++ includes", size=12, fill=MUTED)
    s.text(540, 401, "runtime/kas3.h and nothing else.", size=12, fill=MUTED)
    s.text(290, 300, "godot_ext/ may also include", size=12, fill=MUTED)
    s.text(290, 317, "vm/, ir/ and src/.", size=12, fill=MUTED)
    s.save("layers.svg")


def plugin():
    s = Svg(1100, 500, "The parts of the JetBrains plugin")
    s.text(40, 48, "The parts of the JetBrains plugin", size=20, weight="bold")
    s.text(40, 72, "The IDE shows things. The compiler, Godot and mobile devices stay separate programs.",
           size=13, fill=MUTED)

    s.frame(40, 100, 330, 360, "The plugin, inside Rider or IntelliJ IDEA")
    s.box(70, 140, 270, 70, ["Language support", "LSP client, typing help, highlighting"])
    s.box(70, 240, 270, 70, ["Project and run tools", "wizard, run configurations,", "test runner, settings and scene editors"])
    s.box(70, 340, 270, 70, ["Game Devices panel", "iOS Simulator and Android screens,", "input and console"])

    s.box(520, 140, 220, 70, ["kas3c lsp", "the compiler as a language server"])
    s.arrow([(340, 175), (518, 175)])
    s.text(430, 165, "LSP, standard I/O", size=12, anchor="middle", fill=MUTED)

    s.box(520, 240, 220, 70, ["Godot, runner.py & tools", "desktop, iOS & Android"])
    s.arrow([(340, 275), (518, 275)])

    s.box(520, 340, 220, 70, ["Simulator service", "core/: one session,", "local HTTP with a token"])
    s.arrow([(340, 375), (518, 375)])
    s.box(520, 432, 220, 44, ["simulator.py", "a command-line client"])
    s.arrow([(630, 432), (630, 412)])

    s.box(850, 290, 210, 64, ["xcrun simctl / adb", "boot, install, launch, logs"])
    s.box(850, 390, 210, 64, ["simulator-bridge", "Swift helper: iOS & Android feeds"])
    s.arrow([(740, 360), (800, 360), (800, 322), (848, 322)])
    s.arrow([(740, 390), (800, 390), (800, 422), (848, 422)])
    s.box(850, 140, 210, 70, ["iOS Simulator & Android", "running devices & emulators"])
    s.arrow([(955, 290), (955, 212)])
    s.arrow([(1060, 422), (1080, 422), (1080, 175), (1062, 175)])
    s.save("plugin.svg")


if __name__ == "__main__":
    pipeline()
    calls()
    hot_reload()
    roadmap()
    layers()
    plugin()
