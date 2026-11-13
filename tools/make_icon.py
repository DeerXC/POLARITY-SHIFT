# make_icon.py - res/icon.ico (the icon of the program, GE_06.rc: IDI_APP) from the emblem of the game. Needs pillow.
#   python tools/make_icon.py            (from the project root)    also writes res/icon_preview.png
# The geometry is the one of Wordmark::DrawEmblem (src/ui/Wordmark.cpp): keep the two in step.
import io
import os
import struct

from PIL import Image, ImageDraw

RED = (255, 46, 41, 255)
BLUE = (26, 163, 255, 255)
IVORY = (237, 240, 250, 255)
INK = (10, 10, 18, 255)
FR, FT, HR, SH, GAP, CUT, CORE = 1.00, 0.085, 0.74, 0.30, 0.060, 0.050, 0.15


def tri(cx, cy, r, side, dy, grow=0.0):
    g = GAP / 2
    pts = [(-g, -HR), (-HR, 0), (-g, HR)] if side < 0 else [(g, -HR), (HR, 0), (g, HR)]
    mx = sum(p[0] for p in pts) / 3
    my = sum(p[1] for p in pts) / 3
    out = []
    for x, y in pts:
        vx, vy = x - mx, y - my
        l = (vx * vx + vy * vy) ** 0.5
        out.append((cx + (x + vx / l * grow) * r, cy + (y + vy / l * grow + dy) * r))
    return out


def draw(d, cx, cy, r, framed, core):
    diamond = lambda k: [(cx, cy - k * r), (cx + k * r, cy), (cx, cy + k * r), (cx - k * r, cy)]
    if framed:
        d.polygon(diamond(FR * 1.07), fill=INK)                    # a dark rim: the frame stays visible on a light task bar
        d.polygon(diamond(FR), fill=IVORY)
        d.polygon(diamond(FR - FT * 1.4142), fill=INK)
    left, right = tri(cx, cy, r, -1, -SH), tri(cx, cy, r, +1, +SH)
    if framed:
        d.polygon(tri(cx, cy, r, -1, -SH, CUT), fill=INK)
        d.polygon(tri(cx, cy, r, +1, +SH, CUT), fill=INK)
    d.polygon(left, fill=RED)
    d.polygon(right, fill=BLUE)
    if core:
        d.polygon([(cx, cy - CORE * r), (cx + CORE * r, cy), (cx, cy + CORE * r), (cx - CORE * r, cy)], fill=IVORY)


def render(size):
    ss = 8 if size < 64 else 4
    t = Image.new('RGBA', (size * ss, size * ss), (0, 0, 0, 0))
    draw(ImageDraw.Draw(t), size * ss / 2, size * ss / 2, size * ss * 0.5 / 1.24, framed=size >= 32, core=size >= 24)
    return t.resize((size, size), Image.LANCZOS)


def write_ico(path, sizes):
    images = [render(s) for s in sizes]
    blobs = []
    for im in images:
        b = io.BytesIO()
        im.save(b, format='PNG')
        blobs.append(b.getvalue())
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b''
    for s, blob in zip(sizes, blobs):
        entries += struct.pack('<BBBBHHII', 0 if s >= 256 else s, 0 if s >= 256 else s, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(path, 'wb') as f:
        f.write(header + entries + b''.join(blobs))
    return images


if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, '..', 'res')
    os.makedirs(out, exist_ok=True)
    imgs = write_ico(os.path.join(out, 'icon.ico'), [16, 24, 32, 48, 64, 128, 256])
    sheet = Image.new('RGBA', (256 + 128 + 64 + 48 + 32 + 24 + 16 + 8 * 8, 270), (30, 30, 40, 255))
    x = 8
    for im in reversed(imgs):
        sheet.alpha_composite(im, (x, 8))
        x += im.width + 8
    sheet.save(os.path.join(out, 'icon_preview.png'))
    print('res/icon.ico written:', [im.width for im in imgs])
