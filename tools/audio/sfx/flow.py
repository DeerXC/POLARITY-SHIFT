"""The flow of a run: the stinger of a cleared wave and the card of a new one."""
from .common import P, run
from .dsp import Mix, brass, env, glass, moving_band, noise, room, shimmer, thump, tick
from .registry import sfx


@sfx("wave_clear", "jingle")
def wave_clear(rng, v):
    """the stinger of a cleared wave (it lands on a beat of the music, so its first note is its first sample): the MOTTO in brass (A A D) over a timpani, and the harp
    runs up the scale behind it."""
    dur = 1.7
    m = Mix(dur)
    m.add(thump(90, 48, 0.4, 0.12, 0.6, rng), 0, 0.8)
    for t, name, octave, d in [(0.0, "A", 4, 0.15), (0.17, "A", 4, 0.15), (0.34, "D", 5, 0.8)]:
        m.add(brass(P(name, octave), d, rng, vel=0.9, attack=0.012, scoop=0.025, rel=0.2, law=0.5), t, 0.7)
        m.add(brass(P(name, octave - 1), d, rng, vel=0.8, attack=0.016, scoop=0.025, rel=0.2, law=0.45), t, 0.45)
    run(m, rng, 0.5, 0.07, [("D", 6), ("E", 6), ("G", 6), ("A", 6), ("C", 7), ("D", 7)], "harp", 0.35, 0.8, 0.5)
    m.add(glass(P("D", 6), 1.1, rng, tau=1.3), 0.34, 0.35)
    m.add(shimmer(1.0, rng, 3500, 10000, 16, 0.5, 0.1), 0.5, 1.0)
    return room(m.out(), 1.0, 0.25)


@sfx("cutin_wave", "jingle")
def cutin_wave(rng, v):
    m = Mix(0.75)
    m.add(moving_band(noise(0.22, rng), (350, 6500)) * env(0.22, 0.04, 0.08, 0.06), 0, 0.5)
    m.add(thump(130, 60, 0.2, 0.06), 0, 0.5)
    m.add(glass(P("A", 5), 0.6, rng, tau=0.8), 0.07, 0.6)
    m.add(glass(P("D", 6), 0.6, rng, tau=0.9), 0.14, 0.6)
    m.add(tick(rng, 3000), 0.07, 0.4)
    return room(m.out(), 0.7, 0.2)
