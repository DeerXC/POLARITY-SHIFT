"""What happens to an enemy, and the enemies' own cues.

THE FAMILY OF IMPACTS (version 9). A hit, a shield and a kill are the same few layers heard at three sizes (common.impact): a SNAP of noise, a BODY that falls in pitch, a BOLT
(the energy of the weapon) and DEBRIS. Version 8 made them from a wooden bar and a glass bar: it was one instrument, and a struck bar is not a weapon. Now:
    HIT      snap + body + bolt, a little air and a few chips; blue is bright and glassy (the glass layer is on top), red is dark and bitten (the body is on top)
    SHIELD   the body and a dull snap, nothing above 2.5 kHz: it says 'nothing happened'
    KILL     the same snap and body as the hit, then it opens: a blast of noise that falls in pitch, debris, and a figure of three notes rising behind it (quiet: 'it is gone')
The materials differ in the weight of the body, the band of the snap and what rings: armour is low and dull, the sniper's glass is thin and bright, the rival strikes twice."""
import numpy as np

from .common import P, impact
from .dsp import (T, BAR, Mix, PLATE, WOOD, crack, crackle, drive, env, fit, glass, harm, lp, modal, moving_band, n_, noise, phase, room, saw_amps, smooth, sweep, thump, tick)
from .registry import sfx


# ==========================================================================================================================================================
# HITS
# ==========================================================================================================================================================
@sfx("enemy_hit_blue", "hit", 4)
def enemy_hit_blue(rng, v):
    """a blue shot on a light fighter: a snap, a bolt that falls onto a note of the palette (E G A C, octave 6-7), a small body, shards. Bright."""
    g = P(*[("E", 6), ("G", 6), ("A", 6), ("C", 7)][v])
    m = impact(rng, dur=0.15,
               snap=(3000, 11000, 0.006, 0.55),
               bolt=(g * 2.4, g, 0.06, "sine", 0.55),
               body=(300, 140, 0.022, 0.42),
               air=(2500, 8000, 0.03, 0.16),
               debris=(700, 3000, 9000, 0.03, 0.14),
               tone=(lambda r: glass(g * 0.5, 0.1, r, tau=0.12), 0.16))
    return fit(m.out(), 0.15) * env(0.15, 0.0004, None, 0.03)


@sfx("enemy_hit_red", "hit", 4)
def enemy_hit_red(rng, v):
    """a red shot on a light fighter: a darker snap, a body that is heavier than the blue one, and a bite of saw that falls onto D E G A (octave 4). Lower and shorter."""
    w = P(*[("D", 4), ("E", 4), ("G", 4), ("A", 4)][v])
    m = impact(rng, dur=0.12,
               snap=(1000, 6500, 0.007, 0.75),
               body=(240, 80, 0.032, 0.95),
               bolt=(w * 1.8, w, 0.05, "saw", 0.42),
               debris=(500, 1200, 5000, 0.025, 0.16))
    return fit(m.out(), 0.12) * env(0.12, 0.0004, None, 0.03)


@sfx("elite_hit", "hit_heavy", 3)
def elite_hit(rng, v):
    """an elite, in general: both layers, a heavier body, a sheet of metal that rings a little under it."""
    w = P(*[("D", 4), ("E", 4), ("G", 4)][v])
    m = impact(rng, dur=0.26,
               snap=(1200, 8000, 0.008, 0.70),
               body=(170, 65, 0.05, 1.0),
               bolt=(w * 2.2, w, 0.08, "tri", 0.40),
               debris=(500, 1500, 6000, 0.04, 0.22),
               tone=(lambda r: modal(w, 0.2, PLATE, r, 0.5), 0.22))
    return fit(m.out(), 0.26) * env(0.26, 0.0005, None, 0.04)


@sfx("enemy_hit_armor", "hit_heavy", 3)
def enemy_hit_armor(rng, v):
    """heavy, charger, turret: thick plate. A dull snap, a big body, a low clank that is gone quickly, a rattle."""
    w = P(*[("D", 3), ("E", 3), ("G", 3)][v])
    m = impact(rng, dur=0.30,
               snap=(700, 4500, 0.010, 0.80),
               body=(140, 52, 0.065, 1.15),
               debris=(350, 600, 3500, 0.05, 0.26),
               tone=(lambda r: modal(w, 0.2, PLATE, r, 0.45), 0.36))
    return fit(m.out(), 0.3) * env(0.3, 0.0005, None, 0.05)


@sfx("enemy_hit_glass", "hit_heavy", 3)
def enemy_hit_glass(rng, v):
    """the sniper: thin and bright. A high snap, a bolt that falls from far above, a small body, and a few glass shards that ring on a note (A D E)."""
    g = P(*[("E", 6), ("A", 6), ("D", 7)][v])
    m = impact(rng, dur=0.24,
               snap=(3500, 12000, 0.008, 0.70),
               bolt=(g * 2.6, g, 0.07, "sine", 0.50),
               body=(230, 110, 0.03, 0.50),
               debris=(1100, 4000, 12000, 0.05, 0.30),
               tone=(lambda r: modal(g * 0.5, 0.24, BAR, r, 0.6), 0.34))
    return fit(m.out(), 0.24) * env(0.24, 0.0004, None, 0.04)


@sfx("enemy_hit_clash", "hit_heavy", 3)
def enemy_hit_clash(rng, v):
    """the rival: two strikes a little apart ('tak-tak'), the second higher and with a rising 'shing' of noise: a duel."""
    w1 = P(*[("D", 4), ("E", 4), ("G", 4)][v])
    w2 = P(*[("A", 4), ("A", 4), ("D", 5)][v])
    m = Mix(0.3)
    impact(rng, dur=0.2, snap=(1500, 8000, 0.008, 0.70), body=(180, 80, 0.04, 0.80), bolt=(w1 * 2.0, w1, 0.06, "tri", 0.35),
           tone=(lambda r: modal(w1, 0.15, PLATE, r, 0.4), 0.22), m=m, at=0.0)
    impact(rng, dur=0.24, snap=(2500, 11000, 0.007, 0.65), body=(210, 100, 0.03, 0.55), bolt=(w2 * 0.9, w2 * 2.2, 0.06, "sine", 0.40),
           debris=(800, 2500, 9000, 0.05, 0.22), tone=(lambda r: modal(w2, 0.2, PLATE, r, 0.45), 0.25), m=m, at=0.034)
    m.add(moving_band(noise(0.08, rng), (2200, 8500)) * env(0.08, 0.003, 0.03, 0.02), 0.034, 0.30)
    return fit(m.out(), 0.3) * env(0.3, 0.0004, None, 0.05)


@sfx("boss_hit", "boss_hit", 3)
def boss_hit(rng, v):
    """a shot on the hull of the boss: the family at the size of a ship's hull - a heavy body, a snap, a sheet of metal."""
    f = [250.0, 280.0, 315.0][v]
    m = impact(rng, dur=0.24,
               snap=(1200, 8000, 0.010, 0.80),
               body=(190, 70, 0.06, 1.0),
               debris=(500, 1500, 6000, 0.04, 0.22),
               tone=(lambda r: modal(f, 0.2, PLATE, r, 0.6), 0.32))
    return fit(m.out(), 0.24) * env(0.24, 0.0005, None, 0.04)


@sfx("boss_armor", "boss_hit", 3)
def boss_armor(rng, v):
    """THE SHIELD (an elite's, the boss's armour): a dull snap, a body, a short damped knock and a few chips - nothing above 2.5 kHz and nothing that rings. It says 'nothing
    happened'."""
    w = P(*[("D", 4), ("E", 4), ("G", 4)][v])
    m = Mix(0.24)
    m.add(lp(crack(0.04, rng, 300, 2400, 0.012), 2600), 0, 0.85)
    m.add(thump(150, 66, 0.14, 0.045), 0, 1.0)
    m.add(modal(w * 0.5, 0.14, [(1.0, 1.0, 0.05), (2.136, 0.35, 0.03), (2.918, 0.2, 0.02)], rng), 0, 0.42)
    m.add(crackle(0.12, rng, 200, 400, 2200, 0.03), 0.004, 0.15)
    return fit(m.out(), 0.24) * env(0.24, 0.0005, None, 0.04)


# ==========================================================================================================================================================
# KILLS
# ==========================================================================================================================================================
def _rising_figure(m, rng, root, at, gains=(0.10, 0.12, 0.14), step=0.05, size=1.0):
    """three notes of glass that rise (the root, a fourth, an octave): 'it is gone'. Quiet: it ties the kill to the key of the music, it is not the sound."""
    for i, mult in enumerate((1.0, 4.0 / 3.0, 2.0)):
        m.add(glass(root * mult, 0.3 * size + 0.1, rng, tau=0.3 * size), at + i * step, gains[i])


@sfx("enemy_explosion", "kill", 3)
def enemy_explosion(rng, v):
    """the end of a light fighter: the same snap and body as the hit, then it opens - a blast of noise that falls from 6 kHz to 350 Hz, a boom, debris."""
    root = P(*[("D", 5), ("E", 5), ("G", 5)][v])
    m = Mix(0.5)
    impact(rng, dur=0.5, snap=(1500, 9000, 0.008, 0.80), body=(160, 46, 0.10, 1.0), m=m)
    m.add(moving_band(noise(0.36, rng), (6000, 350)) * env(0.36, 0.002, 0.09, 0.06), 0.008, 0.85)
    m.add(crackle(0.34, rng, 1100, 1200, 8000, 0.10), 0.02, 0.45)
    _rising_figure(m, rng, root, 0.07)
    return fit(m.out(), 0.5) * env(0.5, 0.0005, None, 0.1)


@sfx("elite_explosion", "big")
def elite_explosion(rng, v):
    """the end of an elite: the same, bigger and lower - a long boom, a blast that falls from 5 kHz to 200 Hz, shards of metal, a long rattle, and the figure of notes an octave
    down with two more above."""
    root = P("D", 4)
    m = Mix(1.0)
    impact(rng, dur=1.0, snap=(900, 8000, 0.012, 0.85), body=(130, 38, 0.16, 1.1), m=m)
    m.add(moving_band(noise(0.7, rng), (5000, 200)) * env(0.7, 0.002, 0.17, 0.1), 0.01, 0.85)
    m.add(moving_band(noise(0.3, rng), (9000, 1500)) * env(0.3, 0.002, 0.07, 0.05), 0.0, 0.45)
    m.add(crackle(0.8, rng, 1300, 1000, 7000, 0.22), 0.02, 0.50)
    for _ in range(7):
        m.add(modal(float(np.exp(rng.uniform(np.log(900), np.log(4200)))), 0.3, PLATE, rng, 0.35), rng.uniform(0.02, 0.35), rng.uniform(0.07, 0.17))
    _rising_figure(m, rng, root, 0.08, gains=(0.14, 0.16, 0.18), step=0.07, size=1.8)
    for i, (name, octave) in enumerate([("A", 5), ("D", 6)]):
        m.add(glass(P(name, octave), 0.5, rng, tau=0.5), 0.30 + i * 0.07, 0.16)
    return room(m.out(), 0.5, 0.16)


# ==========================================================================================================================================================
# THE CUES OF THE ENEMIES
# ==========================================================================================================================================================
@sfx("sniper_lock", "alert")
def sniper_lock(rng, v):
    """two cold pings, high and hard-edged: where the music is not."""
    m = Mix(0.34)
    for t in (0.0, 0.1):
        m.add(modal(P("G", 7), 0.15, [(1.0, 1.0, 0.05), (2.0, 0.15, 0.02)], rng) * smooth(T(0.15) / 0.0015), t, 0.9)
        m.add(tick(rng, 6000, 0.0008), t, 0.35)
    m.add(modal(P("G", 6), 0.25, [(1.0, 1.0, 0.09)], rng), 0.1, 0.3)
    return m.out()


@sfx("sniper_fire", "ui")
def sniper_fire(rng, v):
    m = Mix(0.3)
    m.add(crack(0.05, rng, 2500, 10000, 0.006), 0, 1.0)
    f = sweep(3400, 450, 0.13)
    m.add(np.sin(phase(f)) * env(0.13, 0.0005, 0.05, 0.03), 0, 0.6)
    m.add(thump(180, 70, 0.12, 0.04), 0, 0.7)
    return room(m.out(), 0.3, 0.12)


@sfx("charger_warning", "alert")
def charger_warning(rng, v):
    """THE CHARGER LOCKS ITS LANE: a short figure of notes - three strikes that RISE (D A D: the answer of the motto, 'it is coming'), a ping of glass on the last that cuts
    through the music, and a thin glide under them that closes like a lock. All the energy is between 500 Hz and 3.5 kHz; nothing under 250 Hz."""
    m = Mix(0.6)
    for t, name, octave, g in [(0.0, "D", 5, 0.8), (0.11, "A", 5, 1.0), (0.22, "D", 6, 1.0)]:
        m.add(modal(P(name, octave), 0.3, WOOD, rng, 1.3), t, g)
        m.add(tick(rng, 3500, 0.001), t, 0.5)
    m.add(glass(P("A", 6), 0.35, rng, tau=0.5), 0.22, 0.6)
    m.add(glass(P("D", 7), 0.3, rng, tau=0.4), 0.22, 0.3)
    f = sweep(P("D", 5), P("D", 6), 0.22)
    m.add(np.sin(phase(f)) * env(0.22, 0.01, None, 0.03), 0, 0.18)
    return m.out()


@sfx("charger_dash", "ui")
def charger_dash(rng, v):
    m = Mix(0.45)
    m.add(moving_band(noise(0.4, rng), (200, 1800)) * env(0.4, 0.04, 0.17, 0.08), 0, 0.8)
    f = sweep(P("D", 2), P("D", 3), 0.3)
    m.add(lp(drive(harm(f, 0.3, saw_amps(10)), 2.0), 900) * env(0.3, 0.02, 0.14, 0.05), 0, 0.5)
    m.add(thump(120, 50, 0.2, 0.06), 0, 0.6)
    return m.out()


@sfx("rival_twin", "entrance")
def rival_twin(rng, v):
    """THE SECOND RIVAL ARRIVES (wave 5, the duel of the twins). Version 9: a WARP-IN, and everything in it is smooth. The twins are the mirror of the theme, so two pure tones
    move towards each other: a BLUE one that glides UP (D4 to D5) and a RED one that glides DOWN (D6 to A5). They never cross - two tones that pass through the same pitch
    beat against each other, and a beat is what the ear hears as rough - they stop a fifth apart, and the fifth (D5 and A5, two pure tones with a breath of their second
    harmonic) blooms and dies away. A soft sub swells under them and a puff of air opens and closes. No brass (a saw sounds like an alarm), no bell (a bell's partials are not
    whole numbers: they beat), no random pings. Nothing above 5 kHz, nothing sharper than a 12 ms attack, and the whole sound is passed through a gentle low-pass."""
    dur = 1.05
    m = Mix(dur)
    glide = 0.42
    t = T(glide)
    swell = smooth(t / 0.30) * smooth((glide - t) / 0.10)
    m.add(np.sin(phase(sweep(P("D", 4), P("D", 5), glide))) * swell, 0.0, 0.42)
    m.add(np.sin(phase(sweep(P("D", 6), P("A", 5), glide))) * swell, 0.0, 0.26)
    m.add(thump(62, 52, 0.7, 0.20) * smooth(T(0.7) / 0.08), 0.0, 0.40)                               # the sub: a soft swell, never a click
    air = moving_band(noise(0.55, rng), (500, 3200)) * smooth(T(0.55) / 0.30) * smooth((0.55 - T(0.55)) / 0.25)
    m.add(air, 0.0, 0.14)
    for name, octave, g in [("D", 5, 1.0), ("A", 5, 0.8)]:                                           # the meeting: a fifth
        f = P(name, octave)
        tone = (np.sin(phase(np.full(n_(0.7), f))) + 0.12 * np.sin(phase(np.full(n_(0.7), f * 2.0)))) * env(0.7, 0.012, 0.28, 0.1)
        m.add(tone, glide - 0.03, 0.36 * g)
    return room(lp(m.out(), 6000), 0.8, 0.20, damp=0.8)
