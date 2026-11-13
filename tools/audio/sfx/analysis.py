"""Measurements of a rendered sound. The levels of the pack are set by these numbers (build_sfx.py), and the checks of a recipe (`build_sfx.py --check`) print them: a recipe is
judged by what it measures as well as by how it sounds."""
import numpy as np
from scipy.signal import find_peaks

from .dsp import SR


def a_weight(n):
    """the A-weighting curve for the bins of an n-sample rfft (power, linear)."""
    f = np.fft.rfftfreq(n, 1.0 / SR)
    f2 = np.maximum(f, 1.0) ** 2
    ra = (12194.0 ** 2 * f2 ** 2) / ((f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194.0 ** 2))
    return ra * 10 ** (2.0 / 20)


def gated_level(x, gate_db=25.0, frame_ms=10.0):
    """the A-weighted RMS (dBFS) of the part of the sound that is loud: the frames within `gate_db` of the loudest."""
    w = np.fft.irfft(np.fft.rfft(x) * a_weight(len(x)), len(x))
    n = int(SR * frame_ms / 1000)
    k = len(w) // n
    if k == 0:
        return -99.0
    p = (w[: k * n].reshape(k, n) ** 2).mean(axis=1)
    keep = p > p.max() * 10 ** (-gate_db / 10)
    return 10 * np.log10(p[keep].mean() + 1e-20)


def centroid(x):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    return float((s * f).sum() / (s.sum() + 1e-20))


def band_shares(x, edges=(0, 250, 500, 1500, 4000, 8000, 22050)):
    """the share (%) of the A-weighted energy of the sound that is in each band."""
    spec = np.abs(np.fft.rfft(x)) ** 2 * a_weight(len(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    total = spec.sum() + 1e-20
    return [100.0 * spec[(f >= lo) & (f < hi)].sum() / total for lo, hi in zip(edges[:-1], edges[1:])]


def tonality(x, peaks=4):
    """how much of the energy sits in a few narrow spectral peaks (%): 90 % is a struck bar or a flute, 30 % is a snap of noise. A weapon is under 50 %."""
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    idx, props = find_peaks(spec, prominence=spec.max() * 1e-3)
    if len(idx) == 0:
        return 0.0
    top = np.sort(spec[idx])[::-1][:peaks]
    width = 3                                                       # a peak is the bin and its two neighbours
    return float(100.0 * min(1.0, (top.sum() * width) / (spec.sum() + 1e-20)))


def dissonance(x, frame=0.15, hop=0.075, partials=12):
    """the sensory dissonance (Sethares, 1993) of the sound, averaged over time: for every pair of the strongest partials of a frame, the roughness that their distance in
    frequency makes in the ear (partials that are a few Hz apart beat: that is what the ear hears as 'rough'). A pure interval is near 0; a cluster of bell partials is high.
    The number is only for comparing one sound with another: it is not in any unit."""
    n = int(frame * SR)
    h = int(hop * SR)
    if len(x) < n:                                                  # a sound shorter than a frame: one frame, padded
        x = np.pad(x, (0, n - len(x)))
    values = []
    for start in range(0, max(1, len(x) - n), h):
        seg = x[start:start + n] * np.hanning(n)
        if np.abs(seg).max() < 1e-4:
            continue
        spec = np.abs(np.fft.rfft(seg, 4 * n))
        f = np.fft.rfftfreq(4 * n, 1.0 / SR)
        idx, _ = find_peaks(spec, prominence=spec.max() * 0.02)
        idx = idx[(f[idx] > 80) & (f[idx] < 6000)]
        idx = idx[np.argsort(spec[idx])[::-1][:partials]]
        if len(idx) < 2:
            continue
        fr, am = f[idx], spec[idx] / spec[idx].max()
        total = 0.0
        for i in range(len(fr)):
            for j in range(i + 1, len(fr)):
                lo = min(fr[i], fr[j])
                s = 0.24 / (0.0207 * lo + 18.96)
                d = abs(fr[i] - fr[j])
                total += am[i] * am[j] * (np.exp(-3.5 * s * d) - np.exp(-5.75 * s * d))
        values.append(total)
    return float(np.mean(values)) if values else 0.0
