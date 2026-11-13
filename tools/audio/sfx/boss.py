"""The boss: its cues (they live where the music does not: pure, hard-edged tones between 1.5 and 5 kHz, and a sub below 100 Hz), its weapons and its end.
Warnings are TONES (they mean something); damage and destruction are NOISE and BODY (they are something)."""
import numpy as np

from .common import P
from .dsp import (T, Mix, PLATE, bell, brass, crack, drive, env, formant_pad, glass, harm, lp, modal, moving_band, n_, noise, phase, room, saw_amps, shimmer, smooth, square_amps,
                  sweep, thump, tick)
from .registry import sfx


@sfx("boss_warning", "boss_alert")
def boss_warning(rng, v):
    """three low brass blasts (D D A), a klaxon over them in the band the music does not use, a rising wash, and a sub."""
    dur = 1.9
    m = Mix(dur)
    for t, name, octave, d in [(0.0, "D", 2, 0.42), (0.5, "D", 2, 0.42), (1.0, "A", 2, 0.7)]:
        m.add(brass(P(name, octave), d, rng, vel=0.95, attack=0.03, scoop=0.03, rel=0.2, law=0.55, slope=0.8), t, 0.8)
        m.add(thump(90, 40, 0.5, 0.12), t, 0.7)
        klaxon = modal(P("A", 6), 0.3, [(1.0, 1.0, 0.2)], rng) + modal(P("E", 7), 0.3, [(1.0, 1.0, 0.2)], rng) * 0.6
        pulse = ((T(0.3) % 0.1) < 0.06).astype(float)
        m.add(klaxon * pulse * env(0.3, 0.003, None, 0.03), t, 0.35)
    m.add(moving_band(noise(1.5, rng), (300, 7000)) * env(1.5, 0.6, None, 0.3), 0.2, 0.3)
    m.add(crack(0.1, rng, 1500, 8000, 0.02), 0, 0.6)
    return room(m.out(), 0.9, 0.22)


@sfx("boss_phase", "boss_big")
def boss_phase(rng, v):
    """the boss changes shape: a hit, a glass rise over the whole range, a stab on D and a cloud of shards."""
    m = Mix(1.4)
    m.add(thump(130, 38, 0.6, 0.16, 0.8, rng), 0, 1.0)
    m.add(crack(0.12, rng, 1500, 9000, 0.02), 0, 0.8)
    f = sweep(P("D", 4), P("D", 7), 0.5)
    m.add(drive(harm(f, 0.5, [1, 0.3, 0.1]), 1.5) * env(0.5, 0.02, None, 0.05), 0.0, 0.3)
    for name, octave in [("D", 3), ("A", 3), ("D", 4), ("A", 4)]:
        m.add(brass(P(name, octave), 0.4, rng, vel=0.95, attack=0.012, scoop=0.03, rel=0.2, law=0.55), 0.34, 0.45)
    m.add(glass(P("A", 5), 0.9, rng, tau=1.0), 0.34, 0.4)
    m.add(shimmer(0.9, rng, 3000, 10000, 20, 0.5, 0.12), 0.3, 1.0)
    return room(m.out(), 1.0, 0.25)


@sfx("boss_berserk", "boss_big")
def boss_berserk(rng, v):
    dur = 1.7
    m = Mix(dur)
    m.add(thump(120, 34, 0.7, 0.2, 0.8, rng), 0, 1.0)
    f = sweep(P("D", 2), P("D", 3), 1.2) * (1 + 0.02 * np.sin(2 * np.pi * 7 * T(1.2)))
    g = drive(harm(f, 1.2, saw_amps(24, 0.7)), 4.0)
    formant = moving_band(g, np.concatenate([sweep(700, 450, 0.6), sweep(450, 1100, 0.6)]), 0.3) * 3.0
    m.add(formant * env(1.2, 0.02, None, 0.3), 0.05, 0.6)
    m.add(moving_band(noise(1.2, rng), (900, 5200)) * env(1.2, 0.2, None, 0.3), 0.1, 0.3)
    s = sweep(900, 3600, 0.5)
    m.add(drive(np.sin(phase(s) + 2 * np.sin(phase(s * 0.5))), 3.0) * env(0.5, 0.05, None, 0.1), 1.0, 0.25)
    m.add(crack(0.15, rng, 1000, 9000, 0.03), 0, 0.8)
    return room(m.out(), 0.8, 0.2)


@sfx("boss_cannon_charge", "alert")
def boss_cannon_charge(rng, v):
    dur = 0.75
    t = T(dur)
    f = sweep(450, 3300, dur)
    x = np.sin(phase(f)) + 0.6 * np.sin(phase(f * 1.004))
    x *= 0.55 + 0.45 * np.sin(2 * np.pi * np.cumsum(8 + 18 * t / dur) / 44100)
    m = Mix(dur)
    m.add(x * env(dur, 0.03, None, 0.02) * (0.3 + 0.7 * t / dur), 0, 0.7)
    m.add(lp(harm(sweep(55, 110, dur), dur, saw_amps(8)), 400) * env(dur, 0.1, None, 0.02), 0, 0.3)
    return m.out()


@sfx("boss_cannon_fire", "boss_hit", 2)
def boss_cannon_fire(rng, v):
    m = Mix(0.45)
    m.add(thump(150, 38, 0.4, 0.1, 0.8, rng), 0, 1.0)
    m.add(crack(0.08, rng, 1800, 9000, 0.012), 0, 0.9)
    f = sweep(2000 - 400 * v, 220, 0.18)
    m.add(drive(harm(f, 0.18, saw_amps(10)), 2.0) * env(0.18, 0.002, 0.07, 0.03), 0, 0.55)
    return room(m.out(), 0.4, 0.15)


@sfx("boss_cannon_break", "big")
def boss_cannon_break(rng, v):
    m = Mix(0.7)
    m.add(thump(110, 34, 0.5, 0.14, 0.8, rng), 0, 1.0)
    for i in range(3):
        m.add(lp(noise(0.12, rng), 3500) * env(0.12, 0.002, 0.04, 0.03), i * 0.09, 0.7)
    for _ in range(9):
        m.add(modal(float(np.exp(rng.uniform(np.log(700), np.log(3800)))), 0.3, PLATE, rng, 0.3), rng.uniform(0, 0.4), rng.uniform(0.08, 0.25))
    for _ in range(14):
        m.add(tick(rng, rng.uniform(3000, 8000), 0.001), rng.uniform(0, 0.5), rng.uniform(0.2, 0.5))
    f = sweep(300, 70, 0.4)
    m.add(lp(drive(harm(f, 0.4, saw_amps(12)), 2.5), 1800) * env(0.4, 0.01, 0.15, 0.05), 0.05, 0.4)
    return room(m.out(), 0.5, 0.16)


@sfx("boss_dash_warning", "alert")
def boss_dash_warning(rng, v):
    m = Mix(0.5)
    for t, name, octave in [(0.0, "E", 6), (0.14, "A", 6)]:
        m.add(modal(P(name, octave), 0.2, [(1.0, 1.0, 0.08), (3.0, 0.12, 0.03)], rng) * smooth(T(0.2) / 0.0015), t, 0.9)
        m.add(tick(rng, 5500, 0.0008), t, 0.3)
    m.add(modal(P("A", 7), 0.2, [(1.0, 1.0, 0.05)], rng), 0.14, 0.25)
    m.add(lp(harm(sweep(55, 95, 0.4), 0.4, saw_amps(6)), 300) * env(0.4, 0.08, None, 0.05), 0, 0.5)
    return m.out()


@sfx("boss_laser_charge", "alert")
def boss_laser_charge(rng, v):
    dur = 1.0
    t = T(dur)
    m = Mix(dur)
    r = sweep(1.0, 2.0, dur)
    for k, g in enumerate([1.0, 0.7, 0.5, 0.35]):
        m.add(np.sin(phase(P("D", 5) * (k + 1) * r)) * g * 0.35, 0, 1.0)
    shimmer_am = 0.6 + 0.4 * np.sin(2 * np.pi * np.cumsum(6 + 24 * t / dur) / 44100)
    m.x *= shimmer_am * (0.25 + 0.75 * smooth(t / dur)) * smooth((dur - t) / 0.03)
    m.add(moving_band(noise(dur, rng), (900, 6500)) * env(dur, 0.4, None, 0.03) * 0.25, 0, 1.0)
    m.add(lp(harm(sweep(50, 100, dur), dur, saw_amps(6)), 350) * env(dur, 0.1, None, 0.03), 0, 0.15)
    return m.out()


@sfx("boss_laser_fire", "big")
def boss_laser_fire(rng, v):
    dur = 1.2
    m = Mix(dur)
    m.add(thump(120, 34, 0.5, 0.14, 0.8, rng), 0, 1.0)
    m.add(crack(0.1, rng, 1500, 9000, 0.02), 0, 0.8)
    body_dur = dur - 0.05
    body = np.zeros(n_(body_dur))
    for name, octave, g in [("D", 2, 1.0), ("D", 3, 0.8), ("A", 3, 0.5), ("D", 4, 0.35)]:
        body += g * harm(P(name, octave) * (1 + 0.003 * rng.standard_normal()), body_dur, saw_amps(14, 1.1))
    body *= 0.7 + 0.3 * np.sin(2 * np.pi * 60 * T(body_dur))
    m.add(drive(body, 2.5) * env(body_dur, 0.02, 0.9, 0.25), 0.05, 0.5)
    m.add(moving_band(noise(dur, rng), (6500, 1800)) * env(dur, 0.01, 0.55, 0.2), 0, 0.5)
    return room(m.out(), 0.5, 0.15)


@sfx("boss_explosion", "big")
def boss_explosion(rng, v):
    m = Mix(1.5)
    m.add(thump(110, 28, 1.2, 0.3, 0.8, rng), 0, 1.0)
    m.add(moving_band(noise(1.0, rng), (5000, 200)) * env(1.0, 0.003, 0.3, 0.15), 0, 0.8)
    m.add(crack(0.12, rng, 1000, 9000, 0.02), 0, 0.8)
    for _ in range(14):
        m.add(modal(float(np.exp(rng.uniform(np.log(700), np.log(4500)))), 0.4, PLATE, rng, 0.45), rng.uniform(0.04, 0.9), rng.uniform(0.08, 0.25))
    m.add(drive(harm(sweep(180, 45, 0.9), 0.9, saw_amps(8)), 2.0) * env(0.9, 0.01, 0.35, 0.1), 0, 0.4)
    return room(m.out(), 1.0, 0.25)


@sfx("boss_final_explosion", "boss_final")
def boss_final_explosion(rng, v):
    """the end of the boss: three blasts that grow, the big one, a long rumble, and out of it a bell chord in D MAJOR (the clear music is in the light)."""
    dur = 3.2
    m = Mix(dur)
    for t, g in [(0.0, 0.6), (0.28, 0.75), (0.56, 0.9)]:
        m.add(thump(130, 32, 0.6, 0.15, 0.8, rng), t, g)
        m.add(moving_band(noise(0.4, rng), (5000, 300)) * env(0.4, 0.002, 0.12, 0.06), t, 0.6 * g)
        m.add(crack(0.08, rng, 1200, 8000, 0.012), t, 0.6 * g)
    m.add(thump(100, 24, 2.0, 0.5, 1.0, rng), 0.95, 1.0)
    m.add(moving_band(noise(1.6, rng), (6000, 120)) * env(1.6, 0.003, 0.5, 0.3), 0.95, 0.9)
    m.add(crack(0.15, rng, 800, 9000, 0.03), 0.95, 0.8)
    for _ in range(24):
        m.add(modal(float(np.exp(rng.uniform(np.log(600), np.log(5000)))), 0.5, PLATE, rng, 0.5), rng.uniform(0.0, 1.8), rng.uniform(0.06, 0.2))
    for name, octave, delay in [("D", 5, 0.0), ("A", 5, 0.1), ("D", 6, 0.2)]:
        m.add(bell(P(name, octave), 1.8, rng, 1.2), 1.5 + delay, 0.28)
    f_sharp6 = 440.0 * 2.0 ** ((12 * 7 + 6 - 69) / 12.0)                                           # the F sharp that makes the chord D MAJOR (the only note outside the palette)
    m.add(glass(f_sharp6, 1.6, rng, tau=1.5), 1.75, 0.25)
    m.add(formant_pad(P("D", 4), 1.4, rng, "a", 4, 0.5, 0.6), 1.4, 0.4)
    m.add(formant_pad(P("A", 4), 1.2, rng, "a", 3, 0.55, 0.6), 1.5, 0.28)
    m.add(shimmer(1.4, rng, 3500, 10000, 26, 0.8, 0.1), 1.5, 1.0)
    return room(m.out(), 1.6, 0.3, damp=0.8)


@sfx("boss_vent", "ui")
def boss_vent(rng, v):
    m = Mix(0.85)
    m.add(moving_band(noise(0.8, rng), (9000, 2500)) * env(0.8, 0.03, 0.3, 0.2), 0, 0.8)
    m.add(thump(100, 55, 0.25, 0.07), 0, 0.5)
    m.add(modal(185.0, 0.5, [(1.0, 1.0, 0.25), (2.0, 0.4, 0.15), (3.0, 0.2, 0.1)], rng), 0.02, 0.35)
    return room(m.out(), 0.5, 0.15)


@sfx("boss_swap", "ui")
def boss_swap(rng, v):
    m = Mix(0.5)
    for t in (0.0, 0.07):
        m.add(modal(P("G", 4) * (1.0 if t == 0 else 0.8), 0.15, PLATE, rng, 0.25), t, 0.7)
        m.add(tick(rng, 3500, 0.002), t, 0.6)
    m.add(moving_band(noise(0.3, rng), (400, 4200)) * env(0.3, 0.05, 0.1, 0.1), 0.05, 0.5)
    m.add(glass(P("A", 5), 0.3, rng, tau=0.4), 0.2, 0.4)
    m.add(brass(P("D", 4), 0.1, rng, vel=0.8, attack=0.006, rel=0.04), 0.27, 0.5)
    return room(m.out(), 0.4, 0.12)


@sfx("boss_flip", "ui")
def boss_flip(rng, v):
    from .dsp import fit
    m = Mix(0.16)
    m.add(modal(P("D", 5), 0.15, PLATE, rng, 0.2), 0, 0.7)
    m.add(tick(rng, 4500, 0.0015), 0, 0.7)
    m.add(glass(P("D", 6), 0.15, rng, tau=0.2), 0.004, 0.35)
    return fit(m.out(), 0.16) * env(0.16, 0.0004, None, 0.03)


@sfx("white_warn", "alert")
def white_warn(rng, v):
    """three cold pings that climb (A D E): a volley of white needles. White has no colour: a pure sine with a hard edge."""
    m = Mix(0.62)
    for i, (name, octave) in enumerate([("A", 6), ("D", 7), ("E", 7)]):
        t = i * 0.12
        m.add(modal(P(name, octave), 0.18, [(1.0, 1.0, 0.07), (3.0, 0.08, 0.02)], rng) * smooth(T(0.18) / 0.0012), t, 0.9)
        m.add(tick(rng, 6500, 0.0007), t, 0.35)
    m.add(modal(P("A", 5), 0.5, [(1.0, 1.0, 0.18)], rng), 0.24, 0.2)
    return m.out()


@sfx("overload_alarm", "alert")
def overload_alarm(rng, v):
    """the klaxon of a charging overload: two strident tones a fifth apart (A, E) in four pulses that tighten, a sub that throbs with them."""
    dur = 1.6
    m = Mix(dur)
    for i, t in enumerate([0.0, 0.4, 0.76, 1.08, 1.36]):
        d = 0.3 - 0.03 * i
        tone = harm(P("A", 5), d, square_amps(10)) * 0.6 + harm(P("E", 6), d, square_amps(8)) * 0.4
        tone = tone * (0.8 + 0.2 * np.sin(2 * np.pi * 40 * T(d)))
        m.add(tone * env(d, 0.004, None, 0.04), t, 0.6)
        m.add(thump(90, 48, 0.3, 0.08), t, 0.6)
    m.add(moving_band(noise(1.4, rng), (2500, 6500)) * env(1.4, 0.8, None, 0.2), 0.1, 0.14)
    return m.out()


@sfx("overload_break", "boss_big")
def overload_break(rng, v):
    """you broke it: a crack, a cloud of glass, a stab on D, a bell and the motto's rising answer (D A D)."""
    dur = 1.3
    m = Mix(dur)
    m.add(thump(150, 40, 0.5, 0.12, 0.8, rng), 0, 1.0)
    m.add(crack(0.15, rng, 1500, 10000, 0.03), 0, 1.0)
    for _ in range(34):
        st = rng.uniform(0.0, 0.35)
        m.add(glass(float(np.exp(rng.uniform(np.log(2200), np.log(8000)))) * (1 - 0.3 * st), 0.35, rng, tau=0.25), st, rng.uniform(0.08, 0.22))
    for name, octave in [("D", 3), ("A", 3), ("D", 4)]:
        m.add(brass(P(name, octave), 0.3, rng, vel=1.0, attack=0.008, scoop=0.03, rel=0.15, law=0.6), 0.05, 0.5)
    m.add(bell(P("A", 5), 1.0, rng, 1.0), 0.1, 0.4)
    for i, (name, octave) in enumerate([("D", 5), ("A", 5), ("D", 6)]):
        m.add(glass(P(name, octave), 0.6, rng, tau=0.7), 0.42 + i * 0.09, 0.55)
    m.add(shimmer(0.8, rng, 3500, 10000, 14, 0.4, 0.1), 0.4, 1.0)
    return room(m.out(), 0.9, 0.25)
