#!/usr/bin/env python3
"""Rasterises Material Icons glyphs into assets/icons/*.png for EditorIcons.

The codepoints come from src/Engine/UI/IconsMaterialDesign.h, so a viewport
icon is the same glyph the scene tree already shows for that entity. White on
transparent: the billboard tints it per instance.

    python tools/make_editor_icons.py
"""

import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT = ROOT / "assets" / "MaterialIcons-Regular.ttf"
OUT = ROOT / "assets" / "icons"

SIZE = 80
# name -> codepoint, matching the ICON_MD_* used in Spawnable.h
GLYPHS = {
    "light": 0xE0F0,   # ICON_MD_LIGHTBULB
    "camera": 0xE04B,  # ICON_MD_VIDEOCAM
}


def render(codepoint: int, path: pathlib.Path) -> None:
    # Rasterise larger than the target, then box-filter down: the hinted
    # bitmap at 80px has hard edges the billboard's alpha test would alias.
    scale = 4
    px = SIZE * scale
    font = ImageFont.truetype(str(FONT), px)
    glyph = chr(codepoint)

    big = Image.new("L", (px * 2, px * 2), 0)
    draw = ImageDraw.Draw(big)
    draw.text((px, px), glyph, font=font, fill=255, anchor="mm")

    box = big.getbbox()
    if box is None:
        raise SystemExit(f"glyph U+{codepoint:04X} rendered empty")
    glyph_img = big.crop(box)

    # Fit inside the square keeping the aspect ratio, with a small margin so
    # the icon never touches the quad's edge.
    margin = int(SIZE * 0.06)
    fit = SIZE - 2 * margin
    w, h = glyph_img.size
    ratio = min(fit / w, fit / h)
    glyph_img = glyph_img.resize(
        (max(1, round(w * ratio)), max(1, round(h * ratio))), Image.LANCZOS
    )

    alpha = Image.new("L", (SIZE, SIZE), 0)
    alpha.paste(
        glyph_img,
        ((SIZE - glyph_img.width) // 2, (SIZE - glyph_img.height) // 2),
    )

    out = Image.merge("RGBA", (Image.new("L", (SIZE, SIZE), 255),) * 3 + (alpha,))
    out.save(path)
    print(f"{path.relative_to(ROOT)}  U+{codepoint:04X}")


def main() -> int:
    if not FONT.exists():
        raise SystemExit(f"font not found: {FONT}")
    OUT.mkdir(parents=True, exist_ok=True)
    for name, codepoint in GLYPHS.items():
        render(codepoint, OUT / f"{name}.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
