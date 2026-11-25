"""Pack the painted source plates listed in assets/art/prompts.json."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

ART = Path(__file__).resolve().parents[1] / "assets/art"
PALETTE = ("1b1f3a", "2e3a4a", "5f7f86", "ffb347", "ffe2a8", "e4572e", "efe6d2", "a3864b")


def palette_image(cool):
    bases = [tuple(bytes.fromhex(value)) for value in PALETTE]
    if cool:
        bases = [bases[i] for i in (0, 1, 2, 6)]
    # Intermediate pigments preserve the paper grain in the reduced image.
    colours = [tuple(round(a + (b - a) * part / 7) for a, b in zip(left, right))
               for i, left in enumerate(bases) for right in bases[i:] for part in range(8)]
    colours = list(dict.fromkeys(colours))
    palette = Image.new("P", (1, 1))
    palette.putpalette([v for colour in colours[:256] for v in colour])
    return palette


def key(image):
    rgb = np.asarray(image.convert("RGB"), dtype=np.int16)
    red, green, blue = rgb.transpose(2, 0, 1)
    spill = np.minimum(red, blue) - green
    alpha = np.uint8(255 - np.clip((spill - 24) * 6, 0, 255))
    frame = image.convert("RGBA")
    frame.putalpha(Image.fromarray(alpha))
    return frame


def frames(sheet, columns, rows, row_edges):
    width, height = sheet.size
    return [sheet.crop((round(x * width / columns), round(row_edges[y] * height),
                        round((x + 1) * width / columns), round(row_edges[y + 1] * height)))
            for y in range(rows) for x in range(columns)]


def pack(raw, entry):
    sheet = Image.open(raw).convert("RGB")
    if list(sheet.size) != entry["source_size"]:
        raise ValueError(f"{raw.name} has the wrong source dimensions")
    cols, rows, size = entry["columns"], entry["rows"], entry["cell"]
    category = entry["category"]
    sprite = category not in ("tiles", "illustrations")
    cells = frames(key(sheet) if sprite else sheet.convert("RGBA"), cols, rows,
                   entry.get("row_edges", [i / rows for i in range(rows + 1)]))
    boxes = [cell.getbbox() for cell in cells]
    shared = max(max(box[2] - box[0], box[3] - box[1]) for box in boxes if box)
    height = round(size * cells[0].height / cells[0].width) if category == "illustrations" else size
    atlas = Image.new("RGBA", (cols * size, rows * height))
    cool = category == "creatures" and not entry["key"].startswith("wren")
    palette = palette_image(cool)
    for index, (cell, box) in enumerate(zip(cells, boxes)):
        if box is None:
            continue
        if sprite:
            cell = cell.crop(box)
            longest = shared if category == "creatures" else max(cell.size)
            scale = size * 0.92 / longest
            cell = cell.resize((max(1, round(cell.width * scale)), max(1, round(cell.height * scale))), Image.Resampling.LANCZOS)
        else:
            cell = ImageOps.fit(cell, (size, height), Image.Resampling.LANCZOS)
        rgb = cell.convert("RGB")
        ink = rgb.quantize(palette=palette, dither=Image.Dither.NONE).convert("RGB")
        frame = Image.blend(rgb, ink, 0.85 if cool else 0.65).convert("RGBA")
        frame.putalpha(cell.getchannel("A"))
        x = index % cols * size + (size - frame.width) // 2
        y = index // cols * height + (height - frame.height) // 2
        atlas.alpha_composite(frame, (x, y))
    return atlas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("--review", type=Path)
    args = parser.parse_args()
    categories = {}
    for entry in json.loads((ART / "prompts.json").read_text()):
        atlas = pack(args.raw / (entry["key"] + ".png"), entry)
        atlas.save(ART / (entry["key"] + ".png"), optimize=True)
        group = entry["category"]
        categories.setdefault(group, []).append(atlas)
        if group == "creatures":
            black = Image.new("RGBA", atlas.size, "black")
            black.putalpha(atlas.getchannel("A"))
            categories.setdefault("silhouettes", []).append(black)
    if args.review:
        args.review.mkdir(parents=True, exist_ok=True)
        for name, plates in categories.items():
            thumbs = []
            for plate in plates:
                thumb = plate.copy()
                thumb.thumbnail((320, 220))
                thumbs.append(thumb)
            proof = Image.new("RGB", (640, ((len(thumbs) + 1) // 2) * 224), "#2e3a4a")
            for i, thumb in enumerate(thumbs):
                proof.paste(thumb, (i % 2 * 320, i // 2 * 224), thumb)
            proof.save(args.review / (name + ".png"))


if __name__ == "__main__":
    main()
