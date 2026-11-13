"""Helpers shared by the recipe modules."""
import numpy as np

from .dsp import Mix, bell, glass, note, pluck, brass, thump, crack, crackle, sizzle, zap, modal, PLATE, WOOD, BAR

P = note


def run(m, rng, start, step, notes, voice, gain=0.6, dur=0.5, tau=0.5):
    """a run of notes (palette letters + octave), `step` seconds apart, in the voice 'harp', 'glass' or 'bell'."""
    for i, (name, octave) in enumerate(notes):
        f = P(name, octave)
        if voice == "harp":
            s = pluck(f, dur, rng, tau=tau)
        elif voice == "glass":
            s = glass(f, dur, rng, tau=tau)
        else:
            s = bell(f, dur, rng, tau=tau * 0.5)
        m.add(s, start + i * step, gain)


def impact(rng, dur=0.2, snap=None, body=None, bolt=None, air=None, debris=None, tone=None, m=None, at=0.0):
    """THE CORE OF EVERY IMPACT in the game: a hit, a shield, a kill and a shot are made of the same few layers, heard at different sizes and in different materials.
        snap    (lo Hz, hi Hz, tau s, gain)           a burst of noise: the crack that says 'something hard happened' in the first 5 ms
        body    (f0, f1, tau s, gain)                 a sine that falls in pitch: the weight of the thing that was hit
        bolt    (f0, f1, dur s, wave, gain)           a tone that falls fast: the energy of the weapon (zap)
        air     (lo, hi, tau s, gain)                 a hiss that falls away: the air that was torn
        debris  (clicks/s, lo, hi, decay s, gain)     chips that thin out: the thing coming apart
        tone    (callable(rng) -> array, gain)        a tuned layer (glass, plate) that ties the impact to the key of the music, kept low
    A game does not need a mallet in this place: the ear takes a struck bar for an instrument. It needs a snap, a body and debris."""
    m = m or Mix(dur)
    if snap:
        lo, hi, tau, g = snap
        m.add(crack(0.05, rng, lo, hi, tau), at, g)
    if body:
        f0, f1, tau, g = body
        m.add(thump(f0, f1, tau * 3.2, tau), at, g)
    if bolt:
        f0, f1, d, wave, g = bolt
        m.add(zap(f0, f1, d, wave), at, g)
    if air:
        lo, hi, tau, g = air
        m.add(sizzle(0.12, rng, lo, hi, tau), at + 0.002, g)
    if debris:
        dens, lo, hi, decay, g = debris
        m.add(crackle(max(0.1, dur - 0.01), rng, dens, lo, hi, decay), at + 0.004, g)
    if tone:
        fn, g = tone
        m.add(fn(rng), at + 0.002, g)
    return m


__all__ = ["P", "run", "impact", "Mix", "np", "bell", "glass", "pluck", "brass", "thump", "crack", "crackle", "sizzle", "zap", "modal", "PLATE", "WOOD", "BAR"]
