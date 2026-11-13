"""The theme of the game ("MIRROR"), what can be done to it, and the small tools of harmony.

THE IDEA (after the way Koji Kondo and Nobuo Uematsu build a score, not after a sample library)
  * ONE short, singable theme that the whole game is made of. Two notes on one pitch and a leap of a fourth (A A D), then a step down: the "motto". It is the first
    thing the ear learns, and it can be played by a single flute.
  * It is not repeated, it is TRANSFORMED, and every place of the game has its own transformation (Kondo: one tune, a hundred moods; Uematsu: the same melody as a
    lullaby, as a march, as a battle hymn):
        augmentation    every note twice as long                  the title: the theme as an overture
        inversion       the theme turned upside down (the mirror)  the RED face: what the blue face says, red says the other way round
        displacement    the same notes, off the beat               a stage that stumbles (3 + 3 + 2)
        fragmentation   only the motto, passed between voices      stage C: a call and an answer
        stretto         the theme and its mirror, a bar apart      the second phase of the boss (the sky splits in two)
        reharmonisation the same notes over other chords           the bridge, the clear screen (major)
        modulation      the key lifts a step                       the end of stage C and the third phase of the boss
  * The harmony is the second voice of the theme: dorian (a minor sound with a bright sixth, the sound of an adventure, not of a tragedy) for the stages, aeolian
    (the sixth falls: the boss), ionian (the same notes in the light: the clear screen).
A note is (step, length, degree) in 16th notes from the start of the phrase; degree 0 is the tonic, 7 the tonic an octave up (scale degrees, so a transposition or a
change of mode keeps the shape). An optional fourth value is a chromatic alteration in semitones.
"""
import itertools
import numpy as np

MODES = {
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "aeolian": [0, 2, 3, 5, 7, 8, 10],
    "ionian": [0, 2, 4, 5, 7, 9, 11],
    "lydian": [0, 2, 4, 6, 7, 9, 11],
    "phrygian": [0, 1, 3, 5, 7, 8, 10],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10],
}
NOTE = {"C": 60, "C#": 61, "Db": 61, "D": 62, "D#": 63, "Eb": 63, "E": 64, "F": 65, "F#": 66, "Gb": 66, "G": 67, "G#": 68, "Ab": 68, "A": 69, "A#": 70, "Bb": 70, "B": 71}


class Key:
    def __init__(self, tonic, mode="dorian"):
        self.tonic = NOTE[tonic] if isinstance(tonic, str) else tonic   # the MIDI note of degree 0 (the octave of middle C)
        self.mode = mode

    def deg(self, d, alt=0):
        octave, i = divmod(int(d), 7)
        return self.tonic + 12 * octave + MODES[self.mode][i] + alt

    def lifted(self, semitones, mode=None):
        return Key(self.tonic + semitones, mode or self.mode)


def N(step, length, degree, alt=0):
    return (step, length, degree, alt)


# ---------------------------------------------------------------------------------------------------------------------------------------------
# the theme (D dorian degrees: 0 D4, 4 A4, 7 D5, 9 F5 ...). Every phrase is two bars = 32 steps.
# ---------------------------------------------------------------------------------------------------------------------------------------------
MOTTO = [N(0, 2, 4), N(3, 1, 4), N(4, 3, 7)]                                    # A A D (the call)

# phrase A: bar 1 the motto and its answer, bar 2 the same a third lower (a sequence: "it is going somewhere")
A1 = [N(0, 2, 4), N(3, 1, 4), N(4, 3, 7), N(8, 2, 6), N(10, 2, 4), N(12, 4, 5),
      N(16, 2, 2), N(19, 1, 2), N(20, 3, 5), N(24, 2, 4), N(26, 2, 2), N(28, 4, 3)]
# antecedent: it climbs to E and falls back, and stops on A (a question)
A2_OPEN = [N(0, 2, 4), N(3, 1, 4), N(4, 3, 7), N(8, 2, 8), N(10, 2, 7), N(12, 4, 6),
           N(16, 4, 5), N(20, 4, 4), N(24, 8, 4)]
# consequent: it climbs higher (to F), then comes down the whole scale and lands on the tonic (the answer)
A2_CLOSE = [N(0, 2, 4), N(3, 1, 4), N(4, 3, 7), N(8, 2, 8), N(10, 2, 9), N(12, 4, 8),
            N(16, 2, 7), N(18, 2, 6), N(20, 2, 5), N(22, 2, 4), N(24, 2, 3), N(26, 2, 2), N(28, 4, 0)]

# the bridge (4 bars): long notes, the motto grown into a line. Over Bb - C - Dm - A (bVI bVII i V).
BRIDGE = [N(0, 6, 7), N(6, 4, 9), N(10, 6, 8),
          N(16, 4, 8), N(20, 4, 10), N(24, 8, 9),
          N(32, 6, 11), N(38, 2, 10), N(40, 4, 9), N(44, 4, 8),
          N(48, 4, 7), N(52, 4, 6, 1), N(56, 8, 7)]
# the lift: the same bridge a third higher in its second half, for the last four bars of a long climb
BRIDGE_HIGH = [N(s, l, d + (2 if s >= 16 else 0), a) for s, l, d, a in BRIDGE]

THEME_A = A1 + [(s + 32, l, d, a) for s, l, d, a in A2_OPEN]          # 4 bars, open
THEME_A_CLOSED = A1 + [(s + 32, l, d, a) for s, l, d, a in A2_CLOSE]  # 4 bars, closed


def mirror(notes, axis=4, resolve=False):
    """INVERSION: every degree d becomes 2 * axis - d (the theme upside down, in the scale). resolve: the last note goes home (the mirror is allowed to end on the tonic)."""
    out = [(s, l, 2 * axis - d, -a) for s, l, d, a in notes]
    if resolve and out:
        s, l, d, a = out[-1]
        out[-1] = (s, l, 7 if d >= 7 else 0, 0)
    return out


def augment(notes, k=2):
    return [(s * k, l * k, d, a) for s, l, d, a in notes]


def diminish(notes, k=2):
    return [(int(s / k), max(1, int(round(l / k))), d, a) for s, l, d, a in notes]


def displace(notes, steps, wrap=None):
    """the same notes later (or earlier): the downbeat moves. wrap: length in steps of the phrase (notes that fall off the end come back at the start)."""
    out = []
    for s, l, d, a in notes:
        s2 = s + steps
        if wrap:
            s2 %= wrap
        out.append((s2, l, d, a))
    return sorted(out)


def transpose(notes, degrees):
    return [(s, l, d + degrees, a) for s, l, d, a in notes]


def octave(notes, n):
    return [(s, l, d + 7 * n, a) for s, l, d, a in notes]


def window(notes, a, b):
    """the notes that start in [a, b), moved so that a is 0"""
    return [(s - a, l, d, al) for s, l, d, al in notes if a <= s < b]


def at(notes, step):
    return [(s + step, l, d, a) for s, l, d, a in notes]


def stretch_to(notes, bars):
    """a phrase that is shorter than `bars` is repeated (as it is) until it fills them"""
    out = []
    span = max(s + l for s, l, d, a in notes)
    span = int(np.ceil(span / 16.0)) * 16
    for k in range(0, bars * 16, span):
        out += at(notes, k)
    return [n for n in out if n[0] < bars * 16]


# ---------------------------------------------------------------------------------------------------------------------------------------------
# harmony
# ---------------------------------------------------------------------------------------------------------------------------------------------
QUALITY = {
    "maj": [0, 4, 7], "min": [0, 3, 7], "dim": [0, 3, 6], "sus4": [0, 5, 7], "sus2": [0, 2, 7],
    "maj7": [0, 4, 7, 11], "maj7x": [0, 4, 11], "m7": [0, 3, 7, 10], "7": [0, 4, 7, 10], "m7b5": [0, 3, 6, 10], "7sus4": [0, 5, 7, 10],
    "add9": [0, 4, 7, 14], "madd9": [0, 3, 7, 14], "m9": [0, 3, 7, 10, 14], "9": [0, 4, 7, 10, 14], "maj9": [0, 4, 7, 11, 14], "m11": [0, 3, 7, 10, 14, 17],
    "5": [0, 7, 12],
}


class Chord:
    def __init__(self, root, quality, bass=None):
        self.root = root            # semitones above the tonic of the key it is written in (may be negative: Bb in D = -4 or 8)
        self.quality = quality
        self.bass = root if bass is None else bass

    def pcs(self):
        return [(self.root + i) % 12 for i in QUALITY[self.quality]]


def C(root, quality, bass=None):
    return Chord(root, quality, bass)


def chord_midi(chord, key, octave_of_root=3):
    """absolute MIDI notes of a chord, root position, root in the octave `octave_of_root` (middle C = octave 4)."""
    base = key.tonic + chord.root
    base = base - 12 * ((base - (12 * (octave_of_root + 1))) // 12 + 0) if False else base
    # bring the root into [12 * (octave + 1), 12 * (octave + 2))
    lo = 12 * (octave_of_root + 1)
    while base >= lo + 12:
        base -= 12
    while base < lo:
        base += 12
    return [base + i for i in QUALITY[chord.quality]]


def bass_note(chord, key, lo=34, hi=46):
    m = key.tonic + chord.bass
    while m >= hi:
        m -= 12
    while m < lo:
        m += 12
    return m


def voice(chord, key, prev=None, lo=52, hi=74, size=4, omit_root=True):
    """a close voicing of the chord between lo and hi, led from the previous one by the smallest moves (smooth voice leading: what a good arranger does by ear).
    The root is left to the bass when the chord has enough other notes (3rd, 5th, 7th, 9th: the colour tones)."""
    pcs = []
    for i in QUALITY[chord.quality]:
        pc = (key.tonic + chord.root + i) % 12
        if pc not in pcs:
            pcs.append(pc)
    root_pc = (key.tonic + chord.root) % 12
    if omit_root and len(pcs) - 1 >= min(size, 3) and root_pc in pcs:
        pcs.remove(root_pc)
    pcs = pcs[:size]
    target = prev if prev else [lo + 4 + 3 * i for i in range(size)]
    # every stack of distinct pitch classes (any order), each in some octave inside the range, ascending. The rules are relaxed step by step if no stack satisfies them.
    for max_span, allow_m2, widen in ((19, False, 0), (24, False, 0), (24, False, 3), (24, True, 3)):
        best, best_cost = None, 1e9
        for combo in itertools.permutations(pcs, min(size, len(pcs))):
            cands = [[m for m in range(lo - widen, hi + widen + 1) if m % 12 == pc] for pc in combo]
            for stack in itertools.product(*cands):
                if list(stack) != sorted(stack) or len(set(stack)) < len(stack):
                    continue
                if stack[-1] - stack[0] > max_span:
                    continue
                if not allow_m2 and any(b - a == 1 for a, b in zip(stack, stack[1:])):
                    continue                                   # no minor second inside a voicing (the 9th sits a seventh above the 3rd, never next to it)
                tgt = target[: len(stack)] if len(target) >= len(stack) else target + [target[-1]] * (len(stack) - len(target))
                cost = sum(abs(a - b) for a, b in zip(stack, tgt)) + 0.4 * abs(np.mean(stack) - (lo + hi) / 2)
                if cost < best_cost:
                    best, best_cost = list(stack), cost
        if best is not None:
            return best
    return [lo + i * 4 for i in range(min(size, 3))]


def degree_notes(notes, key, bar0=0):
    """(step, length, degree, alt) -> (absolute step, length, midi)"""
    return [(bar0 * 16 + s, l, key.deg(d, a)) for s, l, d, a in notes]
