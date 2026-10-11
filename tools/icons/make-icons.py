#!/usr/bin/env python3
"""Render the desktop icon set from the shared FujiNet Go launcher art.

The artwork (data/icons/src/fujinet-go-h89-foreground.png) is the exact
same transparent FujiNet mark the other desktop apps use -- byte-identical
to the siblings' copies -- composited over this product's own badge, so the
whole family reads as one product line while each target is still told
apart at a glance in a dock.

The H89's badge is styled after the Heathkit nameplate on the machine's
front panel (per the user's request, 2026-10-11): a glossy black plate, the
gold frame of the "computer" box, and the cream of the lettering. Three
things differ from the plain sibling recipe:

  * The plate is not a flat fill but a subtle vertical gradient
    (#1c1c1e at the top to #0b0b0c at the bottom) with a faint gloss over
    its upper part, as on the enamelled plate.
  * A gold (#E39B2D) rounded-rectangle frame is drawn inset from the edge.
  * The mark is recoloured: its white stones and grid lines become the
    plate's cream (#E8E2D0), shading through its anti-aliased edges, and the
    black centre stone -- which would vanish on a black plate -- gets a
    cream rim.

Everything else about the composite (rounded-square mask, corner radius,
foreground zoom, output sizes) matches the sibling repos' own
tools/icons/make-icons.py exactly.

The results are committed (data/icons/hicolor/..., data/icons/*.icns,
frontends/windows/app.ico) so building the project needs no image tooling;
re-run this only when the artwork or the colours change:

    python3 tools/icons/make-icons.py
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
FOREGROUND = ROOT / "data/icons/src/fujinet-go-h89-foreground.png"
OUTDIR = ROOT / "data/icons/hicolor"
ICNS_OUT = ROOT / "data/icons/fujinet-go-h89.icns"
ICO_OUT = ROOT / "frontends/windows/app.ico"

PLATE_TOP = (0x1C, 0x1C, 0x1E)           # the plate, lit from above ...
PLATE_BOTTOM = (0x0B, 0x0B, 0x0C)        # ... to near-black at the foot
GLOSS_ALPHA = 0.07                       # peak white of the gloss, top edge
GLOSS_EXTENT = 0.42                      # fraction of the height it covers
FRAME = (0xE3, 0x9B, 0x2D)               # the "computer" box's gold
FRAME_INSET = 0.075                      # fraction of the edge
FRAME_WIDTH = 0.024                      # fraction of the edge
CREAM = (0xE8, 0xE2, 0xD0)               # the nameplate's lettering
STONE = PLATE_BOTTOM                     # the centre stone stays black
RIM_WIDTH = 0.11                         # fraction of the centre stone's diameter

MASTER = 1024                            # render big, downsample with LANCZOS
CORNER_RADIUS = 0.22                     # fraction of the edge
FOREGROUND_ZOOM = 1.18                   # Android's mask crops; compensate a little
SIZES = (16, 24, 32, 48, 64, 128, 192, 256, 512)
ICNS_SIZES = (32, 64, 128, 256, 512, 1024)  # every size macOS's icns TOC references


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def recolour(art: Image.Image) -> Image.Image:
    """White-on-transparent family mark -> cream mark with a black stone.

    The mark is greyscale, so luminance alone says where a pixel sits
    between the black centre stone (0) and the white stones/lines (255);
    mapping that ramp onto STONE..CREAM keeps every anti-aliased edge."""
    out = Image.new("RGBA", art.size)
    src = art.load()
    dst = out.load()
    w, h = art.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = src[x, y]
            if a == 0:
                dst[x, y] = (0, 0, 0, 0)
                continue
            lum = (r + g + b) / (3 * 255)
            dst[x, y] = lerp(STONE, CREAM, lum) + (a,)
    return out


def centre_stone_box(art: Image.Image):
    """Bounding box of the opaque black disc in the middle of the mark."""
    px = art.load()
    w, h = art.size
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a > 200 and (r + g + b) < 3 * 60:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise SystemExit("could not find the centre stone in the artwork")
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def render_plate(scale: int) -> Image.Image:
    size = MASTER * scale
    # vertical gradient, then the gloss over the upper part
    grad = Image.new("RGBA", (1, size))
    for y in range(size):
        t = y / (size - 1)
        rgb = lerp(PLATE_TOP, PLATE_BOTTOM, t)
        gloss = GLOSS_ALPHA * max(0.0, 1.0 - t / GLOSS_EXTENT)
        rgb = lerp(rgb, (255, 255, 255), gloss)
        grad.putpixel((0, y), rgb + (255,))
    plate = grad.resize((size, size))

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, size - 1, size - 1),
        radius=int(size * CORNER_RADIUS), fill=255)

    inset = int(size * FRAME_INSET)
    ImageDraw.Draw(plate).rounded_rectangle(
        (inset, inset, size - 1 - inset, size - 1 - inset),
        radius=int(size * CORNER_RADIUS) - inset,
        outline=FRAME + (255,), width=max(1, int(size * FRAME_WIDTH)))

    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(plate, (0, 0), mask)
    return out


def render_master() -> Image.Image:
    raw = Image.open(FOREGROUND).convert("RGBA")
    stone = centre_stone_box(raw)
    art = recolour(raw)

    # The plate and the stone's rim are drawn at 4x and downsampled so their
    # curves are antialiased, as the siblings do for the rounded square.
    scale = 4
    big = render_plate(scale)

    art_size = int(MASTER * FOREGROUND_ZOOM)
    k = art_size * scale / raw.width
    offset = (MASTER - art_size) * scale // 2
    art_big = art.resize((art_size * scale, art_size * scale), Image.LANCZOS)
    overlay = Image.new("RGBA", big.size, (0, 0, 0, 0))
    overlay.paste(art_big, (offset, offset), art_big)
    big = Image.alpha_composite(big, overlay)

    # The rim, centred on the stone, just outside its edge.
    x0, y0, x1, y1 = (offset + v * k for v in stone)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    diameter = ((x1 - x0) + (y1 - y0)) / 2
    width = diameter * RIM_WIDTH
    radius = diameter / 2 + width / 2
    ImageDraw.Draw(big).ellipse(
        (cx - radius, cy - radius, cx + radius, cy + radius),
        outline=CREAM + (255,), width=max(1, int(round(width))))

    composed = big.resize((MASTER, MASTER), Image.LANCZOS)
    # Keep everything inside the rounded silhouette.
    mask = Image.new("L", (MASTER * scale, MASTER * scale), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, MASTER * scale - 1, MASTER * scale - 1),
        radius=int(MASTER * scale * CORNER_RADIUS), fill=255)
    mask = mask.resize((MASTER, MASTER), Image.LANCZOS)
    composed.putalpha(Image.composite(composed.getchannel("A"),
                                      Image.new("L", (MASTER, MASTER), 0), mask))
    return composed


def main() -> int:
    if not FOREGROUND.exists():
        print(f"missing artwork: {FOREGROUND}", file=sys.stderr)
        return 1

    master = render_master()
    for size in SIZES:
        out = OUTDIR / f"{size}x{size}" / "apps" / "fujinet-go-h89.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        master.resize((size, size), Image.LANCZOS).save(out, optimize=True)
        print(f"wrote {out.relative_to(ROOT)}")

    # Pillow's ICNS writer works on any platform (no iconutil needed): it
    # just packs PNGs into the icns TOC. Pass every non-master size in
    # explicitly, LANCZOS-downsampled from the 1024 master like the hicolor
    # set above, so nothing gets a blurry re-resize from a smaller source.
    variants = [master.resize((size, size), Image.LANCZOS)
                for size in ICNS_SIZES if size != master.width]
    master.save(ICNS_OUT, format="ICNS", append_images=variants)
    print(f"wrote {ICNS_OUT.relative_to(ROOT)}")

    # The Windows .rc-embedded icon (frontends/windows/resource.rc's own
    # IDI_APPICON). Pillow's ICO writer packs whichever sizes are passed as
    # the `sizes` kwarg, resampling from `master` itself -- matching the
    # sibling repos' own app.ico (16x16 and 32x32, both 32bpp).
    ICO_OUT.parent.mkdir(parents=True, exist_ok=True)
    master.save(ICO_OUT, format="ICO", sizes=[(16, 16), (32, 32)])
    print(f"wrote {ICO_OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
