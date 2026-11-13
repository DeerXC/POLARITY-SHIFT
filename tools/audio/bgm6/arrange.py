"""The tools of an arranger: put a theme, a chord chart, a bass line, an arpeggio, an ostinato or a drum pattern on a song.
A chart is a list with one entry per bar; an entry is a Chord (the whole bar) or a list of (start step, Chord)."""
import numpy as np
from . import theme as T


class Chart:
    def __init__(self, key, bars):
        self.key = key
        self.bars = list(bars)

    def segments(self, bar):
        e = self.bars[bar % len(self.bars)]
        if isinstance(e, T.Chord):
            return [(0, 16, e)]
        out = []
        for i, (s, c) in enumerate(e):
            end = e[i + 1][0] if i + 1 < len(e) else 16
            out.append((s, end, c))
        return out

    def at(self, bar, step):
        for s, end, c in self.segments(bar):
            if s <= step < end:
                return c
        return self.segments(bar)[-1][2]

    def __len__(self):
        return len(self.bars)


def lead(song, stem, inst, notes, key, bar0, vel=0.75, pan=0.0, send="hall", wet=0.3, db=0.0, legato=0.96, octave=0, lag=0.0, jitter=5.0, accent=0.10, shape=None):
    """a melody. notes: (step, length, degree, alt) from the start of bar bar0. shape: optional function(note index, count) -> velocity factor (a phrase has a line)."""
    for i, (s, l, d, a) in enumerate(notes):
        m = key.deg(d, a) + 12 * octave
        v = vel * (1.0 if (s % 4 == 0 or l >= 4) else 1.0 - accent)
        if shape:
            v *= shape(i, len(notes))
        song.add(stem, inst, bar0 * 16 + s, max(0.5, l * legato), m, min(v, 1.0), pan, send, wet, db, jitter, lag)


def phrase_shape(i, n):
    """the arc of a phrase: a little softer at the start, the top in the middle, a little less at the end"""
    x = i / max(n - 1, 1)
    return 0.90 + 0.14 * np.sin(np.pi * x)


def pad_voicings(chart, bar0, bars, lo=52, hi=72, size=4, omit_root=True, prev=None):
    """the voicing of every chord of the chart, led smoothly from one to the next: {(bar, start step): [midi...]}. Every sustained part (strings, horns, vibraphone, choir)
    uses THESE notes (in the same octave or an octave away), so two pads never sit a semitone apart."""
    out = {}
    for b in range(bar0, bar0 + bars):
        for s, e, c in chart.segments(b):
            prev = T.voice(c, chart.key, prev, lo, hi, size, omit_root)
            out[(b, s)] = prev
    return out


def pads(song, stem, inst, chart, bar0, bars, vel=0.45, pan=0.0, send="hall", wet=0.5, db=0.0, lo=52, hi=72, size=4, overlap=1.0, voicings=None, pick=None, shift=0, omit_root=True, lag=0.0):
    """sustained chords. A chord lasts until the next one begins (plus a little overlap: no hole between them). voicings: use these notes (pad_voicings); pick: which of them
    (indices); shift: semitones (an octave = 12)."""
    vs = voicings or pad_voicings(chart, bar0, bars, lo, hi, size, omit_root)
    for b in range(bar0, bar0 + bars):
        for s, e, c in chart.segments(b):
            v = vs[(b, s)]
            idx = range(len(v)) if pick is None else [i for i in pick if i < len(v)]
            for i in idx:
                song.add(stem, inst, b * 16 + s, (e - s) + overlap, v[i] + shift, vel * (0.95 + 0.05 * (i % 2)), pan + (i - len(v) / 2) * 0.06, send, wet, db, 14.0, lag)
    return vs


def bassline(song, stem, inst, chart, bar0, bars, pattern, vel=0.7, db=0.0, send="room", wet=0.1, lo=34, hi=46, lag=0.0, fills=None):
    """pattern: list of (step, length, kind[, vel factor]); kind r (root), 5 (the fifth above), 8 (the octave above), 7 (the minor seventh below the octave), 3 (the third), '-5' (the fifth below)."""
    key = chart.key
    for b in range(bar0, bar0 + bars):
        pat = fills.get(b, pattern) if fills else pattern
        for entry in pat:
            s, l, kind = entry[0], entry[1], entry[2]
            vf = entry[3] if len(entry) > 3 else 1.0
            c = chart.at(b, s)
            m = T.bass_note(c, key, lo, hi)
            off = {"r": 0, "5": 7, "8": 12, "7": 10, "3": 3 if 3 in [i % 12 for i in T.QUALITY[c.quality]] else 4, "-5": -5}[kind]
            if kind == "3":
                off = [i for i in T.QUALITY[c.quality] if i in (3, 4)][0] if any(i in (3, 4) for i in T.QUALITY[c.quality]) else 4
            song.add(stem, inst, b * 16 + s, l, m + off, min(1.0, vel * vf), 0.0, send, wet, db, 3.0, lag)


def arp(song, stem, inst, chart, bar0, bars, pattern, vel=0.55, pan=0.0, send="hall", wet=0.4, db=0.0, lo=55, hi=79, size=4, oct_shift=0, jitter=3.0, lag=0.0, swing_pan=0.35, prev=None):
    """an arpeggio over the chart. pattern: list of (step, length, index[, vel factor]); the index counts up the voicing and on into the octave above it."""
    key = chart.key
    for b in range(bar0, bar0 + bars):
        segs = chart.segments(b)
        voicings = {}
        for s, e, c in segs:
            prev = T.voice(c, key, prev, lo, hi, size, omit_root=False)
            voicings[s] = prev
        for entry in pattern:
            s, l, idx = entry[0], entry[1], entry[2]
            vf = entry[3] if len(entry) > 3 else 1.0
            seg_start = max(k for k in voicings if k <= s)
            v = voicings[seg_start]
            ext = v + [x + 12 for x in v] + [x + 24 for x in v]
            m = ext[idx % len(ext)] + 12 * oct_shift
            p = pan + swing_pan * np.sin(idx * 1.7 + s)
            song.add(stem, inst, b * 16 + s, l, m, min(1.0, vel * vf), float(np.clip(p, -0.9, 0.9)), send, wet, db, jitter, lag)
    return prev


def ostinato(song, stem, inst, chart, bar0, bars, rhythm, vel=0.6, pan=0.0, send="room", wet=0.12, db=0.0, lo=43, hi=64, lag=0.0, jitter=3.0, roles=None):
    """a repeated figure on the chord. rhythm: list of (step, length, role[, vel factor]) where role is r (root), 5 (fifth), 8 (octave), 3 (third), 7 (seventh)."""
    key = chart.key
    for b in range(bar0, bar0 + bars):
        for entry in rhythm:
            s, l, role = entry[0], entry[1], entry[2]
            vf = entry[3] if len(entry) > 3 else 1.0
            c = chart.at(b, s)
            r = key.tonic + c.root
            while r < lo:
                r += 12
            while r >= lo + 12:
                r -= 12
            third = [i for i in T.QUALITY[c.quality] if i in (3, 4, 2, 5)]
            seventh = [i for i in T.QUALITY[c.quality] if i in (10, 11)]
            off = {"r": 0, "5": 7, "8": 12, "3": third[0] if third else 4, "7": seventh[0] if seventh else 10, "-5": -5, "4": 5}[role]
            m = r + off
            song.add(stem, inst, b * 16 + s, l, m, min(1.0, vel * vf), pan, send, wet, db, jitter, lag)


def hits(song, stem, inst, bar0, bars, steps, vel=0.6, pitch=60, pan=0.0, send="room", wet=0.1, db=0.0, jitter=3.0, lag=0.0, only=None, skip=None):
    """a drum pattern: steps = list of step or (step, vel factor). only / skip: sets of bar numbers (counted from bar0) where it plays / does not."""
    for b in range(bar0, bar0 + bars):
        rb = b - bar0
        if only is not None and rb not in only:
            continue
        if skip is not None and rb in skip:
            continue
        for e in steps:
            s, vf = (e, 1.0) if not isinstance(e, tuple) else e
            song.add(stem, inst, b * 16 + s, 1.0, pitch, min(1.0, vel * vf), pan, send, wet, db, jitter, lag)


def roll(song, stem, inst, bar, start, count, spacing, v0, v1, pitch=60, pan=0.0, wet=0.12, send="room", db=0.0):
    """a drum roll / fill: `count` strokes, `spacing` steps apart, from v0 to v1."""
    for i in range(count):
        v = v0 + (v1 - v0) * i / max(count - 1, 1)
        song.add(stem, inst, bar * 16 + start + i * spacing, 1.0, pitch, v, pan, send, wet, db, 2.0, 0.0)


def swell(song, stem, bar, start, dur, vel=0.6, db=0.0):
    song.add(stem, "riser", bar * 16 + start, dur, 60, vel, 0.0, "hall", 0.25, db, 0.0, 0.0)
