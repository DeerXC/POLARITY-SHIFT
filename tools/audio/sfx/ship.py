"""The ship: its shots, its colours, its energy, its hurt, its beam and its modules.

BLUE = glass (a clean sine on top, a ring); RED = ink (a hard attack, a bite, no tail). The shots are the most frequent sounds of the game, so they are the smallest: a bolt
(a zap), a snap of noise and a breath of air - not a note struck on a bar."""
import numpy as np

from .common import P, run
from .dsp import (T, TWO_PI, Mix, PLATE, WOOD, bell, brass, crack, drive, env, fit, formant_pad, glass, harm, lp, modal, moving_band, noise, phase, pluck, room, saw_amps,
                  shimmer, smooth, sweep, thump, tick, zap)
from .registry import sfx


# ==========================================================================================================================================================
# THE SHOTS
# ==========================================================================================================================================================
@sfx("player_shot_blue", "shot", 4)
def player_shot_blue(rng, v):
    """a bolt of glass: a sine that falls fast and ends on a note of the palette (D E G A, octave 6), the octave above it very quiet, a snap of noise on top and a breath of
    air behind. 70 ms."""
    end = P(*[("D", 6), ("E", 6), ("G", 6), ("A", 6)][v])
    m = Mix(0.1)
    m.add(zap(end * 2.7, end, 0.07, "sine"), 0, 1.0)
    m.add(zap(end * 5.4, end * 2.0, 0.045, "sine"), 0, 0.22)
    m.add(crack(0.03, rng, 3500, 11000, 0.005), 0, 0.42)
    m.add(moving_band(noise(0.06, rng), (6500, 2200)) * env(0.06, 0.002, 0.022, 0.02), 0.002, 0.20)
    return fit(m.out(), 0.1) * env(0.1, 0.0004, None, 0.02)


@sfx("player_shot_red", "shot", 4)
def player_shot_red(rng, v):
    """a bolt of ink: a saw that falls fast with a bite, a body under it (a short thump) and a darker snap. Lower and a little shorter than the blue one."""
    end = P(*[("D", 5), ("E", 5), ("G", 5), ("A", 5)][v])
    m = Mix(0.09)
    m.add(lp(drive(zap(end * 2.3, end, 0.06, "saw"), 1.6), 5200), 0, 0.85)
    m.add(thump(260, 110, 0.05, 0.016), 0, 0.55)
    m.add(crack(0.03, rng, 1200, 6500, 0.005), 0, 0.55)
    return fit(m.out(), 0.09) * env(0.09, 0.0004, None, 0.02)


@sfx("polarity_switch_blue", "ui")
def polarity_switch_blue(rng, v):
    """to blue: a glass glide up, landing on A (up a fifth: bright, open)."""
    m = Mix(0.45)
    f = sweep(P("A", 5) * 0.5, P("A", 5), 0.11)
    m.add(np.sin(phase(f)) * env(0.11, 0.01, 0.06, 0.02), 0, 0.4)
    m.add(moving_band(noise(0.25, rng), (900, 6500)) * env(0.25, 0.06, 0.08), 0, 0.22)
    m.add(glass(P("A", 5), 0.4, rng, tau=0.8), 0.09, 0.75)
    m.add(glass(P("A", 6), 0.3, rng, tau=0.5), 0.09, 0.3)
    return room(m.out(), 0.7, 0.22)


@sfx("polarity_switch_red", "ui")
def polarity_switch_red(rng, v):
    """to red: a blade, landing on D (down a fifth: hard, closed)."""
    m = Mix(0.3)
    m.add(moving_band(noise(0.12, rng), (7000, 1600)) * env(0.12, 0.002, 0.05, 0.02), 0, 0.5)
    m.add(brass(P("D", 4), 0.14, rng, vel=0.85, attack=0.004, scoop=0.04, rel=0.05), 0.04, 0.8)
    m.add(modal(P("D", 5), 0.12, PLATE, rng, 0.3), 0.04, 0.3)
    m.add(tick(rng, 3200, 0.002), 0, 0.6)
    return m.out()


@sfx("absorb_blue", "absorb", 3)
def absorb_blue(rng, v):
    """the base of the chain is D5: the engine climbs D E G A C D (AudioLogic::ChainSemitones). A soft glass drop."""
    f0 = P("D", 5)
    n = int(0.2 * 44100)
    t = np.arange(n) / 44100
    f = f0 * (0.97 + 0.03 * smooth(t / 0.035))
    x = np.sin(phase(f)) + [0.25, 0.4, 0.15][v] * np.sin(phase(f * 2.0)) + 0.12 * np.sin(phase(f * 2.756))
    return x * env(0.2, 0.007, [0.055, 0.07, 0.045][v], 0.02)


@sfx("absorb_red", "absorb", 3)
def absorb_red(rng, v):
    f = P("D", 5)
    m = Mix(0.16)
    m.add(lp(brass(f, 0.1, rng, vel=0.7 + 0.05 * v, attack=0.004, scoop=0.03, rel=0.05, law=0.5), 5200), 0, 0.9)
    m.add(tick(rng, 4500, 0.001), 0, 0.5)
    return fit(m.out(), 0.16) * env(0.16, 0.001, 0.05, 0.02)


@sfx("absorb_chime", "chime")
def absorb_chime(rng, v):
    m = Mix(0.9)
    m.add(bell(P("D", 6), 0.9, rng, 0.9), 0, 0.7)
    m.add(glass(P("D", 7), 0.6, rng, tau=0.8), 0.01, 0.35)
    m.add(shimmer(0.7, rng, 4500, 10000, 8, 0.35, 0.1), 0.02, 1.0)
    return room(m.out(), 0.9, 0.3)


@sfx("energy_full", "jingle")
def energy_full(rng, v):
    """the beam is ready: a triad shoots up (D A D), the top rings and the bar 'clicks' shut."""
    m = Mix(0.9)
    for i, (name, octave) in enumerate([("D", 5), ("A", 5), ("D", 6)]):
        m.add(glass(P(name, octave), 0.6, rng, tau=0.8), i * 0.055, 0.75)
    m.add(glass(P("A", 6), 0.7, rng, tau=1.2), 0.17, 0.5)
    m.add(modal(P("D", 4), 0.2, WOOD, rng, 1.5), 0.17, 0.5)
    m.add(shimmer(0.7, rng, 4000, 10000, 10, 0.3, 0.1), 0.15, 1.0)
    m.add(tick(rng, 3000), 0.0, 0.6)
    return room(m.out(), 0.9, 0.28)


# ==========================================================================================================================================================
# HURT AND DEATH
# ==========================================================================================================================================================
@sfx("player_damage", "damage")
def player_damage(rng, v):
    m = Mix(0.5)
    m.add(thump(190, 48, 0.4, 0.09, 0.8, rng), 0, 1.0)
    m.add(crack(0.12, rng, 1200, 7000, 0.02), 0, 0.9)
    m.add(modal(P("A", 4) * 0.62, 0.3, PLATE, rng, 0.7), 0, 0.45)
    f = sweep(P("A", 4), P("D", 4), 0.28)
    m.add(lp(drive(harm(f, 0.28, saw_amps(16)), 3.0), 2600) * env(0.28, 0.004, 0.12, 0.03), 0.0, 0.55)
    return m.out()


@sfx("player_death", "death")
def player_death(rng, v):
    m = Mix(1.9)
    m.add(thump(120, 30, 1.0, 0.3, 0.8, rng), 0, 1.0)
    m.add(moving_band(noise(0.7, rng), (6000, 300)) * env(0.7, 0.003, 0.22, 0.1), 0, 0.8)
    m.add(crack(0.15, rng, 1000, 8000, 0.03), 0, 0.9)
    f = sweep(P("D", 4), P("D", 2), 1.2)
    m.add(drive(harm(f, 1.2, saw_amps(14)), 2.2) * env(1.2, 0.01, 0.55, 0.2), 0.0, 0.45)
    for _ in range(18):                                    # the glass of the cockpit: pings that fall
        st = rng.uniform(0.02, 0.7)
        fr = np.exp(rng.uniform(np.log(1800), np.log(7000))) * (1.0 - 0.4 * st)
        m.add(glass(fr, 0.4, rng, tau=0.3), st, rng.uniform(0.1, 0.3))
    m.add(bell(P("D", 3), 1.8, rng, 1.2), 0.45, 0.35)
    return room(m.out(), 1.4, 0.3, damp=0.8)


# ==========================================================================================================================================================
# THE BEAM, THE REPAIR, THE REVIVE
# ==========================================================================================================================================================
@sfx("special_shot", "beam")
def special_shot(rng, v):
    """THE PIERCING BEAM: the sword-beam of an adventure. A strike of glass on D and A with a bar of air that rises, a clean sub, a chord of PURE tones (D A D E A D) that
    swells and sings with a little vibrato, and a cascade of glass that falls from the top (A G E D A) like the ring of a blade. No saw, no drive, no tremolo: nothing in it
    is a motor. (The picture is BeamFx: a braid of two colours round a white core.)"""
    dur = 1.5
    m = Mix(dur)
    for name, octave, g in [("D", 6, 1.0), ("A", 6, 0.8), ("D", 7, 0.45)]:                  # the strike
        m.add(glass(P(name, octave), 0.9, rng, tau=1.1), 0, 0.5 * g)
    m.add(moving_band(noise(0.16, rng), (1800, 9500)) * env(0.16, 0.004, 0.06, 0.04), 0, 0.42)
    m.add(tick(rng, 4200, 0.0012), 0, 0.5)
    m.add(thump(115, 38, 0.55, 0.15), 0, 0.85)                                              # the sub: a clean sine that falls
    body_dur = dur - 0.05
    t = T(body_dur)
    chord = np.zeros(len(t))
    for name, octave, g in [("D", 3, 1.0), ("A", 3, 0.8), ("D", 4, 0.7), ("E", 4, 0.30), ("A", 4, 0.5), ("D", 5, 0.32)]:
        f = P(name, octave) * (1.0 + 0.0012 * rng.standard_normal())
        ph = phase(f * (1.0 + 0.0028 * np.sin(TWO_PI * 5.2 * t + rng.uniform(0, 6.28)) * smooth((t - 0.1) / 0.3)))
        chord += g * (np.sin(ph) + 0.16 * np.sin(2.0 * ph))
    m.add(chord * env(body_dur, 0.07, 0.5, 0.35), 0.05, 0.28)                              # the radiance
    run(m, rng, 0.17, 0.075, [("A", 6), ("G", 6), ("E", 6), ("D", 6), ("A", 5)], "glass", 0.36, 0.9, 0.7)   # the ring
    m.add(moving_band(noise(1.2, rng), (5200, 1500)) * env(1.2, 0.15, 0.5, 0.3), 0.1, 0.09)
    m.add(shimmer(1.0, rng, 3500, 10000, 14, 0.5, 0.1), 0.12, 1.0)
    return room(m.out(), 0.9, 0.2)


@sfx("heal", "jingle")
def heal(rng, v):
    """repair: the little run of a Cure spell, a warm swell under it, a bell on top."""
    m = Mix(1.1)
    run(m, rng, 0.0, 0.05, [("D", 5), ("E", 5), ("G", 5), ("A", 5), ("C", 6), ("D", 6), ("E", 6)], "glass", 0.5, 0.5, 0.5)
    m.add(formant_pad(P("D", 4), 0.6, rng, "o", 3, 0.2, 0.4), 0.0, 0.3)
    m.add(bell(P("D", 7), 0.8, rng, 0.8), 0.33, 0.28)
    m.add(shimmer(0.7, rng, 4000, 10000, 10, 0.4, 0.1), 0.2, 1.0)
    return room(m.out(), 1.0, 0.28)


@sfx("aegis_block", "ui")
def aegis_block(rng, v):
    m = Mix(0.45)
    m.add(modal(P("A", 5), 0.4, PLATE, rng, 1.3), 0, 0.8)
    m.add(tick(rng, 4500, 0.002), 0, 0.7)
    m.add(thump(160, 80, 0.12, 0.035), 0, 0.5)
    m.add(glass(P("A", 6), 0.3, rng, tau=0.5), 0.01, 0.3)
    return room(m.out(), 0.5, 0.15)


@sfx("backup_revive", "death")
def backup_revive(rng, v):
    """the backup core: a heartbeat, then a harp glissando up the scale, a choir comes in under it, a bell on the top. (The light that gathers is the first 0.28 s: the hit
    stop of the revive is 0.3 s long - the picture is ModulesDraw DrawReviveBlast, and the shock rings are born at 0.28 s, with the first note of the bell.)"""
    dur = 1.9
    m = Mix(dur)
    m.add(thump(90, 40, 0.3, 0.1), 0.0, 0.6)
    m.add(thump(80, 36, 0.3, 0.1), 0.15, 0.45)
    notes = []
    for octave in (3, 4, 5, 6):
        for name in ("D", "E", "G", "A", "C"):
            notes.append((name, octave + (1 if name == "C" else 0)))
    for i, (name, octave) in enumerate(notes[:20]):
        m.add(pluck(P(name, octave), 0.7, rng, tau=0.5), i * 0.014, 0.12 + 0.35 * i / 20)        # the rise is quick: it is the light that gathers
    m.add(moving_band(noise(0.3, rng), (500, 7000)) * env(0.3, 0.2, None, 0.05), 0.0, 0.25)
    m.add(crack(0.08, rng, 1500, 9000, 0.018), 0.28, 0.7)                                      # the flash
    m.add(thump(120, 34, 0.5, 0.14), 0.28, 0.8)
    m.add(formant_pad(P("D", 4), 1.2, rng, "a", 4, 0.55, 0.5), 0.2, 0.5)
    m.add(formant_pad(P("A", 4), 1.0, rng, "a", 3, 0.6, 0.5), 0.3, 0.3)
    m.add(bell(P("D", 6), 1.4, rng, 1.2), 0.28, 0.5)
    m.add(glass(P("A", 6), 1.0, rng, tau=1.4), 0.28, 0.3)
    m.add(shimmer(1.0, rng, 3500, 10000, 18, 0.6, 0.1), 0.28, 1.0)
    return room(m.out(), 1.4, 0.3)


@sfx("echo_arc", "ui")
def echo_arc(rng, v):
    m = Mix(0.35)
    f = sweep(3600, 800, 0.1)
    fm = f * (1 + 0.5 * np.sin(TWO_PI * 190 * T(0.1)))
    m.add(np.sin(phase(fm)) * env(0.1, 0.001, 0.05, 0.02), 0, 0.5)
    for _ in range(7):
        m.add(tick(rng, rng.uniform(2500, 6000), 0.0012), rng.uniform(0, 0.16), rng.uniform(0.2, 0.5))
    m.add(glass(P("E", 6), 0.25, rng, tau=0.3), 0.05, 0.4)
    return m.out()


@sfx("tactic_decoy", "ui")
def tactic_decoy(rng, v):
    m = Mix(0.7)
    f = sweep(400, 1000, 0.1)
    m.add(np.sin(phase(f)) * env(0.1, 0.005, 0.07, 0.02), 0, 0.3)
    m.add(modal(P("D", 5), 0.3, WOOD, rng, 1.5), 0.03, 0.8)
    m.add(modal(P("A", 5), 0.3, WOOD, rng, 1.5), 0.14, 0.8)
    m.add(moving_band(noise(0.12, rng), (3000, 900)) * env(0.12, 0.002, 0.05, 0.02), 0.26, 0.4)
    m.add(glass(P("D", 6), 0.4, rng, tau=0.5), 0.27, 0.45)
    return room(m.out(), 0.6, 0.18)


@sfx("tactic_dash", "ui")
def tactic_dash(rng, v):
    m = Mix(0.4)
    air = noise(0.34, rng)
    cur = np.concatenate([sweep(250, 5200, 0.14), sweep(5200, 700, 0.2)])
    m.add(moving_band(air, cur) * env(0.34, 0.03, 0.12, 0.06), 0, 0.8)
    f = np.concatenate([sweep(P("E", 5), P("E", 6), 0.12), np.full(int(0.06 * 44100), P("E", 6))])
    m.add(np.sin(phase(f)) * env(0.18, 0.005, 0.07, 0.04), 0, 0.2)
    m.add(thump(110, 55, 0.15, 0.05), 0, 0.5)
    return m.out()


@sfx("low_hp_pulse", "peak")
def low_hp_pulse(rng, v):
    m = Mix(0.6)
    m.add(thump(72, 40, 0.3, 0.09), 0.0, 1.0)
    m.add(thump(64, 36, 0.3, 0.08), 0.2, 0.7)
    return lp(m.out(), 160)
