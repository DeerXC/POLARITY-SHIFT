"""The interface, the cards and the jingles: ink and glass fused. Quiet, short, never in the way - except the jingles, which are the rare big moments and play the MOTTO of the
theme (A A D)."""
import numpy as np

from .common import P, run
from .dsp import (Mix, PLATE, WOOD, bell, drive, env, fit, formant_pad, glass, harm, moving_band, n_, noise, phase, pluck, room, shimmer, square_amps, sweep, thump, tick,
                  modal, lp)
from .registry import sfx


@sfx("cursor", "tick")
def cursor(rng, v):
    m = Mix(0.1)
    m.add(tick(rng, 2800, 0.0016), 0, 0.7)
    m.add(glass(P("A", 6), 0.1, rng, tau=0.14), 0, 0.55)
    return fit(m.out(), 0.1) * env(0.1, 0.0006, None, 0.02)


@sfx("menu_move", "tick")
def menu_move(rng, v):
    m = Mix(0.12)
    m.add(modal(P("G", 5), 0.12, WOOD, rng), 0, 0.6)
    m.add(glass(P("E", 6), 0.12, rng, tau=0.14), 0.004, 0.4)
    return fit(m.out(), 0.12) * env(0.12, 0.0006, None, 0.03)


@sfx("menu_adjust", "tick")
def menu_adjust(rng, v):
    m = Mix(0.08)
    f = sweep(P("A", 6) * 0.94, P("A", 6), 0.05, "lin")
    m.add(np.sin(phase(np.concatenate([f, np.full(n_(0.03), f[-1])]))) * env(0.08, 0.001, 0.03, 0.01), 0, 0.5)
    m.add(tick(rng, 4200, 0.001), 0, 0.4)
    return m.out()


@sfx("decide", "ui")
def decide(rng, v):
    m = Mix(0.36)
    m.add(tick(rng, 1800, 0.002), 0, 0.6)
    m.add(thump(220, 110, 0.07, 0.022), 0, 0.4)
    m.add(glass(P("D", 5), 0.34, rng, tau=0.55), 0.0, 0.7)
    m.add(glass(P("A", 5), 0.3, rng, tau=0.55), 0.075, 0.85)
    return room(m.out(), 0.5, 0.18)


@sfx("menu_apply", "ui")
def menu_apply(rng, v):
    m = Mix(0.55)
    for i, (name, octave) in enumerate([("D", 5), ("A", 5), ("D", 6)]):
        m.add(glass(P(name, octave), 0.4, rng, tau=0.6), i * 0.085, 0.7)
    m.add(shimmer(0.4, rng, 4000, 9000, 6, 0.25, 0.1), 0.2, 1.0)
    m.add(tick(rng, 2200), 0, 0.5)
    return room(m.out(), 0.8, 0.22)


@sfx("menu_error", "ui")
def menu_error(rng, v):
    m = Mix(0.34)
    for i, f in enumerate([P("D", 4), P("C", 4)]):
        m.add(modal(f, 0.2, WOOD, rng, 1.6), i * 0.11, 0.9)
        m.add(lp(drive(harm(f * 1.003, 0.1, square_amps(8)), 2.0), 1600) * env(0.1, 0.004, 0.05), i * 0.11, 0.4)
        m.add(thump(190, 120, 0.08, 0.03), i * 0.11, 0.3)
    return m.out()


@sfx("menu_open", "ui")
def menu_open(rng, v):
    m = Mix(0.36)
    air = moving_band(noise(0.3, rng), (700, 5500)) * env(0.3, 0.12, None, 0.1)
    m.add(air, 0, 0.35)
    m.add(glass(P("G", 5), 0.3, rng, tau=0.4), 0.02, 0.45)
    m.add(glass(P("D", 6), 0.3, rng, tau=0.5), 0.1, 0.6)
    return room(m.out(), 0.6, 0.2)


@sfx("ui_hint", "ui")
def ui_hint(rng, v):
    m = Mix(0.55)
    m.add(pluck(P("E", 6), 0.45, rng, tau=0.4), 0, 0.7)
    m.add(pluck(P("A", 6), 0.45, rng, tau=0.45), 0.13, 0.75)
    m.add(shimmer(0.4, rng, 5000, 10000, 5, 0.2, 0.08), 0.14, 1.0)
    return room(m.out(), 0.8, 0.25)


@sfx("unlock", "jingle")
def unlock(rng, v):
    """a thing was bought: the MOTTO (A A D), then a run up the scale into the light."""
    m = Mix(1.7)
    for t, name, octave, d in [(0.0, "A", 5, 0.2), (0.17, "A", 5, 0.2), (0.34, "D", 6, 0.9)]:
        m.add(glass(P(name, octave), d + 0.4, rng, tau=0.8), t, 0.7)
        m.add(pluck(P(name, octave - 1), 0.6, rng, tau=0.5), t, 0.5)
    run(m, rng, 0.62, 0.065, [("D", 6), ("E", 6), ("G", 6), ("A", 6), ("C", 7), ("D", 7)], "harp", 0.4, 0.8, 0.45)
    m.add(formant_pad(P("D", 4), 1.0, rng, "a", 3, 0.25, 0.5), 0.3, 0.35)
    m.add(shimmer(1.0, rng, 3500, 10000, 14, 0.5, 0.1), 0.55, 1.0)
    m.add(bell(P("D", 6), 1.2, rng, 1.0), 0.34, 0.25)
    return room(m.out(), 1.2, 0.3)


@sfx("synergy_found", "jingle")
def synergy_found(rng, v):
    """two modules join: the blue line climbs, the red line (its mirror) falls, and they meet on A."""
    m = Mix(1.3)
    up = [("D", 5), ("E", 5), ("G", 5), ("A", 5)]
    down = [("D", 6), ("C", 6), ("A", 5), ("A", 5)]
    for i in range(4):
        m.add(glass(P(*up[i]), 0.5, rng, tau=0.5), i * 0.09, 0.6)
        m.add(modal(P(*down[i]), 0.4, WOOD, rng, 1.8), i * 0.09, 0.55)
    m.add(glass(P("A", 5), 1.1, rng, tau=1.3), 0.4, 0.8)
    m.add(glass(P("A", 6), 1.0, rng, tau=1.2), 0.4, 0.5)
    m.add(bell(P("D", 5), 1.0, rng, 0.9), 0.4, 0.25)
    m.add(shimmer(0.9, rng, 4500, 10000, 12, 0.4, 0.1), 0.4, 1.0)
    return room(m.out(), 1.1, 0.3)
