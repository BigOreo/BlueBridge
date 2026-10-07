#!/usr/bin/env python3
"""Renders every icon and installer image from the brand SVGs in this folder.

Run from anywhere after editing an SVG:

    pip install pillow cairosvg
    python3 res/brand/render_icons.py

The outputs are committed, so building the project does not need these tools.
"""

import io
import os

import cairosvg
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

BLUE = "#1F5EFF"
SLATE = "#5B6B82"
INK = "#0B1424"
AMBER = "#F2A93B"


def path(*parts):
    return os.path.join(ROOT, *parts)


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return f.read()


def render(svg, size):
    png = cairosvg.svg2png(bytestring=svg.encode("utf-8"),
                           output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def save_png(image, *parts):
    image.save(path(*parts), optimize=True)


def mark(stroke, dot=None, dashed=False, width=4.0):
    """The mark in its 64-unit box: two screens on stands and the arch between them."""
    dash = ' stroke-dasharray="6 7"' if dashed else ""
    dot_svg = f'<circle cx="32" cy="15" r="5" fill="{dot}"/>' if dot else ""
    return (f'<g fill="none" stroke="{stroke}" stroke-width="{width}" stroke-linecap="round">'
            f'<rect x="6" y="34" width="20" height="15" rx="3"/>'
            f'<rect x="38" y="34" width="20" height="15" rx="3"/>'
            f'<path d="M16 49v5M11 55h10M48 49v5M43 55h10"/>'
            f'<path d="M16 34 Q32 -4 48 34"{dash}/>'
            f'</g>{dot_svg}')


def tray_svg(tile, dot=None, dashed=False):
    """Tray icon: the mark on a colored tile, readable on light and dark taskbars."""
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">'
            f'<rect x="1" y="1" width="62" height="62" rx="14" fill="{tile}"/>'
            f'<g transform="translate(5.1 4.7) scale(0.84)">'
            f'{mark("#FFFFFF", dot, dashed, width=6)}</g></svg>')


def mask_svg(dot=False, dashed=False):
    """macOS menu bar template image: black shapes on transparent."""
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">'
            f'{mark("#000000", "#000000" if dot else None, dashed, width=6)}</svg>')


def to_bmp(image, background, *parts):
    """Flattens onto a background color and saves a 24-bit BMP (Inno Setup, WiX)."""
    flat = Image.new("RGB", image.size, background)
    flat.paste(image, mask=image.split()[3])
    flat.save(path(*parts), format="BMP")


def wizard_large(width, height):
    """Inno Setup side panel: the mark on ink, in the upper third."""
    size = int(width * 0.62)
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">'
           f'{mark("#FFFFFF", AMBER)}</svg>')
    canvas = Image.new("RGBA", (width, height), INK)
    logo = render(svg, size)
    canvas.alpha_composite(logo, ((width - size) // 2, int(height * 0.16)))
    return canvas


def main():
    app_svg = read("app-icon.svg")
    small_svg = read("app-icon-small.svg")

    def app_icon(size):
        return render(small_svg if size <= 32 else app_svg, size)

    # Qt resources
    save_png(app_icon(256), "src", "gui", "res", "icons", "256x256", "bluebridge.png")
    # window icons: title bars and the taskbar pick the closest size
    for size in (16, 24, 32, 48, 64, 128):
        save_png(app_icon(size), "src", "gui", "res", "icons", "app", f"bluebridge-{size}.png")
    save_png(app_icon(180), "src", "gui", "res", "image", "about.png")
    states = {
        "connected": dict(tile=BLUE, dot=AMBER),
        "disconnected": dict(tile=SLATE, dashed=True),
        "transfering": dict(tile=BLUE, dot="#FFFFFF"),
    }
    for name, style in states.items():
        save_png(render(tray_svg(**style), 128),
                 "src", "gui", "res", "icons", "128x128", f"bluebridge-{name}.png")
        save_png(render(mask_svg(dot=style.get("dot") is not None,
                                 dashed=style.get("dashed", False)), 128),
                 "src", "gui", "res", "icons", "128x128", f"bluebridge-{name}-mask.png")

    # interface icons (24-unit grid, drawn at 48 px)
    os.makedirs(path("src", "gui", "res", "icons", "48x48"), exist_ok=True)
    ui_icons = {
        "computer": '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
        "manual": '<rect x="2.5" y="6" width="19" height="12" rx="2"/>'
                  '<path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>',
    }
    for name, shape in ui_icons.items():
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" '
               f'stroke="{SLATE}" stroke-width="1.75" stroke-linecap="round" '
               f'stroke-linejoin="round">{shape}</svg>')
        save_png(render(svg, 48), "src", "gui", "res", "icons", "48x48", f"{name}.png")

    # Windows icon: every size Explorer and the taskbar ask for
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    largest = app_icon(256)
    largest.save(path("res", "bluebridge.ico"), format="ICO",
                 sizes=[(s, s) for s in sizes],
                 append_images=[app_icon(s) for s in sizes[:-1]])

    # macOS
    big = app_icon(1024)
    for icns in (("src", "gui", "res", "mac", "QBlueBridge.icns"),
                 ("dist", "macos", "bundle", "BlueBridge.app", "Contents", "Resources",
                  "BlueBridge.icns")):
        big.save(path(*icns), format="ICNS")

    # Linux desktop icon
    with open(path("res", "io.github.bigoreo.bluebridge.svg"), "w", encoding="utf-8") as f:
        f.write(app_svg)

    # Inno Setup wizard images at 100%, 150% and 200% scaling
    os.makedirs(path("res", "inno"), exist_ok=True)
    for scale, (lw, lh), (sw, sh) in ((100, (164, 314), (55, 55)),
                                      (150, (246, 459), (83, 80)),
                                      (200, (328, 604), (110, 106))):
        to_bmp(wizard_large(lw, lh), INK, "res", "inno", f"wizard-large-{scale}.bmp")
        tile = app_icon(min(sw, sh))
        small = Image.new("RGBA", (sw, sh), "#FFFFFF")
        small.alpha_composite(tile, ((sw - tile.width) // 2, (sh - tile.height) // 2))
        to_bmp(small, "#FFFFFF", "res", "inno", f"wizard-small-{scale}.bmp")

    # WiX installer banner (493x58) and dialog (493x312)
    banner = Image.new("RGBA", (493, 58), "#FFFFFF")
    banner.alpha_composite(app_icon(40), (493 - 40 - 12, 9))
    to_bmp(banner, "#FFFFFF", "res", "banner.bmp")
    dialog = Image.new("RGBA", (493, 312), "#FFFFFF")
    dialog.alpha_composite(wizard_large(164, 312), (0, 0))
    to_bmp(dialog, "#FFFFFF", "res", "dialog.bmp")


if __name__ == "__main__":
    main()
