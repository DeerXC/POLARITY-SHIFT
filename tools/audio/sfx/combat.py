"""What happens to an enemy, and the enemies' own cues.

THE FAMILY OF IMPACTS (version 9). A hit, a shield and a kill are the same few layers heard at three sizes (common.impact): a SNAP of noise, a BODY that falls in pitch, a BOLT
(the energy of the weapon) and DEBRIS. Version 8 made them from a wooden bar and a glass bar: it was one instrument, and a struck bar is not a weapon. Now:
    HIT      snap + body + bolt, a little air and a few chips; blue is bright and glassy (the glass layer is on top), red is dark and bitten (the body is on top)
    SHIELD   a dull slap of noise and a click, nothing above 7 kHz and nothing that has a pitch: it says 'nothing happened'
    KILL     the same snap and body as the hit, then it opens: a puff of air that falls in pitch, a few chips, and a figure of three notes rising behind it (quiet: 'it is gone')
The materials differ in the weight of the body, the band of the snap and what rings: armour is low and dull, the sniper's glass is thin and bright, the rival strikes twice.

VERSION 10. Three sounds were wrong, and what was wrong with each is a rule for the others:
    the KILL was too explosive: a light fighter is a small thing, and a blast that falls from 6 kHz to 350 Hz with a boom under it is the size of a building. Now it is a pop: a short
      puff, a soft body, a few chips; the loudness of its class is 3 dB lower.
    the SHIELD sounded like a slapped bucket: a sine that falls from 150 Hz to 66 Hz and a wooden partial an octave under the note is a hollow body that rings (a 'boink'). Now there is
      no tone in it at all: noise through a low-pass that closes, a click, a breath of fizz.
    the LOCK of the charger sounded like an item that was picked up: three notes that rise (D A D) are a reward, and the music plays that very figure for a win. A lock is the opposite:
      one pitch, pings that come faster and faster, then one hard latch and a held tone on that same pitch. Nothing in it rises or falls.
"""
import numpy as np

from .common import P, impact
from .dsp import (T, BAR, Mix, PLATE, WOOD, crack, crackle, drive, env, fit, glass, harm, lp, modal, moving_band, n_, noise, phase, room, saw_amps, sizzle, smooth, sweep, thump, tick)
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
               tone=(lambda r: modal(f, 0.2, PLATE, r, 0.5), 0.32))
    return fit(m.out(), 0.24) * env(0.24, 0.0005, None, 0.04)


@sfx("boss_armor", "shield", 3)
def boss_armor(rng, v):
    """THE SHIELD (an elite's, the boss's armour): it says 'nothing happened' and it must not say anything else. Version 8 was a ringing glass, version 9 a hollow bucket (a sine
    that falls from 150 Hz and a wooden partial under the note: a body that rings). This one has NO TONE in it: a slap of noise through a low-pass that closes while it dies
    (damped plate: nothing rings, nothing has a pitch), a lower slap of noise under it for the weight (also without a pitch: a sine here is the bucket), a hard click on top,
    and a breath of fizz that falls (the energy being taken). 110 ms."""
    dur = 0.12
    t = T(dur)
    cut = [650.0, 800.0, 980.0][v]
    slap = lp(noise(dur, rng), cut) * np.exp(-t / 0.020)
    weight = lp(noise(dur, rng), 300.0) * np.exp(-t / 0.026)
    m = Mix(dur)
    m.add(slap, 0.0, 1.0)
    m.add(weight, 0.0, 1.1)
    m.add(crack(0.03, rng, 1300, 5200, 0.0035), 0.0, 0.70)
    m.add(moving_band(noise(0.06, rng), (6500, 2200)) * env(0.06, 0.002, 0.02, 0.02), 0.002, 0.20)
    return fit(m.out(), dur) * env(dur, 0.0004, None, 0.03)


# ==========================================================================================================================================================
# KILLS
# ==========================================================================================================================================================
def _rising_figure(m, rng, root, at, gains=(0.10, 0.12, 0.14), step=0.05, size=1.0):
    """three notes of glass that rise (the root, a fourth, an octave): 'it is gone'. Quiet: it ties the kill to the key of the music, it is not the sound."""
    for i, mult in enumerate((1.0, 4.0 / 3.0, 2.0)):
        m.add(glass(root * mult, 0.3 * size + 0.1, rng, tau=0.3 * size), at + i * step, gains[i])


@sfx("enemy_explosion", "kill", 3)
def enemy_explosion(rng, v):
    """the end of a light fighter. Version 10: a POP, not a blast (a blast that falls from 6 kHz to 350 Hz with a boom under it is the size of a building, for a thing that is the
    size of a hand). The same snap as the hit, a soft low body that is gone in 70 ms, a puff of air that falls from 3.2 kHz to 400 Hz and a few chips, and the three notes of glass
    that rise behind it ('it is gone') are a little louder than before and as loud as the puff: that is what makes it a defeat and not an explosion. (Not much louder: three
    notes that rise are also the sound of something picked up.) 360 ms."""
    root = P(*[("D", 5), ("E", 5), ("G", 5)][v])
    m = Mix(0.4)
    impact(rng, dur=0.4, snap=(1000, 6000, 0.007, 0.60), body=(135, 52, 0.060, 0.80), m=m)
    m.add(moving_band(noise(0.22, rng), (3200, 400)) * env(0.22, 0.003, 0.05, 0.05), 0.008, 0.50)
    m.add(crackle(0.2, rng, 600, 1500, 6500, 0.06), 0.02, 0.20)
    _rising_figure(m, rng, root, 0.06, gains=(0.12, 0.14, 0.16), step=0.055)
    return fit(m.out(), 0.4) * env(0.4, 0.0005, None, 0.09)


@sfx("elite_explosion", "kill_heavy")
def elite_explosion(rng, v):
    """the end of an elite: the same pop, bigger and lower - a body an octave down, a puff that falls from 4 kHz to 200 Hz, a rattle of chips, the figure of notes an octave down and
    two more above it. (Version 10: about half of the blast of version 9, and its own class, 3 dB under the explosions of the boss.)"""
    root = P("D", 4)
    m = Mix(0.8)
    impact(rng, dur=0.8, snap=(800, 6000, 0.010, 0.65), body=(115, 40, 0.11, 0.85), m=m)
    m.add(moving_band(noise(0.5, rng), (4000, 200)) * env(0.5, 0.003, 0.12, 0.08), 0.01, 0.50)
    m.add(crackle(0.5, rng, 700, 900, 5500, 0.12), 0.02, 0.25)
    for _ in range(4):
        m.add(modal(float(np.exp(rng.uniform(np.log(900), np.log(3500)))), 0.25, PLATE, rng, 0.3), rng.uniform(0.02, 0.25), rng.uniform(0.04, 0.10))
    _rising_figure(m, rng, root, 0.07, gains=(0.13, 0.15, 0.17), step=0.07, size=1.8)
    for i, (name, octave) in enumerate([("A", 5), ("D", 6)]):
        m.add(glass(P(name, octave), 0.5, rng, tau=0.5), 0.28 + i * 0.07, 0.12)
    return room(m.out(), 0.4, 0.14)


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
    """THE CHARGER LOCKS ITS LANE. Version 10: a LOCK-ON, not a reward. (Version 9 rose D A D, and the music plays that very figure for a win; the version before that was a growl,
    dull and loud at once.) Everything here is ONE PITCH, A6 - 1.76 kHz, at the lower edge of the band where the music is not: six hard pings that come faster and faster (the lane is following the ship), then, at
    the moment that the lane is locked (the last 14 of the 45 steps of the warning: 0.52 s in), a latch - a metal click and a short low thunk - and a held tone on the same pitch
    that lasts until the charge starts. Nothing in it rises and nothing falls: a held, steady tone is what a lock sounds like. 0.78 s."""
    f = P("A", 6)
    m = Mix(0.8)
    for i, t in enumerate([0.0, 0.14, 0.255, 0.35, 0.43, 0.50]):
        g = 0.50 + 0.07 * i
        m.add(modal(f, 0.12, [(1.0, 1.0, 0.035), (2.0, 0.14, 0.012)], rng) * smooth(T(0.12) / 0.0012), t, g)
        m.add(tick(rng, 4200, 0.0008), t, 0.22)
    lock = 0.54
    m.add(crack(0.03, rng, 2200, 9000, 0.004), lock, 0.80)                                          # the latch: a metal click ...
    m.add(modal(2300.0, 0.06, [(1.0, 1.0, 0.012), (1.93, 0.4, 0.008)], rng), lock, 0.35)
    m.add(thump(120, 78, 0.09, 0.025), lock, 0.55)                                                  # ... and a short, dry thunk under it
    held = 0.22
    tone = (np.sin(phase(np.full(n_(held), f))) + 0.10 * np.sin(phase(np.full(n_(held), f * 2.0)))) * env(held, 0.004, None, 0.045)
    m.add(tone, lock, 0.50)                                                                         # the held tone on the pitch of the pings
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
