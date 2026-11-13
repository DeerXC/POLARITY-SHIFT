"""The small instruments the sound effects are made of. Everything is a physical or modal model, a sum of a few partials with their own decay, a sweep, or shaped noise:
nothing here is a sample. All functions return mono float64 arrays at SR; the recipes place them in time with `Mix`.

TWO FAMILIES OF SOUND, because a game has two kinds of sound:
  TONES    (glass, bell, pluck, brass, choir, modal bars)   they are MUSIC: every one is tuned to D E G A C, the notes that all the keys of the score share, so an effect
                                                              never plays a wrong note against it. They are for the interface, the jingles, the warnings
  IMPACTS  (zap, noise_burst, thump, crackle, moving_band)  they are COMBAT: a snap of noise, a body that falls in pitch, debris. They carry no pitch of their own that the ear
                                                              could take for an instrument; a tuned zap only ends on a note of the palette so that it sits in the key
"""
import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

SR = 44100
TWO_PI = 2.0 * np.pi

NOTES = {"D": 2, "E": 4, "G": 7, "A": 9, "C": 0}      # semitones above C


def note(name, octave):
    """'A', 5 -> 880 Hz. Only the palette letters (D E G A C)."""
    return 440.0 * 2.0 ** ((12 * (octave + 1) + NOTES[name] - 69) / 12.0)


def n_(dur):
    return max(1, int(round(dur * SR)))


def T(dur):
    return np.arange(n_(dur)) / SR


# ----------------------------------------------------------------------------------------------------------------------------------------------------------
# envelopes
# ----------------------------------------------------------------------------------------------------------------------------------------------------------
def smooth(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def env(dur, a=0.002, tau=0.1, rel=0.004):
    """a smooth attack of `a` s, an exponential decay with time constant `tau` (None = flat), a fade of `rel` s at the very end (never a click)."""
    t = T(dur)
    e = smooth(t / max(a, 1e-5))
    if tau:
        e = e * np.exp(-t / tau)
    r = min(rel, dur * 0.5)
    if r > 0:
        e = e * smooth((dur - t) / r)
    return e


def endfade(t, dur):
    """a sound that is cut at `dur` while it still rings would click: its last 25 ms (or 40 % of it) fade out."""
    return smooth((dur - t) / min(0.025, 0.4 * dur))


# ----------------------------------------------------------------------------------------------------------------------------------------------------------
# filters and noise
# ----------------------------------------------------------------------------------------------------------------------------------------------------------
def noise(dur, rng):
    return rng.standard_normal(n_(dur))


def bp(x, lo, hi, order=2):
    hi = min(hi, SR * 0.46)
    lo = max(lo, 15.0)
    if lo >= hi:
        return x * 0.0
    return sosfilt(butter(order, [lo / (SR / 2), hi / (SR / 2)], "bandpass", output="sos"), x)


def lp(x, fc, order=2):
    return sosfilt(butter(order, min(fc, SR * 0.46) / (SR / 2), "lowpass", output="sos"), x)


def hp(x, fc, order=2):
    return sosfilt(butter(order, max(fc, 15.0) / (SR / 2), "highpass", output="sos"), x)


def moving_band(x, fc, width=0.45, bands=28, lo=80.0, hi=14000.0):
    """a band-pass whose centre moves: fc is an array (Hz per sample) or a (f_start, f_end) pair (exponential). The input is split into `bands` log-spaced bands, and the
    bands are mixed with a bump that follows fc: the sweep of a whoosh, a hiss that opens and closes."""
    n = len(x)
    if not hasattr(fc, "__len__") or len(fc) == 2 and n != 2:
        f0, f1 = fc
        fc = f0 * (f1 / f0) ** (np.arange(n) / max(n - 1, 1))
    edges = np.geomspace(lo, hi, bands + 1)
    pos = np.log(np.clip(fc, lo, hi) / lo) / np.log(hi / lo) * bands - 0.5
    out = np.zeros(n)
    for i in range(bands):
        w = np.exp(-0.5 * ((pos - i) / (width * 2.2)) ** 2)
        if w.max() < 0.02:
            continue
        out += w * bp(x, edges[i], edges[i + 1], 2)
    return out


# ----------------------------------------------------------------------------------------------------------------------------------------------------------
# tones
# ----------------------------------------------------------------------------------------------------------------------------------------------------------
def phase(f):
    """f: a frequency (Hz) or an array of them; returns the phase in radians (the sweep stays continuous)."""
    return TWO_PI * np.cumsum(f) / SR


def sweep(f0, f1, dur, curve="exp"):
    t = np.arange(n_(dur)) / SR
    x = t / dur
    return f0 * (f1 / f0) ** x if curve == "exp" else f0 + (f1 - f0) * x


def harm(f, dur, amps, rng=None):
    """a stack of harmonics, band-limited: amps[h-1] is the level of the h-th. f is a number or a per-sample array (a sweep)."""
    f = np.full(n_(dur), float(f)) if np.isscalar(f) else np.asarray(f)
    ph = phase(f)
    fmax = float(np.max(f))
    out = np.zeros(len(ph))
    for h, a in enumerate(amps, 1):
        if a == 0 or fmax * h > SR * 0.45:
            continue
        out += a * np.sin(h * ph + (rng.uniform(0, TWO_PI) if rng is not None else 0.0))
    return out


def saw_amps(n=24, slope=1.0):
    return [h ** -slope for h in range(1, n + 1)]


def square_amps(n=24):
    return [1.0 / h if h % 2 else 0.0 for h in range(1, n + 1)]


def modal(f, dur, partials, rng=None, tau_scale=1.0, tilt=0.0):
    """a struck object: partials = [(ratio, amp, tau_s), ...] (bars, bells, plates: the ratios are not whole numbers). tilt > 0 lets the high partials die faster."""
    t = T(dur)
    out = np.zeros(len(t))
    for r, a, tau in partials:
        fr = f * r
        if fr > SR * 0.45:
            continue
        out += a * np.exp(-t / (tau * tau_scale / (1.0 + tilt * (r - 1.0)))) * np.sin(TWO_PI * fr * t + (rng.uniform(0, TWO_PI) if rng is not None else 0.0))
    return out * smooth(t / 0.0003) * endfade(t, dur)


BAR = [(1.0, 1.0, 0.55), (2.756, 0.5, 0.26), (5.404, 0.28, 0.13), (8.933, 0.14, 0.07)]               # a glass / metal bar (glockenspiel)
BELL = [(0.5, 0.5, 1.2), (1.0, 1.0, 0.9), (1.19, 0.45, 0.7), (1.56, 0.4, 0.5), (2.0, 0.5, 0.45), (2.51, 0.3, 0.3), (3.15, 0.25, 0.25), (4.1, 0.18, 0.15)]   # a tubular / church bell
WOOD = [(1.0, 1.0, 0.07), (3.93, 0.35, 0.025), (9.0, 0.10, 0.01)]                                      # a marimba bar
PLATE = [(1.0, 1.0, 0.22), (1.593, 0.7, 0.18), (2.136, 0.6, 0.15), (2.296, 0.5, 0.13), (2.653, 0.45, 0.10), (2.918, 0.4, 0.09), (3.5, 0.3, 0.07), (4.2, 0.25, 0.05)]   # a sheet of metal


def glass(f, dur, rng=None, tau=1.0, vel=1.0):
    """a glass bar: a clean sine on top, a few inharmonic partials that die quickly. The blue voice."""
    return modal(f, dur, [(r, a * (0.6 + 0.4 * vel) ** (r ** 0.5 - 0.9), t * tau) for r, a, t in BAR], rng)


def bell(f, dur, rng=None, tau=1.0):
    return modal(f, dur, [(r, a, t * tau) for r, a, t in BELL], rng)


def pluck(f, dur, rng, tau=0.5, bright=1.0, pos=0.2):
    """a harp string: harmonics that die faster the higher they are; the pluck position cancels some of them."""
    t = T(dur)
    out = np.zeros(len(t))
    for h in range(1, 18):
        fh = f * h * np.sqrt(1.0 + 0.00005 * h * h)
        if fh > SR * 0.45:
            break
        a = h ** (-1.1 / bright) * abs(np.sin(np.pi * h * pos))
        out += a * np.exp(-t / (tau / (1.0 + 0.55 * (h - 1) ** 1.1))) * np.sin(TWO_PI * fh * t + rng.uniform(0, 0.4))
    return out * smooth(t / 0.0015) * endfade(t, dur)


def brass(f, dur, rng, vel=0.8, attack=0.02, scoop=0.012, rel=0.06, law=0.42, slope=0.9, vib=0.0):
    """the blat of a horn / a trumpet / a trombone: the louder, the more harmonics, so every note opens up at its start and closes. The red voice."""
    n = n_(dur + rel * 2)
    t = np.arange(n) / SR
    inst = f * (1.0 - scoop * np.exp(-t / 0.045)) * (1.0 + vib * np.sin(TWO_PI * 5.1 * t) * smooth((t - 0.25) / 0.4))
    ph = phase(inst)
    e = smooth(t / attack) * np.where(t < dur, 1.0, np.exp(-(t - dur) * (3.0 / rel))) * (0.8 + 0.2 * smooth(t / 0.2))
    lev = 0.35 + 0.65 * vel
    out = np.zeros(n)
    for h in range(1, 17):
        if f * h > 12000:
            break
        out += h ** -slope * (e * lev) ** (1.0 + law * (h - 1)) * np.sin(h * ph + 0.3 * h)
    return out * 0.55


def shimmer(dur, rng, f0=3000.0, f1=9000.0, count=10, tau=0.3, seed_amp=0.12):
    """a cloud of tiny high pings that appear and die: sparkle. (Random pitches: keep it quiet, and never put it in a cue that has to be pleasant to hear again and again.)"""
    t = T(dur)
    out = np.zeros(len(t))
    for _ in range(count):
        f = np.exp(rng.uniform(np.log(f0), np.log(f1)))
        st = rng.uniform(0, dur * 0.6)
        i = int(st * SR)
        tt = t[: len(t) - i]
        out[i:] += seed_amp * rng.uniform(0.5, 1.0) * np.exp(-tt / (tau * rng.uniform(0.4, 1.0))) * np.sin(TWO_PI * f * tt) * smooth(tt / 0.002)
    return out


def formant_pad(f, dur, rng, vowel="a", voices=3, attack=0.25, rel=0.4):
    """a choir: harmonics shaped by the formants of a vowel, a few voices a little out of tune."""
    V = {"a": ((800, 1150, 2900), (80, 90, 120), (1.0, 0.5, 0.25)), "o": ((450, 800, 2830), (70, 80, 100), (1.0, 0.45, 0.12))}[vowel]
    n = n_(dur + rel)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for _ in range(voices):
        fv = f * 2.0 ** (rng.normal(0, 7.0) / 1200.0)
        ph = TWO_PI * np.cumsum(fv * (1.0 + 0.004 * np.sin(TWO_PI * rng.uniform(4.8, 5.6) * t + rng.uniform(0, 6.28)))) / SR
        for h in range(1, 31):
            fh = fv * h
            if fh > 7000:
                break
            g = sum(gain * np.exp(-0.5 * ((fh - fc) / bw) ** 2) for fc, bw, gain in zip(*V)) + 0.02 / h
            out += g * h ** -0.35 / voices * np.sin(h * ph + rng.uniform(0, 6.28))
    e = smooth(t / attack) * np.where(t < dur, 1.0, np.exp(-(t - dur) * (3.0 / rel)))
    return out * e * 0.45


def drive(x, amount=3.0):
    return np.tanh(x * amount) / np.tanh(amount)


# ----------------------------------------------------------------------------------------------------------------------------------------------------------
# impacts: what makes a sound a COMBAT sound. A snap of noise, a body that falls in pitch, debris.
# ----------------------------------------------------------------------------------------------------------------------------------------------------------
def thump(f0, f1, dur, tau=0.08, click=0.0, rng=None):
    """a drum / a boom: a sine that falls from f0 to f1 (exponentially) and dies."""
    t = T(dur)
    f = f1 + (f0 - f1) * np.exp(-t / (tau * 0.35))
    out = np.sin(phase(f)) * np.exp(-t / tau) * endfade(t, dur)
    if click > 0 and rng is not None:
        c = min(len(t), n_(0.004))
        out[:c] += click * hp(rng.standard_normal(c), 1500) * np.linspace(1, 0, c)
    return out


def crack(dur, rng, lo=1800, hi=9000, tau=0.006):
    """the sharp start of a shot, a hit, a break: a burst of band-limited noise that dies in a few milliseconds."""
    t = T(dur)
    return bp(rng.standard_normal(len(t)), lo, hi) * np.exp(-t / tau)


def tick(rng, f=3500, tau=0.0015, dur=0.02):
    t = T(dur)
    return bp(rng.standard_normal(len(t)), f * 0.6, f * 1.6) * np.exp(-t / tau)


def zap(f0, f1, dur, wave="sine", tau=None, bright=1.0):
    """a bolt: a tone whose pitch FALLS fast from f0 to f1 (or rises) and dies - the 'pew' of every energy weapon. Most of the fall happens in the first third.
    wave: 'sine', 'tri', 'saw' (bright: a bite), 'square' (hollow). It carries no pitch the ear could take for a note: it is a motion."""
    t = T(dur)
    x = t / dur
    f = f1 + (f0 - f1) * np.exp(-x * 4.5)
    ph = phase(f)
    if wave == "sine":
        y = np.sin(ph) + 0.18 * bright * np.sin(2 * ph)
    elif wave == "tri":
        y = np.sin(ph) - np.sin(3 * ph) / 9.0 + np.sin(5 * ph) / 25.0
    elif wave == "square":
        y = sum(np.sin(h * ph) / h for h in (1, 3, 5, 7, 9) if f0 * h < SR * 0.45)
    else:  # saw
        y = sum(np.sin(h * ph) / h for h in range(1, 13) if max(f0, f1) * h < SR * 0.45)
    return y * np.exp(-t / (tau if tau else dur * 0.45)) * smooth(t / 0.0006) * endfade(t, dur)


def crackle(dur, rng, density=900.0, lo=1500.0, hi=8000.0, decay=0.12):
    """debris: sparse clicks that thin out - the sound of a thing coming apart, or of a fire. `density` clicks per second at the start, falling with `decay` s."""
    n = n_(dur)
    t = np.arange(n) / SR
    p = density / SR * np.exp(-t / decay)
    clicks = (rng.random(n) < p).astype(float) * rng.uniform(0.4, 1.0, n)
    ring = bp(clicks * 8.0, lo, hi)
    return ring


def sizzle(dur, rng, lo=3000.0, hi=10000.0, tau=0.06):
    """air in the wake of a shot: a short hiss that falls away."""
    t = T(dur)
    return bp(rng.standard_normal(len(t)), lo, hi) * np.exp(-t / tau) * smooth(t / 0.002)


# ----------------------------------------------------------------------------------------------------------------------------------------------------------
# rooms: a short IR baked into the file (the world bus has its own small reverb: only the effects that have a tail of their own bring one)
# ----------------------------------------------------------------------------------------------------------------------------------------------------------
_IRS = {}


def room(x, seconds=0.8, wet=0.25, damp=0.6, seed=3, pre=0.008):
    key = (seconds, damp, seed, pre)
    if key not in _IRS:
        r = np.random.default_rng(seed)
        n = n_(seconds)
        t = np.arange(n) / SR
        ir = r.standard_normal(n) * np.exp(-t * (6.9 / seconds))
        ir = lp(ir, 3000 + 9000 * (1 - damp), 2)
        p = n_(pre)
        ir[:p] = 0
        ir[p:p + n_(0.01)] *= np.linspace(0, 1, n_(0.01))
        for _ in range(6):                                  # the first reflections
            d = int(r.uniform(0.006, 0.05) * SR)
            ir[d] += r.uniform(0.3, 0.7) * (1 if r.random() > 0.4 else -1) * 2.5
        _IRS[key] = ir / np.sqrt(np.sum(ir ** 2))
    wet_sig = fftconvolve(x, _IRS[key])
    out = np.zeros(len(wet_sig))
    out[: len(x)] += x
    return out + wet * wet_sig


# ----------------------------------------------------------------------------------------------------------------------------------------------------------
# placing sounds in time
# ----------------------------------------------------------------------------------------------------------------------------------------------------------
class Mix:
    """a canvas of `dur` seconds: add(sound, at_seconds, gain). The canvas grows when a sound is longer than what is left of it (a sound is never cut: a cut would click)."""

    def __init__(self, dur):
        self.x = np.zeros(n_(dur))

    def add(self, s, at=0.0, gain=1.0):
        i = int(round(at * SR))
        if i < 0:
            s = s[-i:]
            i = 0
        if len(s) == 0:
            return self
        if i + len(s) > len(self.x):
            self.x = np.pad(self.x, (0, i + len(s) - len(self.x)))
        self.x[i:i + len(s)] += s * gain
        return self

    def out(self):
        return self.x


def fit(x, dur):
    n = n_(dur)
    return np.pad(x, (0, max(0, n - len(x))))[:n]
