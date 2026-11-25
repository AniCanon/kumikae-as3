"""Render the field guide at the logical pixel sizes used by MarshView and Crowd."""
import argparse
import json
import math
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

GAME = Path(__file__).resolve().parents[1]
ART = GAME / "assets/art"
SHEETS = {s["key"]: s for s in json.loads((ART / "prompts.json").read_text())}
STATION = json.loads((GAME / "data/station.json").read_text())
SIZES = {e["key"]: e["drawSize"] for e in json.loads((GAME / "data/enemies.json").read_text())}
STRUCTURES = ("Lamp", "Wall", "Harpoon", "Brazier", "Bell", "Salvage rig")
BONE, TEAL, MARSH = "#efe6d2", "#afc0be", "#2e3a4a"
VIEW = (435, 234)


@lru_cache
def atlas(name):
    return Image.open(ART / (name + ".png")).convert("RGBA")


def stamp(scene, name, index, x, y, width, height=None, silhouette=False, opacity=1.0):
    sheet = atlas(name)
    columns = SHEETS[name]["columns"]
    w, h = sheet.width // columns, sheet.height // SHEETS[name]["rows"]
    u, v = index % columns * w, index // columns * h
    size = (width, height or width)
    frame = sheet.crop((u, v, u + w, v + h)).resize(size, Image.Resampling.BILINEAR)
    if opacity != 1:
        frame.putalpha(frame.getchannel("A").point(lambda a: round(a * opacity)))
    if silhouette:
        alpha = frame.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
        frame = Image.new("RGBA", size, "black")
        frame.putalpha(alpha)
    scene.alpha_composite(frame, (int(x - size[0] / 2), int(y - size[1] / 2)))


def creature(scene, name, x, y, silhouette=False):
    size = SIZES[name]
    if name == "tidecaller" and not silhouette:
        # Midway through the 90-tick call, below the caller and the fog.
        stamp(scene, "effects", 3, x, y, 39, opacity=0.4)
    stamp(scene, name, 0, x, y, size, silhouette=silhouette)
    if name == "undertow":
        for j in range(1, 7):
            stamp(scene, name, j, x + math.sin(j * 0.45) * 6, y + 15 + j * 8, 20,
                  silhouette=silhouette)


def ground(scene):
    for y in range(VIEW[1] // 13):
        for x in range(VIEW[0] // 15):
            band = 0 if y < 2 else 1 if y < 4 else 2 if y < 7 else 4 if y < 10 else 6
            if any(mx <= x < mx + w and my <= y < my + h for mx, my, w, h in STATION["marsh"]):
                band = 3
            if y >= 5 and x % 16 < 2:
                band = 5
            if (x, y) in ((4, 7), (11, 7), (18, 7)):
                band = 7
            variant = 2 if y in (2, 4, 7, 10) else (x * 7 + y * 5) % 2
            stamp(scene, "tiles", band * 3 + variant, x * 15 + 7.5, y * 13 + 6.5, 15, 13)


def murk(scene):
    # Crop of the 960 by 520 canvas at tide zero, with light_chart and sunrise at zero.
    y, x = np.indices((scene.height, scene.width))
    u, v = (x + 0.5) * 14 / 960, (y + 0.5) * 9 / 520
    i, j = np.floor(u), np.floor(v)
    a, b = u - i, v - j
    a, b = a * a * (3 - 2 * a), b * b * (3 - 2 * b)

    def grain(dx, dy):
        return np.mod(np.sin((i + dx) * 127.1 + (j + dy) * 311.7) * 43758.5453, 1)

    noise = ((1 - b) * ((1 - a) * grain(0, 0) + a * grain(1, 0))
             + b * ((1 - a) * grain(0, 1) + a * grain(1, 1)))[:, :, None]
    fog = 255 * (np.array([0.106, 0.122, 0.227]) * (1 - noise)
                 + np.array([0.373, 0.498, 0.525]) * noise)
    opacity = 0.4 + 0.3 * noise
    rgb = np.asarray(scene.convert("RGB")) * (1 - opacity) + fog * opacity
    return Image.fromarray(np.uint8(np.rint(rgb)))


def sample(lit):
    scene = Image.new("RGBA", VIEW)
    ground(scene)
    for i, name in enumerate(n for n in SIZES if n != "undertow"):
        if lit or name != "lurker":
            creature(scene, name, 29 + i * 53, 43)
    creature(scene, "undertow", 375, 49)
    for n in range(3):
        stamp(scene, "beacons", n, (4.5 + 7 * n) * 15, 7.5 * 13, 40)
    stamp(scene, "wren-walk", 0, 405, 181, 34)
    for col in range(6):
        for tier in range(3):
            stamp(scene, "structures", col * 3 + tier, (2.5 + 4 * col) * 15,
                  (11.5 + 2 * tier) * 13, 24)
    return scene.convert("RGB") if lit else murk(scene)


def label(draw, xy, message, size=14, colour=BONE):
    draw.text(xy, message, fill=colour, font=ImageFont.load_default(size=size))


def comparison():
    board = Image.new("RGB", (934, 628), "#1b1f3a")
    draw = ImageDraw.Draw(board)
    label(draw, (24, 17), "Lantern Reach", 26)
    label(draw, (24, 53), "Title painting", 15, TEAL)
    board.paste(atlas("title").resize((435, 245), Image.Resampling.LANCZOS), (24, 80))
    label(draw, (480, 81), "Reading the marsh", 23)
    lines = [
        "From left along the waterline",
        "Eel, brute, moth, jellyfish, heron, tidecaller, Undertow.",
        "The heron appears only in light.",
        "Along the shore",
        "Stone beacon crowns, from an empty wick to full light.",
        "On the quay, three tiers from top to bottom",
        "Lamp, wall, harpoon, brazier, bell, salvage rig. Wren at right.",
    ]
    for text, y in zip(lines, (126, 150, 173, 208, 232, 269, 293)):
        label(draw, (480, y), text, 14, TEAL if y in (126, 208, 269) else BONE)
    for x, lit, title in ((24, True, "Fully lit"), (475, False, "Unlit Murk")):
        label(draw, (x, 349), title, 21)
        board.paste(sample(lit), (x, 386))
    return board


def silhouettes():
    board = Image.new("RGBA", (760, 407), MARSH)
    draw = ImageDraw.Draw(board)
    label(draw, (24, 16), "Lantern Reach", 25)
    label(draw, (24, 53), "Silhouettes at native draw size", 16, TEAL)
    for i, name in enumerate(n for n in SIZES if n != "undertow"):
        x = 53 + 91 * i
        creature(board, name, x, 121, True)
        label(draw, (x - 29, 158), name.capitalize(), 13)
    creature(board, "undertow", 644, 110, True)
    label(draw, (690, 112), "Undertow", 13)
    for n, name in enumerate(STRUCTURES):
        x = 108 + n * 112
        label(draw, (x - 18, 218), name, 14)
        for tier in range(3):
            stamp(board, "structures", n * 3 + tier, x, 274 + tier * 48, 24, silhouette=True)
    for n, tier in enumerate(("I", "II", "III")):
        label(draw, (24, 265 + n * 48), tier, 15, TEAL)
    return board.convert("RGB")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    output = parser.parse_args().output
    output.mkdir(parents=True, exist_ok=True)
    comparison().save(output / "marsh-comparison.png", optimize=True)
    silhouettes().save(output / "silhouettes.png", optimize=True)
