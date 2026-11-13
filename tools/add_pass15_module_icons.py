# Adds the five new module icons to assets/textures/ui/module_icons.png and moves the four UI icons (plus, minus, lock, shard)
# from cells 15..18 to cells 20..23 (cells 15..19 are module ids now). 8 x 4 cells of 128 px, white on transparent.
import math
from PIL import Image, ImageDraw
SS = 4                    # supersampling
CELL = 128
W = 10 * SS               # stroke width of the existing icons is about 10 px

def new_cell():
    im = Image.new('L', (CELL * SS, CELL * SS), 0)
    return im, ImageDraw.Draw(im)

def P(x, y):              # a point in 128-px cell coordinates
    return (x * SS, y * SS)

def line(d, pts, w=W):
    for a, b in zip(pts, pts[1:]):
        d.line([P(*a), P(*b)], fill=255, width=w)
    for p in (pts[0], pts[-1]):
        r = w / 2
        d.ellipse([P(*p)[0] - r, P(*p)[1] - r, P(*p)[0] + r, P(*p)[1] + r], fill=255)

def arc(d, cx, cy, r, a0, a1, w=W):
    # angles in degrees, 0 = right, clockwise on the screen
    box = [P(cx - r, cy - r), P(cx + r, cy + r)]
    d.arc(box, a0, a1, fill=255, width=w)
    for a in (a0, a1):
        x = cx + r * math.cos(math.radians(a)); y = cy + r * math.sin(math.radians(a))
        rr = w / 2
        d.ellipse([P(x, y)[0] - rr, P(x, y)[1] - rr, P(x, y)[0] + rr, P(x, y)[1] + rr], fill=255)

def dot(d, x, y, r):
    d.ellipse([P(x - r, y - r), P(x + r, y + r)], fill=255)

def rrect(d, x0, y0, x1, y1, r):
    d.rounded_rectangle([P(x0, y0), P(x1, y1)], radius=r * SS, fill=255)

def icon_overflow():
    """a basin that is full and spills: three streams rise out of it"""
    im, d = new_cell()
    arc(d, 64, 66, 34, 5, 175)                 # the basin (lower half circle)
    line(d, [(30, 66), (30, 60)])
    line(d, [(98, 66), (98, 60)])
    line(d, [(64, 56), (64, 26)])              # centre stream
    dot(d, 64, 18, 8)
    line(d, [(46, 54), (38, 34), (30, 24)])    # left stream, curving out
    line(d, [(82, 54), (90, 34), (98, 24)])    # right stream
    dot(d, 28, 20, 6)
    dot(d, 100, 20, 6)
    return im

def icon_momentum():
    """four bars that grow: the stacks"""
    im, d = new_cell()
    xs = [24, 50, 76, 102]
    tops = [86, 68, 48, 24]
    for x, t in zip(xs, tops):
        rrect(d, x - 9, t, x + 9, 108, 8)
    return im

def hexagon(d, cx, cy, r, w=W, skip=None):
    pts = []
    for i in range(6):
        a = math.radians(-90 + 60 * i)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    for i in range(6):
        if skip is not None and i in skip: continue
        line(d, [pts[i], pts[(i + 1) % 6]], w)
    return pts

def icon_breach():
    """the hexagon of an armoured enemy, split down the middle: the two halves are pushed apart and a crack runs between them"""
    im, d = new_cell()
    top, ul, ll, bot = (64, 14), (22, 38), (22, 90), (64, 114)
    s = 9
    line(d, [(top[0] - s, top[1]), (ul[0] - s, ul[1]), (ll[0] - s, ll[1]), (bot[0] - s, bot[1])])
    line(d, [(top[0] + s, top[1]), (128 - ul[0] + s, ul[1]), (128 - ll[0] + s, ll[1]), (bot[0] + s, bot[1])])
    line(d, [(64, 28), (58, 50), (70, 66), (58, 84), (64, 100)], 5 * SS)     # the crack
    return im

def icon_aegis():
    """a dome of force over a small ship"""
    im, d = new_cell()
    arc(d, 64, 76, 46, 180, 360)               # the dome
    line(d, [(18, 76), (110, 76)])             # the ground line
    line(d, [(46, 100), (82, 100)])
    dot(d, 64, 56, 9)                          # the ship under the dome
    return im

def icon_mend():
    """a spanner: an open-end head at the top right, a thick handle to the bottom left"""
    im, d = new_cell()
    cx, cy, r = 84, 44, 25
    d.ellipse([P(cx - r - 5, cy - r - 5), P(cx + r + 5, cy + r + 5)], fill=255)          # head
    d.ellipse([P(cx - r + 5, cy - r + 5), P(cx + r - 5, cy + r - 5)], fill=0)           # hole
    wedge = [P(cx, cy)]                                                                   # the open jaw
    for a in range(-68, -17, 6):
        wedge.append(P(cx + 60 * math.cos(math.radians(a)), cy + 60 * math.sin(math.radians(a))))
    d.polygon(wedge, fill=0)
    line(d, [(22, 106), (66, 62)], 20 * SS)                                               # handle
    return im

def finalize(im):
    small = im.resize((CELL, CELL), Image.LANCZOS)
    out = Image.new('RGBA', (CELL, CELL), (255, 255, 255, 0))
    out.putalpha(small)
    return out

def main():
    atlas = Image.open('module_icons.png').convert('RGBA')
    cells = {}
    for c in (15, 16, 17, 18):
        x, y = (c % 8) * CELL, (c // 8) * CELL
        cells[c] = atlas.crop((x, y, x + CELL, y + CELL))
        atlas.paste(Image.new('RGBA', (CELL, CELL), (255, 255, 255, 0)), (x, y))
    for old, new in ((15, 20), (16, 21), (17, 22), (18, 23)):
        atlas.paste(cells[old], ((new % 8) * CELL, (new // 8) * CELL))
    new_icons = {15: icon_overflow(), 16: icon_momentum(), 17: icon_breach(), 18: icon_aegis(), 19: icon_mend()}
    for c, im in new_icons.items():
        atlas.paste(finalize(im), ((c % 8) * CELL, (c // 8) * CELL))
    atlas.save('module_icons_new.png')
    print('saved')

main()
