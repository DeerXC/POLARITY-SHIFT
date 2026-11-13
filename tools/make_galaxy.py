#!/usr/bin/env python3
"""POLARITY SHIFT - the Milky Way of the background, simulated and then drawn in the style of the game.

THE SIMULATION (the part that makes it a galaxy and not a texture)
  The picture is what an observer standing INSIDE a spiral galaxy sees: an all-round panorama (360 degrees of galactic longitude, +-40 degrees of latitude).
    1. a model of the galaxy: a thin stellar disk (exponential in radius and height), four logarithmic spiral arms, a bulge, a thin and clumpy dust layer that
       follows the arms, and star-forming nebulae in the arms;
    2. RAY MARCHING: for every direction of the sky a ray is followed out of the observer; at every step the stars that glow there are added, and the dust that was
       passed on the way dims them. The dust cuts the band in two (the Great Rift) and leaves the dark pockets;
    3. a few thousand individual stars are drawn from the same model, with the same dust in front of them.

THE DRAWING (the part that makes it belong to this game: flat saturated fields, 2 - 3 hard tones, hairlines, hard diamonds, no soft gradient)
    * the glow is cut into THREE FLAT TONES with hard edges (a clump noise at the scale of the arms makes the edges organic, small islands and pinholes are taken away,
      so the shapes are bold) and a hairline runs along the outer edge of the band: the same ink as the HUD;
    * the stars are not in the picture: they are written to src/game/GalaxyStars.h and the game draws them as hard diamonds (the bright ones with a cross), so they are
      sharp at every resolution and they sit exactly in the structure of the band;
    * the picture is a grey-scale PNG that the game adds as light (black = nothing): lossless, so the edges stay hard.
  The left and the right edge of the picture are the same place (a full circle), so the game can drift through it for ever.

  python tools/make_galaxy.py [--png assets/textures/effects/galaxy.png] [--stars src/game/GalaxyStars.h] [--preview] [--seed N]
  numpy + scipy + Pillow. About 2 minutes (--preview: 20 seconds, smaller).
"""
import argparse
import math
import time
import numpy as np
from scipy.ndimage import gaussian_filter, map_coordinates, zoom, binary_opening, binary_closing, binary_erosion, label, binary_fill_holes
from PIL import Image

# ---- the picture -------------------------------------------------------------------------------------------------------------------------
PPD = 20.0                 # pixels per degree of the final picture (the game shows about 90 degrees on 1280 virtual px: 1.4 screen px per texel at 2560 wide)
LAT = 40.0                 # the picture covers +-LAT degrees of latitude
W = int(360 * PPD)         # 7200
H = int(2 * LAT * PPD)     # 1600
LANE_COVER = 11.0           # how much of the middle of the band the dark lanes take (percent)
COVER = (38.0, 16.0, 5.0)   # how much of the picture each of the three tones covers (percent)
RAY = 5.0                  # the glow is marched at 1 / RAY of the resolution (it is smooth) and enlarged

# ---- the galaxy (kpc) --------------------------------------------------------------------------------------------------------------------
R0 = 8.0                   # the observer's distance from the centre
RD = 3.9                   # scale length of the stellar disk
HZ = 0.32                  # scale height of the thin disk
HZ_THICK = 0.85
RD_DUST, HZ_DUST = 3.6, 0.27
KAPPA = 1.15               # dust opacity per kpc at the density 1
EXT = np.array([0.74, 1.0, 1.36])
ARMS, PITCH, R_IN = 4, math.radians(13.0), 1.6
ARM_W = 0.55


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


class Noise3:
    """value noise in 3D (trilinear, periodic), summed over a few octaves: the clumps of the dust and of the nebulae."""

    def __init__(self, seed, n=48):
        r = np.random.default_rng(seed)
        self.g = [r.random((n, n, n)).astype(np.float32) for _ in range(3)]
        self.n = n

    def __call__(self, x, y, z, scales=(1.3, 0.55, 0.22), weights=(0.55, 0.3, 0.15)):
        out = 0.0
        for g, s, w in zip(self.g, scales, weights):
            c = np.stack([x / s, y / s, z / s]).astype(np.float32) % self.n
            out = out + w * map_coordinates(g, c, order=1, mode="grid-wrap")
        return out


NOISE = Noise3(5)


def arm_weight(R, phi, width=ARM_W, shift=0.0):
    R = np.maximum(R, 0.3)
    spiral = np.log(R / R_IN) / math.tan(PITCH)
    step = 2.0 * math.pi / ARMS
    d = np.mod(phi - spiral + step / 2, step) - step / 2
    dist = d * R - shift
    return np.exp(-0.5 * (dist / (width * (0.6 + 0.4 * np.minimum(R, 8) / 8))) ** 2)


def dust_density(x, y, z):
    R = np.sqrt(x * x + y * y)
    phi = np.arctan2(y, x)
    arm = arm_weight(R, phi, 0.45, shift=-0.25)
    radial = np.exp(-(np.minimum(R, 10.0) - R0) / RD_DUST) * (R > 0.5)
    radial = np.minimum(radial, 6.0) * smoothstep(0.4, 2.2, R)
    vertical = np.exp(-np.abs(z - 0.04 * np.sin(2.0 * phi)) / HZ_DUST)
    clump = 0.15 + 1.9 * smoothstep(0.38, 0.72, NOISE(x, y, z * 2.2))
    return radial * vertical * (0.45 + 1.4 * arm) * clump


def emissivity(x, y, z):
    R = np.sqrt(x * x + y * y)
    phi = np.arctan2(y, x)
    arm = arm_weight(R, phi)
    disk = np.exp(-R / RD) * (np.exp(-np.abs(z) / HZ) * (1.0 + 3.0 * arm * np.exp(-np.abs(z) / 0.22)) + 0.10 * np.exp(-np.abs(z) / HZ_THICK))
    r2 = R * R + (z / 0.72) ** 2
    bulge = 13.0 * (1.0 + r2 / 0.65 ** 2) ** -2.5 * np.exp(-r2 / 2.4 ** 2)
    young = np.clip(arm * 0.9, 0, 1)[..., None]
    disk_col = (1 - young) * np.array([1.0, 0.93, 0.80]) + young * np.array([0.72, 0.86, 1.0])
    rgb = disk[..., None] * disk_col + bulge[..., None] * np.array([1.0, 0.76, 0.48])
    neb = arm * smoothstep(0.60, 0.80, NOISE(x * 1.1 + 7, y * 1.1, z * 3.0)) * np.exp(-np.abs(z) / 0.09) * np.exp(-R / 6.0) * 2.4
    rgb = rgb + neb[..., None] * np.array([1.0, 0.30, 0.42])
    return rgb, arm


def sky_to_xyz(l, b, d):
    cb = np.cos(b)
    return R0 - d * cb * np.cos(l), d * cb * np.sin(l), 0.02 + d * np.sin(b)


# ---- 1. the glow: ray marching -----------------------------------------------------------------------------------------------------------
STEPS = 110
D_EDGES = np.geomspace(0.03, 26.0, STEPS + 1)
D_MID = 0.5 * (D_EDGES[1:] + D_EDGES[:-1])
D_LEN = np.diff(D_EDGES)
NEAR_STEP = int(np.searchsorted(D_MID, 3.5))


def march(rows_lat, cols_lon, chunk=8, log=None):
    h, w = len(rows_lat), len(cols_lon)
    out = np.zeros((h, w), np.float32)
    near = np.zeros((h, w), np.float32)                      # the dust in the first 3.5 kpc: the dark lanes that cut the band
    jrng = np.random.default_rng(11)
    for r0 in range(0, h, chunk):
        b = rows_lat[r0:r0 + chunk][:, None, None]
        l = cols_lon[None, :, None]
        jitter = jrng.random((b.shape[0], w, STEPS)).astype(np.float32)   # (every ray samples its steps at another place: the error of a coarse step is noise, never a streak)
        d = D_EDGES[None, None, :-1] + jitter * D_LEN[None, None, :]
        x, y, z = sky_to_xyz(l, b, d)
        x, y, z = (np.broadcast_to(a, (b.shape[0], w, STEPS)) for a in (x, y, z))
        rho = dust_density(x, y, z) * D_LEN[None, None, :]
        tau = np.cumsum(rho, axis=2) * KAPPA - 0.5 * rho * KAPPA
        e, _ = emissivity(x, y, z)
        lum = e[..., 0] * 0.30 + e[..., 1] * 0.59 + e[..., 2] * 0.11          # (the flat tones are grey: only the luminance matters)
        out[r0:r0 + chunk] = np.sum(lum * np.exp(-tau * 0.95) * D_LEN[None, None, :], axis=2)
        near[r0:r0 + chunk] = tau[..., NEAR_STEP]
        if log:
            log(r0 + chunk, h)
    return out, near


# ---- 2. the stars ------------------------------------------------------------------------------------------------------------------------
def tau_grid(n_l=361, n_b=int(2 * LAT / 0.8) + 1):
    ls = np.linspace(-math.pi, math.pi, n_l)
    bs = np.radians(np.linspace(-LAT - 4, LAT + 4, n_b + 10))
    l, b, d = np.meshgrid(ls, bs, D_MID, indexing="ij")
    x, y, z = sky_to_xyz(l, b, d)
    rho = dust_density(x, y, z) * D_LEN[None, None, :]
    tau = (np.cumsum(rho, axis=2) - 0.5 * rho) * KAPPA
    return ls, bs, tau.astype(np.float32)


def sample_stars(n, rng, grid):
    """n stars from the model. Returns lon, lat (radians), flux (rgb) after the dust."""
    ls, bs, tau = grid
    n_bulge = n // 4
    n_disk = n - n_bulge
    R = rng.gamma(2.0, RD, n_disk * 2)
    phi = rng.uniform(-math.pi, math.pi, n_disk * 2)
    thick = rng.random(n_disk * 2) < 0.12
    z_all = np.where(thick, rng.laplace(0.0, HZ_THICK, n_disk * 2), rng.laplace(0.0, HZ, n_disk * 2))
    keep = rng.random(n_disk * 2) < (1.0 + 2.2 * arm_weight(R, phi) * np.exp(-np.abs(z_all) / 0.22)) / 3.2
    R, phi, z = R[keep][:n_disk], phi[keep][:n_disk], z_all[keep][:n_disk]
    dx, dy, dz = R * np.cos(phi), R * np.sin(phi), z
    young = np.clip(arm_weight(R, phi) * 0.9, 0, 1)
    u = rng.random(n_bulge)
    rb = 0.55 / np.sqrt(u ** (-2.0 / 3.0) - 1.0 + 1e-9)
    ct = rng.uniform(-1, 1, n_bulge)
    ph = rng.uniform(0, 2 * math.pi, n_bulge)
    st = np.sqrt(1 - ct * ct)
    bx, by, bz = rb * st * np.cos(ph), rb * st * np.sin(ph), rb * ct * 0.72
    X = np.concatenate([dx, bx]); Y = np.concatenate([dy, by]); Z = np.concatenate([dz, bz])
    is_bulge = np.concatenate([np.zeros(len(dx), bool), np.ones(n_bulge, bool)])
    young = np.concatenate([young, np.zeros(n_bulge)])
    rx, ry, rz = X - R0, Y, Z - 0.02
    d = np.sqrt(rx * rx + ry * ry + rz * rz)
    lon = np.arctan2(ry, -rx)
    lat = np.arcsin(np.clip(rz / np.maximum(d, 1e-6), -1, 1))
    Lum = (rng.random(len(X)) * (1 - 600.0 ** -1.05) + 600.0 ** -1.05) ** (-1 / 1.05) * 0.04
    t = rng.random(len(X))
    kind = np.zeros(len(X), np.int8)                 # 0 ivory, 1 cool (young), 2 warm (old giant)
    kind[t < 0.15 + 0.7 * young] = 1
    kind[(kind == 0) & ((t > 0.62) | (is_bulge & (t > 0.2)))] = 2
    li = (lon + math.pi) / (2 * math.pi) * (len(ls) - 1)
    bi = (lat - bs[0]) / (bs[-1] - bs[0]) * (len(bs) - 1)
    di = np.interp(d, D_MID, np.arange(STEPS))
    ok = (np.abs(np.degrees(lat)) < LAT - 2.0) & (d > 0.04)
    tau_s = np.zeros(len(X), np.float32)
    tau_s[ok] = map_coordinates(tau, np.stack([li[ok], bi[ok], di[ok]]), order=1, mode="nearest")
    flux = Lum / np.maximum(d, 0.04) ** 2 * np.exp(-tau_s * 0.95)
    return lon[ok], lat[ok], flux[ok], kind[ok]


# ---- 3. the drawing ----------------------------------------------------------------------------------------------------------------------
def clean(mask, min_area):
    """bold shapes: no specks, no pinholes."""
    mask = binary_opening(mask, structure=np.ones((3, 3)), iterations=1)
    mask = binary_closing(mask, structure=np.ones((3, 3)), iterations=2)
    lab, n = label(mask)
    if n:
        areas = np.bincount(lab.ravel())
        small = areas < min_area
        small[0] = False
        mask = mask & ~small[lab]
    holes = ~mask
    lab, n = label(holes)
    if n:
        areas = np.bincount(lab.ravel())
        small = areas < min_area
        small[0] = False
        mask = mask | small[lab]
    return mask


def smooth_mask(mask, sigma):
    """round off the edge of a shape (the contours of a threshold are rough at the scale of a texel): a hard edge that runs smoothly."""
    return gaussian_filter(mask.astype(np.float32), sigma, mode="wrap") > 0.5


def clump_noise(shape, rng, sigma):
    n = gaussian_filter(rng.standard_normal(shape).astype(np.float32), sigma, mode="wrap")
    return n / (n.std() + 1e-9)


def build(seed=3, preview=False):
    t0 = time.time()
    rng = np.random.default_rng(seed)
    ray = RAY if not preview else 8.0
    w_r, h_r = int(W / ray), int(H / ray)
    lons = (np.arange(w_r) + 0.5) / w_r * 2 * math.pi - math.pi
    lats = np.radians(LAT - (np.arange(h_r) + 0.5) / h_r * 2 * LAT)

    def log(a, b):
        if a % 80 < 8 or a >= b:
            print(f"  marching {a}/{b} rows   {time.time() - t0:5.1f} s", flush=True)

    print("1. the glow (ray marching)")
    glow, near = march(lats, lons, log=log)
    pad = 8

    def enlarge(a):
        g = np.concatenate([a[:, -pad:], a, a[:, :pad]], axis=1)
        big = zoom(g, (H / h_r, (W + 2 * pad * W / w_r) / g.shape[1]), order=3, mode="nearest")
        off = int(round(pad * W / w_r))
        return np.clip(big[:, off:off + W], 0, None).astype(np.float32)

    glow, near = enlarge(glow), enlarge(near)

    print("2. the flat tones", f"{time.time() - t0:5.1f} s")
    # clumps at the scale of an arm: the edges of the tones are organic, not the contours of a smooth hill
    clump = clump_noise(glow.shape, rng, 60.0 * PPD / 20.0) * 0.7 + clump_noise(glow.shape, rng, 22.0 * PPD / 20.0) * 0.3
    ref = np.percentile(glow, 99.0)
    v = glow / ref
    tone = np.arcsinh(v * 6.0) / np.arcsinh(6.0)
    tone = tone * (1.0 + 0.05 * clump)
    tone = gaussian_filter(tone, 3.2 * PPD / 20.0, mode="wrap")
    lat_deg = LAT - (np.arange(H) + 0.5) / H * 2 * LAT
    fade = smoothstep(LAT, LAT - 12.0, np.abs(lat_deg))[:, None]       # (nothing at the top and bottom edge of the picture)
    tone = tone * fade
    # THE DARK LANES (the Great Rift): where the dust of the first 3.5 kpc is thick, the band is cut out of the tones: a hard black shape, like a blade through it
    nearf = gaussian_filter(near, 3.0 * PPD / 20.0, mode="wrap")
    band = (np.abs(lat_deg) < 14.0)[:, None] & (tone > 0.0)
    lane_t = float(np.percentile(nearf[band], 100.0 - LANE_COVER))
    sm = 3.5 * PPD / 20.0
    lane = smooth_mask(clean(nearf > lane_t, 700 * (PPD / 20.0) ** 2) & band, sm)
    # the tones are cut where a given share of the picture is covered (a threshold on the brightness would depend on the model's numbers): 34 % / 13 % / 3.5 %
    T1, T2, T3 = (float(np.percentile(tone, 100.0 - q)) for q in COVER)
    area = 1400 * (PPD / 20.0) ** 2
    m1 = clean((tone > T1) & ~lane, area)
    m2 = clean((tone > T2) & ~lane, area) & m1
    m3 = clean((tone > T3) & ~lane, area * 0.6) & m2
    m1, m2, m3 = smooth_mask(m1, sm), smooth_mask(m2, sm) & m1, smooth_mask(m3, sm) & m2
    # three flat tones (the picture is added to the sky: these are the amounts of light, x4 of what the game shows - it draws the picture at a quarter of its strength)
    L1, L2, L3, LINE = 40, 84, 132, 100
    img = np.zeros((H, W), np.uint8)
    img[m1] = L1
    img[m2] = L2
    img[m3] = L3
    # a hairline along the outer edge of the band (the ink of the HUD): one texel and a half of the outline of the first tone, only where it is a real edge
    edge = m1 & ~binary_erosion(m1, structure=np.ones((3, 3)), iterations=int(round(1.5 * PPD / 20.0)) + 1)
    img[edge] = np.maximum(img[edge], LINE)
    print("   covered by the tones: %.1f %%  %.1f %%  %.1f %%" % (100 * m1.mean(), 100 * m2.mean(), 100 * m3.mean()))

    print("3. the stars", f"{time.time() - t0:5.1f} s")
    grid = tau_grid()
    lon, lat, flux, kind = sample_stars(2_400_000 if not preview else 300_000, rng, grid)
    order = np.argsort(-flux)[: (2600 if not preview else 600)]
    lon_d = np.degrees(lon[order]); lat_d = np.degrees(lat[order]); f = flux[order]; k = kind[order]
    # three tiers by brightness (hard steps, like the tones): the brightest 3 % get a cross
    tier = np.zeros(len(order), np.int8)
    tier[: int(0.28 * len(order))] = 1
    tier[: int(0.03 * len(order))] = 2
    return img, (lon_d, lat_d, tier, k)


def write_header(path, stars):
    lon, lat, tier, kind = stars
    with open(path, "w", newline="\n") as fh:
        fh.write("#pragma once\n")
        fh.write("// GENERATED by tools/make_galaxy.py - do not edit. The brightest stars of the simulated galaxy: longitude / latitude in degrees on the panorama (galaxy.png:\n")
        fh.write("// x = (lon + 180) * 20 px, y = (40 - lat) * 20 px), a tier 0..2 (small / middle / big with a cross) and a tint 0 ivory / 1 cool / 2 warm.\n")
        fh.write("namespace GalaxyStars\n{\n")
        fh.write("struct Star\n{\n    float lon, lat;\n    unsigned char tier, tint;\n};\n")
        fh.write("constexpr int COUNT = %d;\n" % len(lon))
        fh.write("constexpr Star STARS[COUNT] = {\n")
        for i in range(len(lon)):
            fh.write("    {%.2ff, %.2ff, %d, %d},\n" % (lon[i], lat[i], tier[i], kind[i]))
        fh.write("};\n}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", default="galaxy.png")
    ap.add_argument("--stars", default="GalaxyStars.h")
    ap.add_argument("--preview", action="store_true", help="a quick run to look at a change (the picture is written, the header is not)")
    ap.add_argument("--seed", type=int, default=3)
    a = ap.parse_args()
    img, stars = build(a.seed, a.preview)
    Image.fromarray(np.stack([img, img, img], axis=-1)).save(a.png, optimize=True)
    if not a.preview:
        write_header(a.stars, stars)
    else:
        np.save(a.png.replace(".png", "_stars.npy"), np.stack([stars[0], stars[1], stars[2], stars[3]]))
    print("written", a.png, img.shape)


if __name__ == "__main__":
    main()
