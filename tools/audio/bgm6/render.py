"""From a score (a list of notes) to the stems the engine plays.

  Song.add(...)    one note: which stem (layer of the track), which instrument, when (in 16th notes), how long, which pitch, how hard
  render(song)     every note is synthesised (bgm6/synth.py), humanised (a few ms of timing, a few % of touch), panned, and sent to a room; the stems are summed,
                   the reverb tails of the end of the loop are folded back onto its start (so the loop has no seam and no hole), and every stem has the same length
  master(stems)    one gain for the whole track so that the loudest combination the engine can play stays under -2.5 dBFS
  write_ogg(...)   the .ogg files (name_s0.ogg ...), checked by decoding them again

The reverbs are the two rooms of the two faces: HALL (blue: long, dark, wide) and ROOM (red: short, dry, close). A note sends to one of them.
"""
import os
import shutil
import subprocess
import numpy as np
from scipy.signal import fftconvolve, butter, sosfilt
import soundfile as sf
from . import synth as S

SR = S.SR
STEP_PER_BAR = 16

# the red face and the peak are sparser than the rest (staccato, short notes): measured, their stems came out 3 - 6 dB under the foundation, so they are trimmed up
STEM_TRIM_DB = {2: 3.5, 3: 1.0}
_HP30 = butter(2, 30.0 / (SR / 2), "highpass", output="sos")

# the loudness of an instrument is calibrated once (a reference note), so that a level in dB in the score means the same for every instrument
_CAL = {}


def calibrate(inst):
    if inst in _CAL:
        return _CAL[inst]
    fn = S.INSTRUMENTS[inst]
    rng = np.random.default_rng(99)
    pitched = inst not in S.UNPITCHED
    f = {"bass": 98.0, "cello": 130.8, "trombone": 123.5, "choir_u": 196.0, "timp": 98.0, "timp_roll": 98.0, "taiko": 82.0, "kick": 55.0, "wood": 880.0}.get(inst, 392.0)
    y = fn(f, 0.7, 0.75, rng)
    seg = y[: int(0.6 * SR)]
    if inst in ("snare", "rim", "wood", "shaker", "tamb", "crash", "riser", "kick", "taiko", "timp", "timp_roll", "tubular"):
        ref = np.abs(y).max() / 0.6                       # percussion: by the peak
    else:
        ref = np.sqrt(np.mean(seg ** 2)) / 0.14           # the rest: by the loudness of the sustained part
    _CAL[inst] = 1.0 / max(ref, 1e-6)
    return _CAL[inst]


# ---------------------------------------------------------------------------------------------------------------------------------------------
# rooms
# ---------------------------------------------------------------------------------------------------------------------------------------------
def make_ir(seconds, damping, early, seed, predelay=0.012):
    """an impulse response: early reflections, then a noise tail that dies faster in the highs than in the lows (the air absorbs them)."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    out = np.zeros((2, n))
    for c in range(2):
        tail = np.zeros(n)
        for lo, hi, rt in ((60, 400, 1.0), (400, 2000, 0.8), (2000, 6000, 0.55 * damping), (6000, 14000, 0.30 * damping)):
            nz = rng.standard_normal(n)
            sos = butter(2, [lo / (SR / 2), min(hi, SR * 0.45) / (SR / 2)], "bandpass", output="sos")
            tail += sosfilt(sos, nz) * np.exp(-t * (6.9 / (seconds * rt)))
        p = int(predelay * SR)
        tail[:p] = 0
        tail[p:p + int(0.02 * SR)] *= np.linspace(0, 1, int(0.02 * SR))
        out[c] = tail
        for k in range(early):                      # the first reflections: a few discrete taps in the first 70 ms
            d = int(rng.uniform(0.008, 0.07) * SR)
            if d < n:
                out[c, d] += rng.uniform(0.25, 0.6) * (1 if rng.random() > 0.3 else -1) * 3.0
    out /= np.sqrt(np.sum(out ** 2, axis=1, keepdims=True)) + 1e-9
    return out


_IR = {}


def room(name):
    if name not in _IR:
        if name == "hall":
            _IR[name] = make_ir(2.6, 0.9, 10, 5)
        elif name == "room":
            _IR[name] = make_ir(0.55, 0.6, 7, 6, 0.004)
        elif name == "cathedral":
            _IR[name] = make_ir(3.6, 1.0, 12, 7, 0.02)
        else:
            raise KeyError(name)
    return _IR[name]


# ---------------------------------------------------------------------------------------------------------------------------------------------
class Song:
    def __init__(self, name, bpm, bars, stems, polar=1):
        self.name, self.bpm, self.bars, self.nstems, self.polar = name, bpm, bars, stems, polar
        self.events = []
        self.step_s = 60.0 / bpm / 4.0
        self.frames = int(round(bars * 16 * self.step_s * SR))
        self.levels = {}          # (stem, bus) -> extra gain in dB

    def add(self, stem, inst, step, dur, midi, vel=0.7, pan=0.0, send="hall", wet=0.25, db=0.0, jitter=4.0, lag=0.0):
        """step: in 16th notes from the start of the track (a float is fine). dur: in 16th notes. wet: how much goes to the room (0..1).
        jitter / lag: milliseconds of random / constant lateness (the horns sit a little behind the beat, the shakers a little in front)."""
        self.events.append((stem, inst, float(step), float(dur), float(midi), float(vel), float(pan), send, float(wet), float(db), float(jitter), float(lag)))


def _pan(sig, pan):
    a = (pan + 1.0) * np.pi / 4.0
    return np.stack([sig * np.cos(a), sig * np.sin(a)])


def render(song, seed=1, tail=4.2, log=None):
    rng = np.random.default_rng(seed)
    N = song.frames
    T = N + int(tail * SR)
    dry = [np.zeros((2, T), np.float32) for _ in range(song.nstems)]
    wet = {}
    cache = {}
    for ev in sorted(song.events, key=lambda e: e[2]):
        stem, inst, step, dur, midi, vel, pan, send, wetamt, db, jit, lag = ev
        fn = S.INSTRUMENTS[inst]
        dur_s = dur * song.step_s
        key = (inst, int(round(midi * 2)), int(round(dur_s * 40)), int(round(vel * 8)))
        variants = cache.setdefault(key, [])
        if len(variants) < 3:
            f = float(S.hz(midi))
            y = fn(f, dur_s, vel, rng).astype(np.float32) * calibrate(inst)
            variants.append(y)
            note = y
        else:
            note = variants[int(rng.integers(0, len(variants)))]
        gain = 10.0 ** (db / 20.0) * (0.94 + 0.12 * rng.random())          # the touch is never the same twice
        t0 = step * song.step_s + (rng.normal(0.0, jit) + lag) / 1000.0
        i0 = int(round(t0 * SR))
        if i0 < 0:
            i0 += N                                                        # a note a hair before the start belongs to the end of the loop
        i1 = min(T, i0 + len(note))
        seg = note[: i1 - i0] * gain
        st = _pan(seg, pan)
        dry[stem][:, i0:i1] += st
        if send and wetamt > 0:
            w = wet.setdefault((stem, send), np.zeros((2, T), np.float32))
            w[:, i0:i1] += st * wetamt
    out = []
    for stem in range(song.nstems):
        mix = dry[stem].copy()
        for (s, bus), w in wet.items():
            if s != stem:
                continue
            ir = room(bus)
            conv = np.stack([fftconvolve(w[c], ir[c])[:T] for c in range(2)]).astype(np.float32)
            mix += conv * 1.0
        # fold the tail that is behind the end of the loop onto its start: the loop is one circle
        head = mix[:, N:T]
        mix = mix[:, :N]
        k = min(head.shape[1], N)
        mix[:, :k] += head[:, :k]
        mix = mix * 10.0 ** (STEM_TRIM_DB.get(stem, 0.0) / 20.0) if song.nstems == 4 else mix
        mix = sosfilt(_HP30, mix, axis=1).astype(np.float32)               # (nothing below 30 Hz: a room does not need it and a speaker cannot use it)
        out.append(mix)
        if log:
            log(f"   stem {stem}: {len([e for e in song.events if e[0] == stem])} notes")
    return out


def _peak_db(x):
    return 20 * np.log10(np.abs(x).max() + 1e-9)


def _rms_db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-12)


def combos(song):
    """the sums the engine can play: s0 always; the faces weighted by the colour (1 and 0.35 at the ends, 0.81 / 0.81 in the middle of a switch); the peak with level 2."""
    n = song.nstems
    if n == 1:
        return [[1.0]]
    if n == 3:
        return [[1.0, 1.0, 1.0]]                                           # the title: everything all the time
    return [[1, 1, 0.35, 1], [1, 0.35, 1, 1], [1, 0.81, 0.81, 1]]


def master(song, stems, target_peak_db=-2.5, target_rms_db=-18.0):
    """one gain for all the stems of a track (they must keep their balance). The loudest mix the engine can play is brought to target_rms_db, so that the tracks of the game
    are equally loud (the stage does not jump when the boss comes); if that would push a peak above target_peak_db the peaks win (up to 3 dB of peak may be left
    for the limiter of the engine)."""
    worst_peak, rms_vals = -99.0, []
    for w in combos(song):
        mix = sum(g * s for g, s in zip(w, stems))
        worst_peak = max(worst_peak, _peak_db(mix))
        rms_vals.append(_rms_db(mix))
    rms_now = max(rms_vals)
    gain_db = target_rms_db - rms_now
    gain_db = min(gain_db, target_peak_db + 3.0 - worst_peak)
    g = 10.0 ** (gain_db / 20.0)
    return [s * g for s in stems], gain_db, worst_peak, rms_now


# ---------------------------------------------------------------------------------------------------------------------------------------------
def write_ogg(path, x, quality=4):
    """x: (2, N) float. Ogg Vorbis, quality 4 (about 128 kbps). The file is decoded again and must have the same length."""
    pcm = np.clip(x.T, -1.0, 1.0)
    base = path[:-4]
    wav = base + "._tmp.wav"
    sf.write(wav, pcm, SR, subtype="PCM_16")
    try:
        if shutil.which("ffmpeg"):
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", str(quality), path], check=True)
        else:
            sf.write(path, pcm, SR, format="OGG", subtype="VORBIS")
    finally:
        os.remove(wav)
    n = sf.info(path).frames
    return n
