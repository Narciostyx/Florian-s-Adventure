"""Bake a pixel-perfect UI font atlas from a hinted pixel font.

Why offline instead of stb_truetype at runtime: pixel fonts are grid-fitted through
TrueType hinting, and stb_truetype.h does not implement hinting ("@TODO implement
hinting"). Rasterising one of these fonts without hinting produces off-grid, half-lit
pixels, which destroys the look the font exists to provide. FreeType hints correctly, so
this tool renders each glyph once at the design size and the engine only ever samples a
finished bitmap.

The atlas stays small because it carries only the characters the game actually uses: the
charset is collected from the source tree and data files, so adding new wording and
re-running this tool is all that is needed.

Usage:
    python tools/build_font_atlas.py --font <path-to-ttf> [--size 10]

Outputs, relative to the repository root:
    assets/fonts/ui_atlas.png    white RGB, glyph alpha; tinted at draw time
    assets/fonts/ui_atlas.json   metrics, one entry per codepoint
"""

import argparse
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN = [("src", (".cpp", ".hpp")), ("data", (".json",)), ("config", (".json",))]
GUTTER = 1


def collect_charset():
    """Every character that appears in the scanned sources, plus printable ASCII."""
    chars = set(chr(c) for c in range(32, 127))
    for folder, suffixes in SCAN:
        base = os.path.join(ROOT, folder)
        if not os.path.isdir(base):
            continue
        for dirpath, _dirs, files in os.walk(base):
            for name in files:
                if not name.endswith(suffixes):
                    continue
                try:
                    text = open(os.path.join(dirpath, name), encoding="utf-8").read()
                except (OSError, UnicodeDecodeError):
                    continue
                chars.update(ch for ch in text if ord(ch) > 126)
    return set(ch for ch in chars if 32 <= ord(ch) and ord(ch) != 127)


def bake(font_path, size):
    """Render the charset and shelf-pack it into an atlas. Returns (image, payload)."""
    font = ImageFont.truetype(font_path, size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    # Render into a scratch canvas with the "left/ascender" anchor, then crop to the
    # actual ink box: that gives the bitmap and its offset from the line top in one go.
    # The box has to come from `font.getbbox` -- ImageDraw.textbbox() uses the Draw
    # object's own font (none here), so it silently reports the 8 px default font's box
    # and would crop a 10x9 Han glyph down to its top-left 5x7 corner.
    # FreeType hints a pixel font onto the grid at its design size, so the result is
    # already binary; the threshold just guarantees it.
    pad = size * 2
    glyphs = {}
    for ch in collect_charset():
        canvas = Image.new("L", (pad * 2, pad * 2), 0)
        draw = ImageDraw.Draw(canvas)
        draw.text((pad, pad), ch, font=font, fill=255, anchor="la")
        rel = font.getbbox(ch)
        box = (pad + rel[0], pad + rel[1], pad + rel[2], pad + rel[3])
        ink = canvas.crop(box)
        width, height = ink.size
        glyphs[ch] = {
            "advance": int(round(font.getlength(ch))),
            "bearing_y": rel[1],
            "w": width,
            "h": height,
            "ink": ink.point(lambda v: 255 if v >= 128 else 0) if width and height else None,
        }

    drawn = sorted(((ch, g) for ch, g in glyphs.items() if g["ink"] is not None),
                   key=lambda kv: (-kv[1]["h"], -kv[1]["w"]))
    widest = max((g["w"] for _ch, g in drawn), default=8) + GUTTER
    atlas_width = max(64, min(256, widest * 6))

    x = y = row_height = 0
    for _ch, g in drawn:
        cell_w, cell_h = g["w"] + GUTTER, g["h"] + GUTTER
        if x + cell_w > atlas_width:
            x = 0
            y += row_height
            row_height = 0
        g["x"], g["y"] = x, y
        x += cell_w
        row_height = max(row_height, cell_h)
    atlas_height = max(y + row_height, 1)

    image = Image.new("RGBA", (atlas_width, atlas_height), (255, 255, 255, 0))
    for _ch, g in drawn:
        solid = Image.new("RGBA", (g["w"], g["h"]), (255, 255, 255, 255))
        solid.putalpha(g["ink"])
        image.paste(solid, (g["x"], g["y"]), solid)

    for g in glyphs.values():
        g.pop("ink", None)

    payload = {
        "version": 1,
        "size": size,
        "line_height": line_height,
        "ascent": ascent,
        "descent": descent,
        "width": atlas_width,
        "height": atlas_height,
        "glyphs": [
            {
                "cp": ord(ch),
                "src": [g.get("x", 0), g.get("y", 0), g.get("w", 0), g.get("h", 0)],
                "advance": g["advance"],
                "bearing_y": g.get("bearing_y", 0),
            }
            for ch, g in sorted(glyphs.items(), key=lambda kv: ord(kv[0]))
        ],
    }
    return image, payload


def main():
    parser = argparse.ArgumentParser(description="Bake the UI font atlas.")
    parser.add_argument("--font", required=True, help="path to the hinted pixel TTF")
    parser.add_argument("--size", type=int, default=10, help="design size (Fusion Pixel: 8/10/12)")
    args = parser.parse_args()

    if not os.path.exists(args.font):
        print("font not found:", args.font)
        return 1

    image, payload = bake(args.font, args.size)
    out_dir = os.path.join(ROOT, "assets", "fonts")
    os.makedirs(out_dir, exist_ok=True)
    image.save(os.path.join(out_dir, "ui_atlas.png"))
    with open(os.path.join(out_dir, "ui_atlas.json"), "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=1)
        handle.write("\n")

    print("atlas", image.size, "glyphs", len(payload["glyphs"]), "line_height", payload["line_height"])
    print("wrote assets/fonts/ui_atlas.png + ui_atlas.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
