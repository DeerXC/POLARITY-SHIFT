"""The instruments. Every one is a small physical / modal model, not a sample and not a plain oscillator:

  plucked strings (harp, pizzicato, bass)   partials that die the faster the higher they are, a pluck position that cancels some of them, a stiff-string stretch
  struck bars (celesta, glockenspiel, vibraphone, marimba)   the real inharmonic ratios of a bar, each with its own decay
  piano                                     three slightly detuned strings per note, two-stage decay, hammer thump, brightness by touch
  flute / ocarina                           a nearly pure tone with a late vibrato, a puff of breath at the start, breath noise
  strings / cello (bowed)                   a small ensemble (every player a little out of tune, own vibrato), bow noise, a slow bow
  horn / trumpet                            the brass law: the louder it is, the more harmonics it has (so every note has a blat at its start that closes)
  choir                                     a vowel: harmonics shaped by formants, voices detuned
  timpani / taiko / bass drum / snare / shaker / tambourine / cymbal / wood      modes and noise bursts

Everything returns a mono float32 array (the note and its natural tail). Nothing here knows about music: bgm6/score.py does.
"""
import numpy as np
from scipy.signal import butter, sosfilt

SR = 44100
TWO_PI = 2.0 * np.pi


def hz(m):
    return 440.0 * 2.0 ** ((np.asarray(m, dtype=np.float64) - 69.0) / 12.0)


def _t(n):
    return np.arange(n, dtype=np.float64) / SR


def _smooth(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def _bp(x, lo, hi, order=2):
    hi = min(hi, SR * 0.45)
    lo = max(lo, 20.0)
    if lo >= hi:
        return x * 0.0
    sos = butter(order, [lo / (SR / 2), hi / (SR / 2)], "bandpass", output="sos")
    return sosfilt(sos, x)


def _lp(x, fc, order=2):
    fc = min(fc, SR * 0.45)
    sos = butter(order, fc / (SR / 2), "lowpass", output="sos")
    return sosfilt(sos, x)


def _hp(x, fc, order=2):
    sos = butter(order, max(fc, 20.0) / (SR / 2), "highpass", output="sos")
    return sosfilt(sos, x)


def _noise(rng, n):
    return rng.standard_normal(n)


def _release(t, dur, rate):
    """after the note is over (dur) the sound is damped: exp(-rate * time). rate = 0: not at all."""
    return np.where(t > dur, np.exp(-(t - dur) * rate), 1.0)


def _attack(t, a):
    return _smooth(t / max(a, 1e-4))


def _vibrato_phase(f, n, rng, rate=5.2, depth=0.0035, delay=0.25, ramp=0.4):
    t = _t(n)
    lfo = depth * np.sin(TWO_PI * rate * t + rng.uniform(0, TWO_PI)) * _smooth((t - delay) / ramp)
    return TWO_PI * np.cumsum(f * (1.0 + lfo)) / SR


# ---------------------------------------------------------------------------------------------------------------------------------------------
# plucked strings
# ---------------------------------------------------------------------------------------------------------------------------------------------
def _pluck(f, dur, vel, rng, k0, k1, tail, harmonics, pos, brightness, rel):
    n = int((dur + tail) * SR)
    t = _t(n)
    out = np.zeros(n)
    x = rng.uniform(*pos)
    for h in range(1, harmonics + 1):
        fh = f * h * np.sqrt(1.0 + 0.00006 * h * h)
        if fh > 16000:
            break
        a = h ** -brightness * abs(np.sin(np.pi * h * x)) * (0.55 + 0.45 * vel) ** (0.25 * (h - 1) ** 0.5)
        k = k0 + k1 * (h - 1) ** 1.15
        out += a * np.exp(-k * t) * np.sin(TWO_PI * fh * t + rng.uniform(0, 0.3))
    out *= _attack(t, 0.0012) * _release(t, dur, rel)
    # the finger / the plectrum: a very short, dark click
    c = int(0.006 * SR)
    out[:c] += _lp(_noise(rng, c), 2800) * 0.05 * vel * np.linspace(1, 0, c)
    return out


def harp(f, dur, vel, rng):
    k0 = 1.25 + f / 1100.0
    k1 = 1.5 + f / 700.0
    return _pluck(f, dur, vel, rng, k0, k1, 1.6, 16, (0.17, 0.27), 0.85, 2.2) * 0.95


def pizz(f, dur, vel, rng):
    """pizzicato strings: woody, shorter, a little brighter at the attack"""
    y = _pluck(f, min(dur, 0.35), vel, rng, 5.5 + f / 400.0, 7.0 + f / 300.0, 0.5, 12, (0.12, 0.2), 0.9, 8.0)
    return _lp(y, 3400 + 2600 * vel) * 1.25


def bass_pluck(f, dur, vel, rng):
    """an upright bass: fat low partials, a woody growl at the start"""
    n = int((dur + 0.5) * SR)
    t = _t(n)
    out = np.zeros(n)
    for h in range(1, 9):
        a = {1: 1.0, 2: 0.7, 3: 0.35, 4: 0.28, 5: 0.12, 6: 0.09, 7: 0.05, 8: 0.03}[h] * (0.5 + 0.5 * vel) ** (0.4 * (h - 1) ** 0.6)
        k = 2.3 + 2.4 * (h - 1) ** 1.1 + f / 400.0
        out += a * np.exp(-k * t) * np.sin(TWO_PI * f * h * t)
    out *= _attack(t, 0.004) * _release(t, dur, 7.0)
    c = int(0.012 * SR)
    out[:c] += _bp(_noise(rng, c), 120, 900) * 0.10 * vel * np.linspace(1, 0, c)
    return out * 1.6


# ---------------------------------------------------------------------------------------------------------------------------------------------
# struck bars
# ---------------------------------------------------------------------------------------------------------------------------------------------
def _bar(f, dur, vel, rng, ratios, amps, decays, tail, rel, click=0.0, tremolo=0.0):
    n = int((dur + tail) * SR)
    t = _t(n)
    out = np.zeros(n)
    pitch_scale = (261.6 / max(f, 60.0)) ** 0.45   # higher bars ring shorter
    for r, a, d in zip(ratios, amps, decays):
        if f * r > 16000:
            continue
        out += a * (0.6 + 0.4 * vel) * np.exp(-t / (d * pitch_scale)) * np.sin(TWO_PI * f * r * t + rng.uniform(0, TWO_PI))
    if tremolo > 0:
        out *= 1.0 - tremolo + tremolo * np.sin(TWO_PI * 5.3 * t + rng.uniform(0, TWO_PI))
    out *= _attack(t, 0.0008) * _release(t, dur, rel)
    if click > 0:
        c = int(0.004 * SR)
        out[:c] += _bp(_noise(rng, c), 1500, 7000) * click * vel * np.linspace(1, 0, c)
    return out


def celesta(f, dur, vel, rng):
    return _bar(f, dur, vel, rng, (1.0, 4.0, 9.8), (1.0, 0.30, 0.07), (1.4, 0.45, 0.16), 1.6, 3.0, 0.04) * 0.8


def glock(f, dur, vel, rng):
    return _bar(f, dur, vel, rng, (1.0, 2.756, 5.404, 8.933), (1.0, 0.50, 0.28, 0.14), (1.9, 0.9, 0.45, 0.25), 2.0, 2.5, 0.06) * 0.6


def vibes(f, dur, vel, rng):
    return _bar(f, dur, vel, rng, (1.0, 4.0, 10.1), (1.0, 0.22, 0.05), (2.8, 0.7, 0.25), 2.6, 1.6, 0.02, tremolo=0.22) * 0.85


def marimba(f, dur, vel, rng):
    return _bar(f, dur, vel, rng, (1.0, 3.93, 9.0), (1.0, 0.38, 0.10), (0.55, 0.14, 0.05), 0.8, 9.0, 0.10) * 1.1


# ---------------------------------------------------------------------------------------------------------------------------------------------
# piano
# ---------------------------------------------------------------------------------------------------------------------------------------------
def piano(f, dur, vel, rng):
    ring = min(2.6, 0.9 + 1.4 * (261.0 / max(f, 80.0)) ** 0.6)
    n = int((dur + ring) * SR)
    t = _t(n)
    out = np.zeros(n)
    B = 0.00035 * (f / 261.6) ** 2
    cut = 1400.0 + 5500.0 * vel
    detunes = (0.0, rng.uniform(0.25, 0.7), -rng.uniform(0.25, 0.7))      # three strings: cents
    for h in range(1, 23):
        fh = f * h * np.sqrt(1.0 + B * h * h)
        if fh > 15000:
            break
        a = h ** -0.95 * np.exp(-(fh / cut) ** 2) * (1.0 if h != 1 else 1.1)
        k1 = 4.0 + 0.0021 * fh                                           # the fast part
        k2 = 0.55 + 0.00055 * fh + 0.04 * h                              # the slow part
        for i, c in enumerate(detunes):
            ff = fh * 2.0 ** (c / 1200.0)
            env = 0.62 * np.exp(-k1 * t) + 0.38 * np.exp(-k2 * t * (1.0 if i == 0 else 1.25))
            out += a / 3.0 * env * np.sin(TWO_PI * ff * t + rng.uniform(0, TWO_PI))
    out *= _attack(t, 0.0015) * _release(t, dur, 5.0 if dur < 1.2 else 3.0)
    c = int(0.02 * SR)
    out[:c] += _lp(_noise(rng, c), 700) * 0.12 * vel * np.linspace(1, 0, c) ** 2   # the thump of the hammer
    return out * 1.15


# ---------------------------------------------------------------------------------------------------------------------------------------------
# breath
# ---------------------------------------------------------------------------------------------------------------------------------------------
def flute(f, dur, vel, rng):
    n = int((dur + 0.25) * SR)
    t = _t(n)
    ph = _vibrato_phase(f, n, rng, 5.1, 0.0032, 0.28 if dur > 0.45 else 0.9, 0.35)
    env = _attack(t, 0.055 + 0.03 * (1 - vel)) * np.where(t < dur, 1.0, np.exp(-(t - dur) * 18.0))
    swell = 0.86 + 0.14 * _smooth((t - 0.04) / 0.25)
    out = np.sin(ph) + 0.26 * np.sin(2 * ph + 0.4) + 0.10 * np.sin(3 * ph + 1.1) + 0.04 * np.sin(4 * ph)
    breath = _bp(_noise(rng, n), f * 0.9, f * 3.5) * (0.055 + 0.05 * (1 - vel))
    chiff = _bp(_noise(rng, n), f * 1.5, f * 5.0) * 0.07 * np.exp(-t / 0.045)
    return (out * (0.55 + 0.45 * vel) + breath + chiff) * env * swell * 0.55


def ocarina(f, dur, vel, rng):
    n = int((dur + 0.3) * SR)
    t = _t(n)
    ph = _vibrato_phase(f, n, rng, 4.6, 0.0026, 0.35, 0.5)
    env = _attack(t, 0.035) * np.where(t < dur, 1.0, np.exp(-(t - dur) * 14.0))
    out = np.sin(ph) + 0.07 * np.sin(2 * ph) + 0.03 * np.sin(3 * ph)
    breath = _bp(_noise(rng, n), f * 0.8, f * 2.5) * 0.025
    return (out + breath) * env * (0.5 + 0.5 * vel) * 0.7


# ---------------------------------------------------------------------------------------------------------------------------------------------
# bowed strings
# ---------------------------------------------------------------------------------------------------------------------------------------------
def _saw_phase(ph):
    return 2.0 * ((ph / TWO_PI) % 1.0) - 1.0


def strings(f, dur, vel, rng, voices=4, attack=0.14, release=0.30, bright=1.0, swell=0.0, stac=False):
    n = int((dur + release * 2.2) * SR)
    t = _t(n)
    out = np.zeros(n)
    cut = (1100.0 + 3600.0 * vel) * bright
    for v in range(voices):
        cents = rng.normal(0.0, 5.0)
        fv = f * 2.0 ** (cents / 1200.0)
        ph = _vibrato_phase(fv, n, rng, rng.uniform(4.7, 5.8), rng.uniform(0.0025, 0.0045), rng.uniform(0.12, 0.3), 0.3)
        voice = _saw_phase(ph)
        if stac:
            voice = voice * 0.7 + 0.3 * _saw_phase(2.0 * ph) * np.exp(-t / 0.05)      # the bite of the bow at the start
        out += voice / voices
    out = _lp(out, cut, 2)
    if stac:
        out = out * (1.0 + 0.9 * np.exp(-t / 0.03))
    env = _attack(t, attack) * np.where(t < dur, 1.0, np.exp(-(t - dur) * (3.0 / release)))
    if swell > 0:
        env = env * (1.0 - swell + swell * _smooth(t / max(dur, 0.2)))
    bow = _bp(_noise(rng, n), 2200, 6500) * 0.012 * (1.0 if not stac else 2.2)
    return (out + bow * env) * env * 0.5


def strings_stac(f, dur, vel, rng):
    return strings(f, min(dur, 0.22), vel, rng, voices=3, attack=0.010, release=0.07, bright=1.25, stac=True) * 1.15


def strings_soft(f, dur, vel, rng):
    return strings(f, dur, vel, rng, voices=4, attack=0.22, release=0.45, bright=0.8, swell=0.45)


def cello(f, dur, vel, rng):
    return strings(f, dur, vel, rng, voices=2, attack=0.09, release=0.25, bright=0.95, swell=0.2) * 1.2


# ---------------------------------------------------------------------------------------------------------------------------------------------
# brass
# ---------------------------------------------------------------------------------------------------------------------------------------------
def _brass(f, dur, vel, rng, attack, release, slope, law, scoop, harmonics=16):
    n = int((dur + release * 2.0) * SR)
    t = _t(n)
    ph = _vibrato_phase(f * (1.0 - scoop * np.exp(-_t(n) / 0.05)).mean(), n, rng, 5.0, 0.0018, 0.35, 0.5)
    if scoop > 0:   # a lip slur into the note: the pitch starts below and closes
        inst = f * (1.0 - scoop * np.exp(-t / 0.045))
        ph = TWO_PI * np.cumsum(inst * (1.0 + 0.0018 * np.sin(TWO_PI * 5.0 * t) * _smooth((t - 0.35) / 0.5))) / SR
    env = _attack(t, attack) * np.where(t < dur, 1.0, np.exp(-(t - dur) * (3.0 / release)))
    env = env * (0.8 + 0.2 * _smooth(t / 0.25))
    level = (0.35 + 0.65 * vel)
    out = np.zeros(n)
    for h in range(1, harmonics + 1):
        if f * h > 14000:
            break
        out += h ** -slope * (env * level) ** (1.0 + law * (h - 1)) * np.sin(h * ph + 0.3 * h)
    breath = _bp(_noise(rng, n), 1500, 6000) * 0.012 * env
    return (out + breath) * 0.55


def horn(f, dur, vel, rng):
    return _brass(f, dur, vel, rng, 0.07, 0.22, 0.95, 0.42, 0.012)


def trumpet(f, dur, vel, rng):
    return _brass(f, dur, vel, rng, 0.03, 0.12, 0.55, 0.30, 0.015) * 0.9


def brass_stab(f, dur, vel, rng):
    """a short, hard blat (the red face)"""
    return _brass(f, min(dur, 0.30), vel, rng, 0.012, 0.09, 0.7, 0.50, 0.02) * 1.05


def trombone(f, dur, vel, rng):
    return _brass(f, dur, vel, rng, 0.06, 0.2, 1.1, 0.38, 0.01) * 1.1


# ---------------------------------------------------------------------------------------------------------------------------------------------
# choir
# ---------------------------------------------------------------------------------------------------------------------------------------------
VOWELS = {"a": ((800, 1150, 2900), (80, 90, 120), (1.0, 0.5, 0.25)),
          "o": ((450, 800, 2830), (70, 80, 100), (1.0, 0.45, 0.12)),
          "u": ((325, 700, 2530), (60, 70, 100), (1.0, 0.25, 0.08))}


def choir(f, dur, vel, rng, vowel="a", voices=4, attack=0.35):
    n = int((dur + 0.8) * SR)
    t = _t(n)
    forms, bws, gains = VOWELS[vowel]
    out = np.zeros(n)
    env = _attack(t, attack) * np.where(t < dur, 1.0, np.exp(-(t - dur) * 5.0))
    for v in range(voices):
        fv = f * 2.0 ** (rng.normal(0, 7.0) / 1200.0)
        ph = _vibrato_phase(fv, n, rng, rng.uniform(4.8, 5.6), 0.004, 0.3, 0.5)
        for h in range(1, 31):
            fh = fv * h
            if fh > 7000:
                break
            g = sum(gain * np.exp(-0.5 * ((fh - fc) / bw) ** 2) for fc, bw, gain in zip(forms, bws, gains))
            g = g + 0.02 * h ** -1.0
            out += (g * h ** -0.35 / voices) * np.sin(h * ph + rng.uniform(0, TWO_PI))
    breath = _bp(_noise(rng, n), 800, 3500) * 0.012
    return (out + breath) * env * (0.5 + 0.5 * vel) * 0.45


def choir_o(f, dur, vel, rng):
    return choir(f, dur, vel, rng, "o")


def choir_u(f, dur, vel, rng):
    return choir(f, dur, vel, rng, "u", attack=0.5)


# ---------------------------------------------------------------------------------------------------------------------------------------------
# percussion (the "pitch" argument is a drum's tuning where it has one)
# ---------------------------------------------------------------------------------------------------------------------------------------------
def timpani(f, dur, vel, rng):
    n = int(2.2 * SR)
    t = _t(n)
    glide = 1.0 + 0.10 * np.exp(-t / 0.05)
    out = np.zeros(n)
    for r, a, d in ((1.0, 1.0, 0.9), (1.5, 0.45, 0.55), (1.99, 0.3, 0.40), (2.44, 0.22, 0.25), (2.9, 0.12, 0.15)):
        out += a * np.exp(-t / d) * np.sin(TWO_PI * np.cumsum(f * r * glide) / SR + rng.uniform(0, TWO_PI))
    out += _bp(_noise(rng, n), 60, 500) * np.exp(-t / 0.05) * 0.5 * vel
    out += _bp(_noise(rng, n), 1200, 5000) * np.exp(-t / 0.012) * 0.15 * vel      # the mallet
    return out * (0.5 + 0.5 * vel) * 0.9


def timpani_roll(f, dur, vel, rng):
    """a roll that swells: a stream of soft strokes"""
    n = int((dur + 1.2) * SR)
    out = np.zeros(n)
    step = int(0.062 * SR)
    for i, s in enumerate(range(0, int(dur * SR), step)):
        a = (0.35 + 0.65 * (s / max(dur * SR, 1))) * vel * rng.uniform(0.8, 1.0)
        hit = timpani(f, 0.2, a, rng)[: int(1.0 * SR)]
        out[s:s + len(hit)] += hit * 0.55
    return out


def taiko(f, dur, vel, rng):
    n = int(1.4 * SR)
    t = _t(n)
    glide = 1.0 + 0.55 * np.exp(-t / 0.035)
    body = np.sin(TWO_PI * np.cumsum(f * glide) / SR) * np.exp(-t / 0.42)
    body += 0.45 * np.sin(TWO_PI * np.cumsum(f * 1.5 * glide) / SR) * np.exp(-t / 0.22)
    skin = _bp(_noise(rng, n), 180, 1200) * np.exp(-t / 0.07) * 0.5
    hit = _bp(_noise(rng, n), 1500, 6000) * np.exp(-t / 0.010) * 0.22
    return (body + skin + hit) * (0.45 + 0.55 * vel) * 1.1


def bassdrum(f, dur, vel, rng):
    n = int(0.6 * SR)
    t = _t(n)
    glide = 1.0 + 1.1 * np.exp(-t / 0.025)
    body = np.sin(TWO_PI * np.cumsum(f * glide) / SR) * np.exp(-t / 0.18)
    beater = _bp(_noise(rng, n), 1800, 6000) * np.exp(-t / 0.006) * 0.18
    return (body + beater) * (0.5 + 0.5 * vel) * 1.1


def snare(f, dur, vel, rng):
    n = int(0.45 * SR)
    t = _t(n)
    tone = (np.sin(TWO_PI * 190 * t) * np.exp(-t / 0.05) + 0.5 * np.sin(TWO_PI * 330 * t) * np.exp(-t / 0.035)) * 0.5
    wires = _bp(_noise(rng, n), 2200, 9500) * np.exp(-t / 0.11)
    crack = _hp(_noise(rng, n), 3500) * np.exp(-t / 0.012) * 0.4
    return (tone + wires * 0.55 + crack) * (0.4 + 0.6 * vel) * 0.9


def rim(f, dur, vel, rng):
    n = int(0.15 * SR)
    t = _t(n)
    return (np.sin(TWO_PI * 1650 * t) * np.exp(-t / 0.012) + 0.5 * np.sin(TWO_PI * 2480 * t) * np.exp(-t / 0.008)) * (0.4 + 0.6 * vel) * 0.8


def woodblock(f, dur, vel, rng):
    n = int(0.2 * SR)
    t = _t(n)
    return (np.sin(TWO_PI * f * t) * np.exp(-t / 0.03) + 0.4 * np.sin(TWO_PI * f * 2.4 * t) * np.exp(-t / 0.015)) * (0.4 + 0.6 * vel)


def shaker(f, dur, vel, rng):
    n = int(0.16 * SR)
    t = _t(n)
    return _bp(_noise(rng, n), 4500, 10500) * np.exp(-t / 0.035) * _attack(t, 0.008) * (0.3 + 0.7 * vel) * 0.55


def tambourine(f, dur, vel, rng):
    n = int(0.4 * SR)
    t = _t(n)
    out = _bp(_noise(rng, n), 5500, 12000) * np.exp(-t / 0.04) * 0.35
    for fc in (6200, 7400, 8900, 10300):
        out += np.sin(TWO_PI * fc * t * rng.uniform(0.98, 1.02)) * np.exp(-t / 0.11) * 0.06
    return out * (0.3 + 0.7 * vel)


def cymbal(f, dur, vel, rng):
    """a crash: a long wash"""
    n = int(max(dur, 2.8) * SR)
    t = _t(n)
    x = _hp(_bp(_noise(rng, n), 2500, 14000), 3000)
    env = np.exp(-t / 1.1) * (1.0 - np.exp(-t / 0.012))
    return x * env * (0.3 + 0.7 * vel) * 0.55


def swell(f, dur, vel, rng):
    """a cymbal / noise swell that rises to the end of dur (a riser)"""
    n = int((dur + 0.6) * SR)
    t = _t(n)
    x = _bp(_noise(rng, n), 1800, 11000)
    env = np.where(t < dur, (t / dur) ** 2.2, np.exp(-(t - dur) / 0.35))
    return x * env * vel * 0.45


def tubular(f, dur, vel, rng):
    """tubular bell: an inharmonic, long ring (a stinger)"""
    return _bar(f, dur, vel, rng, (1.0, 2.76, 5.40, 8.93, 11.34), (1.0, 0.6, 0.4, 0.25, 0.12), (4.0, 2.4, 1.6, 1.0, 0.7), 4.0, 0.5, 0.05) * 0.7


INSTRUMENTS = {
    "harp": harp, "pizz": pizz, "bass": bass_pluck,
    "celesta": celesta, "glock": glock, "vibes": vibes, "marimba": marimba,
    "piano": piano, "flute": flute, "ocarina": ocarina,
    "strings": strings_soft, "stac": strings_stac, "cello": cello,
    "horn": horn, "trumpet": trumpet, "stab": brass_stab, "trombone": trombone,
    "choir": choir, "choir_o": choir_o, "choir_u": choir_u,
    "timp": timpani, "timp_roll": timpani_roll, "taiko": taiko, "kick": bassdrum, "snare": snare, "rim": rim, "wood": woodblock,
    "shaker": shaker, "tamb": tambourine, "crash": cymbal, "riser": swell, "tubular": tubular,
}
UNPITCHED = {"snare", "rim", "shaker", "tamb", "crash", "riser", "kick"}   # (kick keeps its tuning, but it is never transposed with a part)
