#!/usr/bin/env python3
"""Renders every icon and installer image from the brand SVGs in this folder:
glidekvm-logo.svg (the full logo) and glidekvm-icon.svg (the small icon, made
by make_small_icon.py).

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

INK = "#0B1424"
GREY = "#8A97AB"

# the brand gradient, used as the default fill of both SVGs
GRADIENT_FILL = 'fill="url(#g)"'


def path(*parts):
    return os.path.join(ROOT, *parts)


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return f.read()


def render(svg, width, height=None):
    png = cairosvg.svg2png(bytestring=svg.encode("utf-8"),
                           output_width=width, output_height=height or width)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def save_png(image, *parts):
    image.save(path(*parts), optimize=True)


def solid(svg, color):
    """The small icon in one flat color (tray states, macOS template images)."""
    return svg.replace(GRADIENT_FILL, f'fill="{color}"')


def fit(svg, box_w, box_h, aspect=580 / 420, margin=0.04):
    """Renders the wide logo centred in a box, keeping its proportions."""
    w = int(box_w * (1 - 2 * margin))
    h = int(w / aspect)
    if h > box_h * (1 - 2 * margin):
        h = int(box_h * (1 - 2 * margin))
        w = int(h * aspect)
    logo = render(svg, w, h)
    canvas = Image.new("RGBA", (box_w, box_h), (0, 0, 0, 0))
    canvas.alpha_composite(logo, ((box_w - w) // 2, (box_h - h) // 2))
    return canvas


def to_bmp(image, background, *parts):
    """Flattens onto a background color and saves a 24-bit BMP (Inno Setup, WiX)."""
    flat = Image.new("RGB", image.size, background)
    flat.paste(image, mask=image.split()[3])
    flat.save(path(*parts), format="BMP")


def wizard_large(logo_svg, width, height):
    """Inno Setup side panel: the logo on ink, in the upper part."""
    canvas = Image.new("RGBA", (width, height), INK)
    logo = fit(logo_svg, width, int(width * 0.8), margin=0.08)
    canvas.alpha_composite(logo, (0, int(height * 0.14)))
    return canvas


def main():
    logo_svg = read("glidekvm-logo.svg")
    icon_svg = read("glidekvm-icon.svg")

    def app_icon(size):
        """The small icon up to 48 px; the full logo from 64 px, where its detail reads."""
        return render(icon_svg, size) if size <= 48 else fit(logo_svg, size, size)

    # Qt resources
    save_png(app_icon(256), "src", "gui", "res", "icons", "256x256", "glidekvm.png")
    # window icons: title bars and the taskbar pick the closest size
    for size in (16, 24, 32, 48, 64, 128):
        save_png(app_icon(size), "src", "gui", "res", "icons", "app", f"glidekvm-{size}.png")
    # the small icon at sharper sizes, for the window's own sidebar and banner
    for size in (64, 96):
        save_png(render(icon_svg, size), "src", "gui", "res", "icons", "app", f"glidekvm-small-{size}.png")
    save_png(fit(logo_svg, 240, 180), "src", "gui", "res", "image", "about.png")
    # tray: the small icon, in the brand gradient when sharing, grey when not
    states = {
        "connected": icon_svg,
        "disconnected": solid(icon_svg, GREY),
        "transfering": solid(icon_svg, "#3EEBE0"),
    }
    for name, svg in states.items():
        save_png(render(svg, 128), "src", "gui", "res", "icons", "128x128", f"glidekvm-{name}.png")
        save_png(render(solid(icon_svg, "#000000"), 128),
                 "src", "gui", "res", "icons", "128x128", f"glidekvm-{name}-mask.png")

    # on/off switches for options (drawn at twice their 40x22 size)
    os.makedirs(path("src", "gui", "res", "icons", "switch"), exist_ok=True)
    for name, track, knob_x, opacity in (("on", "#1F5EFF", 59, 1), ("off", "#C9D3E0", 21, 1),
                                         ("on-disabled", "#1F5EFF", 59, 0.4),
                                         ("off-disabled", "#C9D3E0", 21, 0.5)):
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 80 44">'
               f'<g opacity="{opacity}"><rect x="0" y="0" width="80" height="44" rx="22" fill="{track}"/>'
               f'<circle cx="{knob_x}" cy="22" r="17" fill="#FFFFFF"/></g></svg>')
        save_png(render(svg, 80, 44), "src", "gui", "res", "icons", "switch", f"{name}.png")

    # interface icons (24-unit grid, drawn at 48 px)
    os.makedirs(path("src", "gui", "res", "icons", "48x48"), exist_ok=True)
    ui_icons = {
        "computer": '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
        "manual": '<rect x="2.5" y="6" width="19" height="12" rx="2"/>'
                  '<path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>',
    }
    for name, shape in ui_icons.items():
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" '
               f'stroke="#5B6B82" stroke-width="1.75" stroke-linecap="round" '
               f'stroke-linejoin="round">{shape}</svg>')
        save_png(render(svg, 48), "src", "gui", "res", "icons", "48x48", f"{name}.png")

    # Windows icon: every size Explorer and the taskbar ask for
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    largest = app_icon(256)
    largest.save(path("res", "glidekvm.ico"), format="ICO",
                 sizes=[(s, s) for s in sizes],
                 append_images=[app_icon(s) for s in sizes[:-1]])

    # macOS
    big = app_icon(1024)
    for icns in (("src", "gui", "res", "mac", "QGlideKVM.icns"),
                 ("dist", "macos", "bundle", "GlideKVM.app", "Contents", "Resources",
                  "GlideKVM.icns")):
        big.save(path(*icns), format="ICNS")

    # Linux desktop icon: scalable, so the small icon that reads at launcher sizes
    with open(path("res", "io.github.bigoreo.glidekvm.svg"), "w", encoding="utf-8") as f:
        f.write(icon_svg)

    # Inno Setup wizard images at 100%, 150% and 200% scaling
    os.makedirs(path("res", "inno"), exist_ok=True)
    for scale, (lw, lh), (sw, sh) in ((100, (164, 314), (55, 55)),
                                      (150, (246, 459), (83, 80)),
                                      (200, (328, 604), (110, 106))):
        to_bmp(wizard_large(logo_svg, lw, lh), INK, "res", "inno", f"wizard-large-{scale}.bmp")
        tile = app_icon(min(sw, sh))
        small = Image.new("RGBA", (sw, sh), "#FFFFFF")
        small.alpha_composite(tile, ((sw - tile.width) // 2, (sh - tile.height) // 2))
        to_bmp(small, "#FFFFFF", "res", "inno", f"wizard-small-{scale}.bmp")

    # WiX installer banner (493x58) and dialog (493x312)
    banner = Image.new("RGBA", (493, 58), "#FFFFFF")
    banner.alpha_composite(app_icon(40), (493 - 40 - 12, 9))
    to_bmp(banner, "#FFFFFF", "res", "banner.bmp")
    dialog = Image.new("RGBA", (493, 312), "#FFFFFF")
    dialog.alpha_composite(wizard_large(logo_svg, 164, 312), (0, 0))
    to_bmp(dialog, "#FFFFFF", "res", "dialog.bmp")


if __name__ == "__main__":
    main()
