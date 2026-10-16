#!/usr/bin/env python3
"""Draws Ninja Impact's launcher icon and writes it at every Android density.

The emblem is drawn here, not taken from the game or the series: an orange
four-pointed shuriken with a spiral in its middle, on a deep blue background.
It makes the adaptive icon's foreground and monochrome layers, its background
colour, and the legacy square and round icons. Needs Pillow; the PNGs are
committed, so a build does not.

    python3 make_icons.py
"""
import math
import pathlib

from PIL import Image, ImageDraw

HERE = pathlib.Path(__file__).resolve().parent
RES = HERE / "res"
BACKGROUND = (16, 24, 48)  # deep blue
ORANGE = (249, 115, 22)
ORANGE_LIGHT = (253, 186, 116)
SIZE = 1024  # drawn large, scaled down per density


def shuriken(size: int) -> Image.Image:
    """The emblem on a transparent square of `size` pixels."""
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    c = size / 2
    outer, inner = size * 0.48, size * 0.21
    # Four blades: a star polygon whose points are slightly swept.
    points = []
    for i in range(8):
        angle = math.radians(-90 + i * 45 + (22 if i % 2 else 0))
        radius = outer if i % 2 == 0 else inner
        points.append((c + radius * math.cos(angle), c + radius * math.sin(angle)))
    draw.polygon(points, fill=ORANGE)
    # A lighter edge along one side of each blade, for depth.
    for i in range(0, 8, 2):
        tip = points[i]
        side = points[(i + 1) % 8]
        draw.polygon([(c, c), tip, side], fill=ORANGE_LIGHT)
    # The middle: a dark ring holding a spiral.
    hub = size * 0.11
    draw.ellipse((c - hub, c - hub, c + hub, c + hub), fill=BACKGROUND)
    spiral = []
    turns = 2.25
    for step in range(220):
        t = step / 219
        angle = t * turns * 2 * math.pi
        radius = hub * 0.85 * t
        spiral.append((c + radius * math.cos(angle), c + radius * math.sin(angle)))
    draw.line(spiral, fill=ORANGE, width=max(2, int(size * 0.018)), joint="curve")
    return image


def main() -> None:
    emblem = shuriken(SIZE)
    densities = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
    for density, scale in densities.items():
        out = RES / f"mipmap-{density}"
        out.mkdir(parents=True, exist_ok=True)
        layer, inner, legacy = round(108 * scale), round(72 * scale), round(48 * scale)
        # Adaptive foreground: the emblem inside the 72 dp the launcher's mask keeps.
        foreground = Image.new("RGBA", (layer, layer), (0, 0, 0, 0))
        mark = emblem.resize((inner, inner), Image.LANCZOS)
        foreground.paste(mark, ((layer - inner) // 2, (layer - inner) // 2), mark)
        foreground.save(out / "ic_launcher_foreground.png")
        # Monochrome: the emblem's shape, for themed icons.
        alpha = foreground.getchannel("A")
        Image.merge("LA", (alpha.point(lambda _: 255), alpha)).save(out / "ic_launcher_monochrome.png")
        # Legacy icons: the emblem on the background, square and round.
        for name, round_mask in (("ic_launcher", False), ("ic_launcher_round", True)):
            size = legacy * 4  # supersampled, then scaled down
            icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            mask = Image.new("L", (size, size), 0)
            shape = ImageDraw.Draw(mask)
            if round_mask:
                shape.ellipse((0, 0, size - 1, size - 1), fill=255)
            else:
                shape.rounded_rectangle((0, 0, size - 1, size - 1), radius=size // 6, fill=255)
            icon.paste(Image.new("RGBA", (size, size), BACKGROUND + (255,)), (0, 0), mask)
            mark = emblem.resize((int(size * 0.78), int(size * 0.78)), Image.LANCZOS)
            offset = (size - mark.width) // 2
            icon.paste(mark, (offset, offset), mark)
            icon.resize((legacy, legacy), Image.LANCZOS).save(out / f"{name}.png")
    colour = "#{:02x}{:02x}{:02x}".format(*BACKGROUND)
    (RES / "values" / "ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        f'    <color name="ic_launcher_background">{colour}</color>\n</resources>\n',
        encoding="utf-8")


if __name__ == "__main__":
    main()
