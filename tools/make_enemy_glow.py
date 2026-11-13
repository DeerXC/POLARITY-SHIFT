# Splits every enemy sprite into a neutral gunmetal hull and an emissive layer: the orange chevrons, the hazard stripes, the edge trim, the lenses and the lights of the
# engines are lifted out of the picture (GLOW) and the hull is left with dark steel in their place (BASE). The game draws the emissive layer additively in the colour of the
# enemy, so the plating of an enemy is red or blue like its bullets, and it can pulse, flow and flicker.
import numpy as np, sys, os
from PIL import Image, ImageFilter

NAMES = ['scout', 'sine', 'heavy', 'charger', 'turret', 'sniper', 'orbiter', 'rival']
PAD = 0.06  # margin of the glow picture on every side, as a share of the sprite: the bloom of a trim that runs along the edge is not cut off

def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def hsv(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = rgb.max(-1); mn = rgb.min(-1); d = mx - mn
    v = mx
    s = np.where(mx > 1e-6, d / np.maximum(mx, 1e-6), 0.0)
    h = np.zeros_like(mx)
    m = d > 1e-6
    rc = np.where(m, (mx - r) / np.maximum(d, 1e-6), 0)
    gc = np.where(m, (mx - g) / np.maximum(d, 1e-6), 0)
    bc = np.where(m, (mx - b) / np.maximum(d, 1e-6), 0)
    h = np.where(r == mx, bc - gc, np.where(g == mx, 2.0 + rc - bc, 4.0 + gc - rc))
    h = (h / 6.0) % 1.0
    return h * 360.0, s, v

def process(src, dst_dir, name):
    im = Image.open(src).convert('RGBA')
    a = np.asarray(im).astype(np.float32) / 255.0
    rgb, alpha = a[..., :3], a[..., 3]
    h, s, v = hsv(rgb)
    wh = smooth(h, 12, 28) * (1.0 - smooth(h, 62, 74))           # orange .. yellow
    ws = smooth(s, 0.30, 0.55)
    wv = smooth(v, 0.38, 0.62)
    m = wh * ws * wv * alpha
    # a one pixel dilation catches the soft edge of a thin line (anti aliasing mixes the orange with the steel next to it)
    mi = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3))
    m2 = np.asarray(mi).astype(np.float32) / 255.0
    m = np.maximum(m, 0.6 * m2 * (alpha > 0.5))
    # ---- the neutral hull: dark steel where the accent was (the colour of the metal around it, a little darker)
    lum = (rgb * np.array([0.30, 0.59, 0.11], np.float32)).sum(-1)
    steel = np.stack([0.20 + 0.10 * lum, 0.23 + 0.11 * lum, 0.30 + 0.13 * lum], -1)
    base = rgb * (1 - m[..., None]) + steel * m[..., None]
    out = np.dstack([np.clip(base, 0, 1), alpha])
    Image.fromarray((out * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(dst_dir, name + '.png'))
    # ---- the emissive layer: the accent, hottest where it was brightest, with a bloom around it, padded
    H, W = m.shape
    px, py = int(round(PAD * W)), int(round(PAD * H))
    core = np.zeros((H + 2 * py, W + 2 * px), np.float32)
    heat = np.clip(0.55 + 0.60 * (v - 0.55), 0.35, 1.0)
    core[py:py + H, px:px + W] = m * heat
    ci = Image.fromarray((core * 255).astype(np.uint8))
    r1 = max(2.0, 0.006 * max(W, H)); r2 = max(4.0, 0.016 * max(W, H))
    b1 = np.asarray(ci.filter(ImageFilter.GaussianBlur(r1))).astype(np.float32) / 255.0
    b2 = np.asarray(ci.filter(ImageFilter.GaussianBlur(r2))).astype(np.float32) / 255.0
    g = np.clip(core + 0.55 * b1 + 0.45 * b2 * 1.6, 0, 1)
    # RGB: white hot core, alpha carries the shape (the game tints it; additive blend)
    glow = np.dstack([np.ones_like(g), np.ones_like(g), np.ones_like(g), g])
    Image.fromarray((glow * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(dst_dir, name + '_glow.png'))
    return (W, H, W + 2 * px, H + 2 * py, float(m.mean()))

if __name__ == '__main__':
    srcdir, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for n in NAMES:
        print(n, process(os.path.join(srcdir, n + '.png'), dst, n))
