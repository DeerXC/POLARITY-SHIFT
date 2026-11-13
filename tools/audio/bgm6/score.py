"""The score. One function per track; each returns a Song (bgm6/render.py).

STEMS (what the engine does with them): s0 the foundation, always playing (it must be a good piece on its own: the card screens play only s0) / s1 the BLUE face /
s2 the RED face (the engine cross-fades s1 and s2 with the colour of the ship) / s3 the peak, from heat level 2. The title has three layers (s0, s1, s2) that all play all the time.

THE TWO FACES: blue = glass and air (harp, celesta, flute, vibraphone, strings legato, a long hall); red = wood and metal (horns, trumpets, marimba, pizzicato, staccato
strings, timpani and taiko, a short dry room). Blue says the theme, red says it upside down (the mirror, theme.mirror). The faces take turns with the theme (a call and its
answer) so that when the player is between the two colours the music is a conversation, not a clash.

EVERY TRACK IS ANOTHER WAY OF SAYING THE SAME THING (see theme.py), not the same music at another speed:
  stage A  VANGUARD   the theme as written; the faces take turns by sections; a driving bass under a cross-rhythm (3 + 3 + 2); sixteenth-note harp
  stage B  CROSSFIRE  the theme DISPLACED off the beat; a pedal in the bass while the chords move over it; a 3-note arpeggio cycled across 4/4 (3 against 4)
  stage C  ECLIPSE    the theme FRAGMENTED: a bar of one face, the answer of the other; DIMINUTION (twice as fast) in the peak; the key lifts a step for the last eight bars
  boss 1   AWAKEN     the motto alone, very low, with long silences between (the boss is reading you)
  boss 2   SPLIT      the theme and its mirror a bar apart, hard left and right (a canon, the sky splits); a hemiola underneath (3 + 3 + 3 + 3 + 4)
  boss 3   CORE       the theme AUGMENTED in the brass and a choir over fast arpeggios; the key is a step higher
  title    OVERTURE   the theme AUGMENTED (twice as long) on a piano with an ocarina: the sparse, wide music of an open field
  clear    VICTORY    the motto as a fanfare, the theme in the major (the same notes in the light)
  game over           the mirror alone, slow, and it does not come home
"""
import numpy as np
from . import theme as T
from . import arrange as A
from .render import Song

M = T.mirror


def _fill_bar(song, bar, stem=2, vel=0.7):
    A.roll(song, stem, "snare", bar, 8, 8, 1, vel * 0.5, vel)


DEFAULT_VOICES = dict(
    blue_main="flute", blue_dbl="celesta", red_main="horn", red_dbl="trumpet", both_red="cello",
    peak=(("trumpet", 0, 1.0, 0.0), ("flute", 1, 0.8, -0.25), ("horn", -1, 0.8, 0.3)),
)


def _say(song, owner, bar0, notes, key, vel=0.74, sparkle=True, voices=None, octave=0):
    """THE THEME in a voice. owner: blue (flute + celesta), red (horn + trumpet), both (blue + a cello an octave below); the peak (s3) doubles it in brass and flute.
    Written for a key whose tonic is below middle C, the flute and the trumpet go an octave up (a flute cannot sing at A3); `octave` moves the whole thing."""
    v = dict(DEFAULT_VOICES)
    v.update(voices or {})
    low = 1 if key.tonic < 60 else 0
    if owner in ("blue", "both"):
        A.lead(song, 1, v["blue_main"], notes, key, bar0, vel=vel, pan=-0.15, wet=0.45, shape=A.phrase_shape, octave=low + octave)
        if sparkle and v["blue_dbl"]:
            A.lead(song, 1, v["blue_dbl"], notes, key, bar0, vel=vel * 0.55, pan=0.2, wet=0.5, octave=low + 1 + octave, legato=0.7)
    if owner in ("red", "both"):
        if owner == "red":
            A.lead(song, 2, v["red_main"], notes, key, bar0, vel=min(1.0, vel * 1.18), pan=0.15, send="room", wet=0.18, lag=8.0, shape=A.phrase_shape, octave=octave)
            if v["red_dbl"]:
                A.lead(song, 2, v["red_dbl"], notes, key, bar0, vel=vel * 0.55, pan=-0.1, send="room", wet=0.15, lag=8.0, legato=0.8, octave=octave)
        else:
            A.lead(song, 2, v["both_red"], notes, key, bar0, vel=vel * 0.85, pan=0.15, send="room", wet=0.18, lag=8.0, shape=A.phrase_shape, octave=octave + low - 1)
    for inst, oc, vf, pan in v["peak"]:
        A.lead(song, 3, inst, notes, key, bar0, vel=vel * 0.84 * vf, pan=pan, send="hall", wet=0.28, lag=4.0 if inst != "horn" else 6.0, shape=A.phrase_shape, octave=oc + low + octave)


def _rows(pattern, every=1):
    return pattern


def _stage(name, bpm, sections, g):
    """sections: list of dict(key, chart(list of chords per bar), say=[(rel bar, owner, notes, vel)], ...). g: the groove (patterns)."""
    bars = sum(len(s["chart"]) for s in sections)
    song = Song(name, bpm, bars, 4)
    b0 = 0
    for si, sec in enumerate(sections):
        key = sec["key"]
        n = len(sec["chart"])
        ch = A.Chart(key, sec["chart"])
        last = si == len(sections) - 1
        # ---- s0: the foundation (strings pad, a bass, a soft kick, a shaker): a good piece on its own
        pv = A.pads(song, 0, "strings", ch, b0, n, vel=g.get("pad", 0.25), lo=g.get("pad_lo", 52), hi=g.get("pad_hi", 67), send="hall", wet=0.55, overlap=1.5)
        A.bassline(song, 0, g.get("bass_inst", "bass"), ch, b0, n, g["bass"], vel=g.get("bass_vel", 0.52), wet=0.08)
        if g.get("kick"):
            A.hits(song, 0, "kick", b0, n, g["kick"], vel=g.get("kick_vel", 0.46), pitch=45)
        if g.get("shaker"):
            A.hits(song, 0, "shaker", b0, n, g["shaker"], vel=0.5, lag=-6.0, wet=0.05)
        if g.get("wood"):
            A.hits(song, 0, "wood", b0, n, g["wood"], vel=0.26, pitch=84, wet=0.05, skip={n - 1})
        for rb in range(3, n, 4):
            c = ch.at(b0 + rb, 8)
            song.add(0, "cello", (b0 + rb) * 16 + 8, 8, T.bass_note(c, key, 45, 57) + 12, 0.42, -0.2, "hall", 0.4, 0.0, 12.0, 0.0)
        # ---- s1: the blue face (an arpeggio in the harp, glass above it)
        pats = g["harp"]
        for rb in range(n):
            A.arp(song, 1, g.get("arp_inst", "harp"), ch, b0 + rb, 1, pats[rb % len(pats)], vel=g.get("harp_vel", 0.52), lo=g.get("harp_lo", 55), hi=g.get("harp_hi", 79), wet=0.42)
        A.pads(song, 1, "vibes", ch, b0, n, vel=0.17, voicings=pv, pick=(1, 2, 3), shift=12, send="hall", wet=0.7, overlap=0.0)
        for rb in range(n):
            for st in (6, 14):
                top = T.voice(ch.at(b0 + rb, st), key, None, 76, 92, 2, omit_root=False)
                song.add(1, "celesta", (b0 + rb) * 16 + st, 3, top[-1] if rb % 2 else top[0], 0.30, 0.45 if st == 6 else -0.45, "hall", 0.6, 0.0, 6.0, 0.0)
        # ---- s2: the red face (staccato strings and pizzicato on the rhythm, a backbeat, timpani and taiko)
        A.ostinato(song, 2, g.get("stac_inst", "stac"), ch, b0, n, g["stac"], vel=g.get("stac_vel", 0.62), lo=43, hi=55, wet=0.10)
        if g.get("pizz"):
            A.ostinato(song, 2, "pizz", ch, b0, n, g["pizz"], vel=0.6, lo=48, hi=60, wet=0.1)
        if g.get("snare"):
            A.hits(song, 2, "snare", b0, n, g["snare"], vel=0.66, wet=0.12, skip={n - 1})
        if g.get("rim"):
            A.hits(song, 2, "rim", b0, n, g["rim"], vel=0.45, wet=0.05)
        if g.get("taiko"):
            A.hits(song, 2, "taiko", b0, n, g["taiko"], vel=0.78, pitch=43, wet=0.15, only=set(range(0, n, 4)))
        _fill_bar(song, b0 + n - 1, 2, 0.7)
        song.add(2, "taiko", b0 * 16, 1, 38, 0.95, 0.0, "room", 0.2, 0.0, 2.0, 0.0)
        # ---- s3: the peak (everything bigger, and the air around it)
        A.hits(song, 3, "tamb", b0, n, g["tamb"], vel=0.5, wet=0.06, lag=-4.0)
        song.add(3, "crash", b0 * 16, 16, 60, 0.7, 0.0, "hall", 0.3, 0.0, 2.0, 0.0)
        A.swell(song, 3, b0 + n - 1, 0, 15.5, 0.8)
        A.roll(song, 3, "timp", b0 + n - 1, 8, 8, 1, 0.45, 0.9, pitch=38, wet=0.2)
        A.ostinato(song, 3, "stac", ch, b0, n, [(i, 1, "r" if i % 4 == 0 else ("5" if i % 4 == 2 else "8"), 0.9 if i % 4 == 0 else 0.55) for i in range(0, 16)], vel=0.38, lo=55, hi=67, wet=0.1)
        if sec.get("choir"):
            A.pads(song, 3, "choir_o", ch, b0, n, vel=0.26, voicings=pv, pick=(0, 1, 2), shift=12, send="hall", wet=0.6, overlap=0.5)
        # the horns hold the harmony where the red face does not carry the theme
        red_bars = {rb for rb, owner, notes, vel in sec["say"] if owner in ("red", "both")}
        for rb in range(n):
            if rb in red_bars or any(rb0 <= rb < rb0 + 4 and ow in ("red", "both") for rb0, ow, nt, v in sec["say"]):
                continue
            for s, e, c in ch.segments(b0 + rb):
                for m in pv[(b0 + rb, s)][:2]:
                    song.add(2, "horn", (b0 + rb) * 16 + s, (e - s) + 0.5, m, 0.34, 0.2, "room", 0.2, 0.0, 10.0, 6.0)
        # ---- the theme
        for rb, owner, notes, vel in sec["say"]:
            _say(song, owner, b0 + rb, notes, key, vel, voices=sec.get('voices', g.get('voices')), octave=sec.get('octave', 0))
        for extra in sec.get("extra", []):
            extra(song, b0, ch)
        b0 += n
    return song


# ---------------------------------------------------------------------------------------------------------------------------------------------
D = T.Key("D", "dorian")
Dm, G9, Am_sus, Fmaj7 = T.C(0, "min"), T.C(5, "add9"), T.C(7, "sus4"), T.C(3, "maj7")
Bb, Csus2, Gm9, A7, A7s = T.C(-4, "maj7x"), T.C(-2, "sus2"), T.C(5, "m9"), T.C(7, "7"), T.C(7, "7sus4")   # (no fifth in the Bb: the bridge has an E over it, and the F of the chord would be a semitone from it)


def stage_a():
    """VANGUARD: D dorian, 140 BPM. The theme as written."""
    close = [(0, G9), (8, Dm)]
    g = dict(
        bass=[(0, 2, "r", 1.0), (2, 2, "r", 0.55), (4, 2, "r", 0.8), (6, 2, "5", 0.65), (8, 2, "r", 0.95), (10, 2, "r", 0.55), (12, 2, "r", 0.8), (14, 2, "8", 0.7)],
        kick=[0, (6, 0.8), (10, 0.9)],
        shaker=[(0, 0.55), (2, 0.8), (4, 0.55), (6, 0.8), (8, 0.55), (10, 0.8), (12, 0.55), (14, 0.8), (15, 0.35)],
        wood=[(3, 0.6), (11, 0.6)],
        harp=[[(0, 2, 0), (1, 2, 1), (2, 2, 2), (3, 2, 3), (4, 2, 4, 1.15), (5, 2, 3), (6, 2, 2), (7, 2, 1), (8, 2, 0, 1.1), (9, 2, 1), (10, 2, 2), (11, 2, 3), (12, 2, 4, 1.15), (13, 2, 5), (14, 2, 4), (15, 2, 3)],
              [(0, 2, 0, 1.1), (1, 2, 2), (2, 2, 1), (3, 2, 3), (4, 2, 2, 1.1), (5, 2, 4), (6, 2, 3), (7, 2, 5), (8, 2, 4, 1.1), (9, 2, 3), (10, 2, 5), (11, 2, 4), (12, 2, 6, 1.1), (13, 2, 5), (14, 2, 4), (15, 2, 3)]],
        stac=[(0, 1, "r", 1.0), (3, 1, "r", 0.7), (6, 1, "5", 0.85), (8, 1, "r", 0.95), (11, 1, "r", 0.7), (14, 1, "5", 0.8)],
        pizz=[(3, 1, "8", 0.7), (11, 1, "8", 0.7), (14, 1, "3", 0.6)],
        snare=[4, 12, (15, 0.35)], rim=[(7, 0.5), (9, 0.35)], taiko=[0, (10, 0.7)],
        tamb=[(i, 0.8 if i % 4 == 0 else (0.5 if i % 2 == 0 else 0.3)) for i in range(16)],
    )
    sections = [
        dict(key=D, chart=[Dm, G9, Dm, Am_sus, Dm, G9, Dm, close],
             say=[(0, "blue", T.THEME_A, 0.72), (4, "blue", T.THEME_A_CLOSED, 0.74)]),
        dict(key=D, chart=[Dm, G9, Fmaj7, Am_sus, Dm, G9, Dm, [(0, G9), (8, Am_sus)]],
             say=[(0, "red", M(T.THEME_A), 0.74), (4, "red", M(T.THEME_A_CLOSED, resolve=True), 0.76)]),
        dict(key=D, chart=[Bb, Csus2, Dm, A7, Bb, Csus2, [(0, Gm9), (8, A7s)], A7],
             say=[(0, "both", T.BRIDGE, 0.70), (4, "both", T.BRIDGE_HIGH, 0.74)], choir=True),
        dict(key=D, chart=[Dm, G9, Dm, Am_sus, Dm, G9, Dm, [(0, Bb), (8, A7)]],
             say=[(0, "blue", T.THEME_A, 0.76), (4, "red", M(T.THEME_A_CLOSED, resolve=True), 0.78)]),
    ]
    return _stage("stage_a", 140, sections, g)


G = T.Key("G", "dorian")   # G3: the lead voices are written an octave higher (see _say)


def stage_b():
    """CROSSFIRE: G dorian, 128 BPM. The theme DISPLACED off the beat, a pedal G under moving chords, a 3-note arpeggio against four beats."""
    Gm, C9, Dsus, Ebmaj, Fs2, Cm9, D7, D7s = T.C(0, "min", 0), T.C(5, "9", 0), T.C(7, "sus4", 0), T.C(-4, "maj7x", 0), T.C(-2, "sus2", 0), T.C(5, "m9", 0), T.C(7, "7"), T.C(7, "7sus4")
    Bbmaj = T.C(3, "maj7", 0)
    three = lambda start: [(s, 2, (s - start) // 1 % 3 if False else ((s - start) % 3 if False else 0)) for s in ()]
    # a 3-note cycle over 16 steps: 0 1 2 | 0 1 2 | ... so the accent walks across the bar (the figure comes back to the downbeat only every 3 bars)
    cyc = lambda k: [(s, 2, ((s + k) % 3) + (0 if s < 8 else 2), 1.12 if (s + k) % 3 == 0 else 1.0) for s in range(16)]
    g = dict(
        bass=[(0, 3, "r", 1.0), (3, 3, "r", 0.7), (6, 2, "5", 0.8), (8, 3, "r", 0.95), (11, 3, "r", 0.7), (14, 2, "7", 0.75)],
        kick=[0, (3, 0.7), (8, 0.9), (11, 0.6)],
        shaker=[(0, 0.6), (3, 0.8), (6, 0.5), (8, 0.6), (11, 0.8), (14, 0.5), (15, 0.3)],
        wood=[(6, 0.6), (14, 0.5)],
        harp=[cyc(0), cyc(1), cyc(2)],
        stac=[(0, 1, "r", 1.0), (3, 1, "5", 0.7), (6, 1, "r", 0.85), (8, 1, "8", 0.95), (11, 1, "5", 0.7), (14, 1, "r", 0.8)],
        pizz=[(2, 1, "8", 0.6), (7, 1, "3", 0.6), (10, 1, "5", 0.6)],
        snare=[4, 12, (11, 0.4)], rim=[(3, 0.5), (14, 0.5)], taiko=[0, (3, 0.7), (11, 0.8)],
        tamb=[(i, 0.8 if i in (0, 3, 6, 8, 11, 14) else 0.35) for i in range(16)],
        harp_lo=57, harp_hi=81,
    )
    A_ = [Gm, C9, Gm, Dsus, Gm, C9, Gm, [(0, C9), (8, Gm)]]
    Ap = [Gm, C9, Bbmaj, Dsus, Gm, C9, Gm, [(0, C9), (8, Dsus)]]
    Bc = [Ebmaj, Fs2, Gm, D7, Ebmaj, Fs2, [(0, Cm9), (8, D7s)], D7]
    Ar = [Gm, C9, Gm, Dsus, Gm, C9, Gm, [(0, Ebmaj), (8, D7)]]
    sections = [
        dict(key=G, chart=A_, say=[(0, "blue", T.displace(T.THEME_A, 3), 0.72), (4, "blue", T.displace(T.THEME_A_CLOSED, 3), 0.74)]),
        dict(key=G, chart=Ap, say=[(0, "red", M(T.displace(T.THEME_A, 3)), 0.74), (4, "red", M(T.displace(T.THEME_A_CLOSED, 3), resolve=True), 0.76)]),
        dict(key=G, chart=Bc, say=[(0, "both", T.BRIDGE, 0.70), (4, "both", T.BRIDGE_HIGH, 0.74)], choir=True),
        dict(key=G, chart=Ar, say=[(0, "blue", T.displace(T.THEME_A, 3), 0.76), (4, "red", M(T.displace(T.THEME_A_CLOSED, 3), resolve=True), 0.78)]),
    ]
    return _stage("stage_b", 128, sections, g)


Aa = T.Key("A", "dorian")   # A3
Bk = T.Key(59, "dorian")    # B3: a step higher


def stage_c():
    """ECLIPSE: A dorian, 120 BPM, then B dorian. The theme FRAGMENTED: a bar of one face and the answer of the other. DIMINUTION in the peak. The key lifts a step."""
    Am, D9, Esus = T.C(0, "min"), T.C(5, "add9"), T.C(7, "sus4")
    Fm7, Gs, E7, E7s = T.C(3, "maj7"), T.C(-2, "sus2"), T.C(7, "7"), T.C(7, "7sus4")
    Fb, Gb = T.C(-4, "maj7x"), T.C(-2, "sus2")
    sx = lambda k: [(s, 1, ((s // 2 + k) * 2 + (s % 2)) % 5 + (0 if s < 8 else 2), 1.12 if s % 4 == 0 else 1.0) for s in range(0, 16, 1)]
    g = dict(
        bass=[(0, 4, "r", 1.0), (4, 3, "r", 0.6), (8, 3, "5", 0.9), (11, 2, "r", 0.6), (13, 3, "8", 0.7)],
        kick=[0, (10, 0.8)],
        shaker=[(i, 0.45 + 0.35 * ((i % 4) == 2)) for i in range(0, 16, 1)],
        wood=[(7, 0.5), (15, 0.5)],
        harp=[sx(0), sx(1)],
        stac=[(0, 1, "r", 1.0), (2, 1, "r", 0.5), (4, 1, "5", 0.7), (6, 1, "r", 0.5), (8, 1, "8", 0.9), (10, 1, "r", 0.5), (12, 1, "5", 0.7), (14, 1, "r", 0.5)],
        pizz=[(5, 1, "3", 0.6), (13, 1, "3", 0.6)],
        snare=[(8, 1.0), (15, 0.3)], rim=[(4, 0.4), (12, 0.4)], taiko=[0, (6, 0.7), (10, 0.9)],
        tamb=[(i, 0.8 if i % 4 == 0 else 0.4) for i in range(16)],
        pad=0.32, harp_lo=57, harp_hi=81,
    )
    fr1, fr2 = T.window(T.A1, 0, 16), T.window(T.A1, 16, 32)
    o1, o2 = T.window(T.A2_OPEN, 0, 16), T.window(T.A2_OPEN, 16, 32)
    c1, c2 = T.window(T.A2_CLOSE, 0, 16), T.window(T.A2_CLOSE, 16, 32)
    dim = T.diminish(T.THEME_A, 2)

    def peak_dim(song, b0, ch):
        # DIMINUTION: the theme twice as fast, in the peak (trumpet + celesta)
        for k in range(0, 8, 2):
            A.lead(song, 3, "trumpet", dim, ch.key, b0 + k, vel=0.55, pan=0.0, send="hall", wet=0.25, octave=1)
            A.lead(song, 3, "celesta", dim, ch.key, b0 + k, vel=0.40, pan=0.3, send="hall", wet=0.5, octave=2, legato=0.7)

    sections = [
        # a bar for one face, the answer for the other
        dict(key=Aa, chart=[Am, D9, Am, Esus, Am, D9, Am, [(0, D9), (8, Am)]],
             say=[(0, "blue", fr1, 0.72), (1, "red", M(fr1), 0.74), (2, "blue", fr2, 0.72), (3, "red", M(fr2), 0.74),
                  (4, "blue", o1, 0.72), (5, "red", M(o1), 0.74), (6, "blue", c1, 0.72), (7, "red", M(c1, resolve=True), 0.76)]),
        dict(key=Aa, chart=[Am, D9, Fm7, Esus, Am, D9, Am, [(0, D9), (8, Esus)]],
             say=[(0, "red", M(T.THEME_A), 0.74), (4, "blue", T.THEME_A_CLOSED, 0.76)], extra=[peak_dim]),
        dict(key=Aa, chart=[Fb, Gb, Am, E7, Fb, Gb, [(0, T.C(5, "m9")), (8, E7s)], E7],
             say=[(0, "both", T.BRIDGE, 0.70), (4, "both", T.BRIDGE_HIGH, 0.74)], choir=True),
        dict(key=Bk, chart=[T.C(0, "min"), T.C(5, "add9"), T.C(0, "min"), T.C(7, "sus4"), T.C(0, "min"), T.C(5, "add9"), T.C(0, "min"), [(0, T.C(5, "add9")), (8, T.C(5, "7sus4"))]],
             say=[(0, "blue", T.THEME_A, 0.78), (4, "red", M(T.THEME_A_CLOSED, resolve=True), 0.80)], choir=True),
    ]
    # the last bar of the last section leads back to A dorian: E7 (the dominant of A)
    sections[3]["chart"][7] = [(0, T.C(5, "7sus4")), (8, T.C(5, "7"))]  # (in B: the fifth of the new key is F#; E7 is its flat seventh: a pivot back to A)
    return _stage("stage_c", 120, sections, g)




# ===========================================================================================================================================================
# THE BOSS: one fight, three questions (read the colour, read the position, read the time), and ONE theme that is asked in three ways. D aeolian: the sixth falls,
# so the B of the theme becomes a Bb (the shape is the same, the light has gone out of it).
# ===========================================================================================================================================================
Dn = T.Key("D", "aeolian")
En = T.Key("E", "aeolian")   # E4: a step higher (the third phase)
LOW = dict(blue_main="celesta", blue_dbl="glock", red_main="trombone", red_dbl="horn", both_red="cello",
           peak=(("trumpet", 0, 0.9, 0.0), ("horn", -1, 1.0, 0.3), ("trombone", -1, 0.8, -0.3)))


def boss_awaken():
    """PHASE 1, 'read the colour': 112 BPM. The motto, alone and very low, with long silences between: the boss is reading you."""
    Dm, Bbm, A7, Gm, Dsus = T.C(0, "min"), T.C(-4, "maj"), T.C(7, "7"), T.C(5, "min"), T.C(0, "sus4")
    g = dict(
        voices=LOW, bass_inst="bass", bass_vel=0.66,
        bass=[(0, 6, "r", 1.0), (6, 2, "r", 0.5), (8, 5, "r", 0.9), (13, 3, "5", 0.6)],
        kick=[0, (3, 0.75), (8, 0.5)], kick_vel=0.7,
        shaker=[(2, 0.4), (6, 0.4), (10, 0.4), (14, 0.4)],
        wood=[(7, 0.5)],
        harp=[[(0, 4, 0, 1.0), (4, 4, 2, 0.7), (8, 4, 4, 0.8), (12, 4, 6, 0.6)], [(0, 4, 4), (4, 4, 6, 0.7), (8, 4, 2, 0.8), (12, 4, 0, 0.6)]], arp_inst="vibes", harp_vel=0.40, harp_lo=60, harp_hi=84,
        stac=[(0, 1, "r", 1.0), (1, 1, "r", 0.5), (3, 1, "r", 0.6), (6, 1, "r", 0.7), (8, 1, "r", 1.0), (9, 1, "r", 0.5), (11, 1, "r", 0.6), (14, 1, "5", 0.7)], stac_vel=0.58,
        pizz=[(4, 1, "r", 0.7), (12, 1, "r", 0.7)],
        snare=[(12, 1.0)], rim=[(4, 0.5)], taiko=[0, (8, 0.7), (11, 0.6)],
        tamb=[(i, 0.7 if i % 4 == 0 else 0.3) for i in range(0, 16, 2)], pad=0.30,
    )
    fr = T.window(T.A1, 0, 16)
    chart = [Dm, Gm, Dm, A7, Dm, Gm, Dm, [(0, Gm), (8, Dm)]]          # (the harmony the theme was written over: i - iv - i - V)
    sections = [
        dict(key=Dn, chart=chart, say=[(0, "red", fr, 0.62), (2, "red", T.MOTTO, 0.60), (4, "blue", M(fr), 0.6), (6, "red", T.window(T.A1, 16, 32), 0.62)], octave=-1),
        dict(key=Dn, chart=chart, say=[(0, "red", T.THEME_A, 0.70), (4, "red", T.THEME_A_CLOSED, 0.72)], octave=-1),
        dict(key=Dn, chart=chart, say=[(0, "both", T.THEME_A, 0.74), (4, "red", M(T.THEME_A_CLOSED, resolve=True), 0.76)], octave=-1, choir=True),
    ]
    return _stage("boss_awaken", 112, sections, g)


def boss_split():
    """PHASE 2, 'read the position': 144 BPM. The theme and its mirror a bar apart, hard left and right: the sky splits in two (a canon). Under it a hemiola: 3 + 3 + 3 + 3 + 4."""
    Dm, Bbm, A7, Gm, Cm = T.C(0, "min"), T.C(-4, "maj"), T.C(7, "7"), T.C(5, "min"), T.C(-2, "maj")
    g = dict(
        voices=dict(blue_main="flute", blue_dbl="celesta", red_main="trumpet", red_dbl="horn", peak=(("trumpet", 0, 0.9, 0.0), ("flute", 1, 0.7, -0.4), ("horn", -1, 0.9, 0.4))),
        bass=[(0, 3, "r", 1.0), (3, 3, "r", 0.7), (6, 3, "5", 0.8), (9, 3, "r", 0.7), (12, 4, "r", 0.9)],
        kick=[0, 6, 12], kick_vel=0.62,
        shaker=[(i, 0.8 if i in (0, 3, 6, 9, 12) else 0.35) for i in range(16)],
        wood=[(3, 0.5), (9, 0.5)],
        harp=[[(s, 2, (k % 5) + (0 if s < 8 else 1), 1.15 if s in (0, 3, 6, 9, 12) else 0.9) for k, s in enumerate(range(0, 16, 1))]], harp_lo=57, harp_hi=81,
        stac=[(0, 1, "r", 1.0), (3, 1, "r", 0.8), (6, 1, "5", 0.9), (9, 1, "r", 0.8), (12, 1, "5", 0.9)], stac_vel=0.58,
        pizz=[(2, 1, "8", 0.6), (8, 1, "8", 0.6), (14, 1, "3", 0.6)],
        snare=[4, 12], rim=[(7, 0.5), (10, 0.5)], taiko=[0, 6, (12, 0.9)],
        tamb=[(i, 0.8 if i in (0, 3, 6, 9, 12) else 0.3) for i in range(16)],
    )
    chart = [Dm, Gm, Dm, A7, Dm, Gm, Dm, [(0, Gm), (8, Dm)]]
    bridge = [Bbm, Cm, Dm, A7, Bbm, Cm, [(0, Gm), (8, A7)], A7]

    def canon(song, b0, ch):
        # the mirror, one bar after the voice that carries the theme: left and right
        pass

    sections = [
        # blue states the theme, red answers it upside down, one bar later (hard left / right)
        dict(key=Dn, chart=chart, say=[(0, "blue", T.THEME_A, 0.70), (1, "red", M(T.THEME_A), 0.72), (4, "blue", T.THEME_A_CLOSED, 0.72), (5, "red", M(T.THEME_A_CLOSED, resolve=True), 0.74)]),
        dict(key=Dn, chart=chart, say=[(0, "red", T.THEME_A, 0.74), (1, "blue", M(T.THEME_A), 0.72), (4, "red", T.THEME_A_CLOSED, 0.76), (5, "blue", M(T.THEME_A_CLOSED, resolve=True), 0.74)]),
        dict(key=Dn, chart=bridge, say=[(0, "both", T.BRIDGE, 0.72), (4, "both", T.BRIDGE_HIGH, 0.76)], choir=True),
    ]
    return _stage("boss_split", 144, sections, g)


def boss_core():
    """PHASE 3, 'read the time': 168 BPM, E aeolian (a step higher). The theme AUGMENTED in the brass, a choir over it, and fast arpeggios under it."""
    Em, C, B7, Am, D = T.C(0, "min"), T.C(-4, "maj"), T.C(7, "7"), T.C(5, "min"), T.C(-2, "maj")
    A_s = T.C(5, "min")
    g = dict(
        voices=dict(blue_main="flute", blue_dbl="celesta", red_main="horn", red_dbl="trumpet", peak=(("trumpet", 0, 0.9, 0.0), ("horn", -1, 1.0, 0.3), ("trombone", -1, 0.8, -0.3), ("flute", 1, 0.6, -0.3))),
        bass=[(0, 2, "r", 1.0), (2, 2, "r", 0.6), (4, 2, "r", 0.85), (6, 2, "5", 0.6), (8, 2, "r", 1.0), (10, 2, "r", 0.6), (12, 2, "r", 0.85), (14, 2, "8", 0.7)],
        kick=[0, 4, 8, (10, 0.7), 12], kick_vel=0.64,
        shaker=[(i, 0.8 if i % 4 == 0 else 0.45) for i in range(16)],
        wood=[(3, 0.5), (11, 0.5)],
        harp=[[(s, 1, (s % 8) if (s % 8) < 5 else 8 - (s % 8), 1.2 if s % 4 == 0 else 0.9) for s in range(16)]], harp_lo=57, harp_hi=81, harp_vel=0.44,
        stac=[(i, 1, "r" if i % 4 == 0 else ("5" if i % 4 == 2 else "8"), 0.95 if i % 4 == 0 else 0.55) for i in range(16)], stac_vel=0.50,
        pizz=[(3, 1, "8", 0.6), (11, 1, "8", 0.6)],
        snare=[4, 12, (7, 0.4), (15, 0.5)], rim=[(10, 0.5)], taiko=[0, 4, 8, 12], 
        tamb=[(i, 0.8 if i % 4 == 0 else 0.4) for i in range(16)], pad=0.34,
    )
    chart = [Em, Am, Em, B7, Em, Am, Em, [(0, Am), (8, Em)]]
    long_theme = T.augment(T.THEME_A)                        # AUGMENTATION: twice as long, 8 bars
    long_closed = T.augment(T.THEME_A_CLOSED)
    sections = [
        dict(key=En, chart=chart, say=[(0, "red", long_theme, 0.78)], choir=True, octave=0),
        dict(key=En, chart=chart, say=[(0, "red", long_closed, 0.80)], choir=True),
        dict(key=En, chart=chart, say=[(0, "blue", T.THEME_A_CLOSED, 0.78), (4, "red", M(T.THEME_A_CLOSED, resolve=True), 0.82)], choir=True),
    ]
    return _stage("boss_core", 168, sections, g)


# ===========================================================================================================================================================
# THE TITLE: OVERTURE. The theme AUGMENTED (every note twice as long), a piano and an ocarina, wide and sparse. 76 BPM, 16 bars, three layers that all play all the time.
# ===========================================================================================================================================================
def title():
    song = Song("title", 76, 16, 3, polar=2)
    key = D
    Dm, G9a, Asus = T.C(0, "min"), T.C(5, "add9"), T.C(7, "sus4")
    half = [Dm, Dm, G9a, G9a, Dm, Dm, Asus, Asus]
    close = [Dm, Dm, G9a, G9a, Dm, Dm, [(0, G9a), (8, Dm)], Dm]
    ch = A.Chart(key, half + close)
    long_open = T.augment(T.THEME_A)
    long_close = T.augment(T.THEME_A_CLOSED)
    # ---- s0: the ground (a pad, a low cello, a bell where a statement begins)
    pv = A.pads(song, 0, "strings", ch, 0, 16, vel=0.30, lo=52, hi=67, send="hall", wet=0.6, overlap=2.0)
    for b in range(16):
        c = ch.at(b, 0)
        song.add(0, "cello", b * 16, 15, T.bass_note(c, key, 38, 50), 0.46, -0.2, "hall", 0.5, 0.0, 15.0, 0.0)
    for b in (0, 8):
        song.add(0, "tubular", b * 16, 16, key.deg(0) - 12, 0.5, 0.0, "hall", 0.6, 0.0, 0.0, 0.0)
    # ---- s1: blue (a piano says the theme, an ocarina takes it up, the harp breathes under it, the celesta far above)
    A.lead(song, 1, "piano", long_open, key, 0, vel=0.62, pan=-0.1, wet=0.4, shape=A.phrase_shape, octave=0, legato=1.0, jitter=9.0)
    A.lead(song, 1, "piano", long_close, key, 8, vel=0.66, pan=-0.1, wet=0.4, shape=A.phrase_shape, octave=0, legato=1.0, jitter=9.0)
    A.lead(song, 1, "ocarina", long_close, key, 8, vel=0.55, pan=0.25, wet=0.5, shape=A.phrase_shape, octave=1, legato=0.95)
    slow = [(0, 4, 0, 1.0), (4, 4, 2, 0.8), (8, 4, 4, 0.9), (12, 4, 3, 0.7)]
    slow2 = [(0, 4, 4, 1.0), (4, 4, 3, 0.8), (8, 4, 2, 0.9), (12, 4, 1, 0.7)]
    for b in range(16):
        A.arp(song, 1, "harp", ch, b, 1, slow if b % 2 == 0 else slow2, vel=0.34, lo=55, hi=76, wet=0.5, swing_pan=0.5)
    A.pads(song, 1, "vibes", ch, 0, 16, vel=0.14, voicings=pv, pick=(1, 2, 3), shift=12, send="hall", wet=0.8, overlap=0.0)
    for b in range(1, 16, 2):
        c = ch.at(b, 0)
        v = T.voice(c, key, None, 72, 86, 2, omit_root=False)
        song.add(1, "celesta", b * 16 + 10, 4, v[-1], 0.32, 0.4, "hall", 0.7, 0.0, 10.0, 0.0)
    # ---- s2: red (a low horn, a cello that comes in with the mirror of the motto at the very end, a timpani that swells into the next loop)
    A.pads(song, 2, "horn", ch, 0, 16, vel=0.22, voicings=pv, pick=(0, 1), shift=0, send="room", wet=0.35, overlap=1.0, lag=6.0)
    A.lead(song, 2, "cello", long_open, key, 0, vel=0.34, pan=0.2, send="hall", wet=0.4, octave=-1, legato=1.0, shape=A.phrase_shape)
    A.lead(song, 2, "cello", T.augment(M(T.MOTTO)), key, 14, vel=0.50, pan=0.2, send="hall", wet=0.45, octave=0, legato=1.0)
    A.roll(song, 2, "timp", 7, 0, 1, 1, 0.5, 0.5, pitch=38, wet=0.4)
    song.add(2, "timp_roll", 15 * 16, 16, 38, 0.6, 0.0, "hall", 0.4, 0.0, 0.0, 0.0)
    song.add(2, "timp", 0, 1, 38, 0.7, 0.0, "hall", 0.4, 0.0, 0.0, 0.0)
    return song


# ===========================================================================================================================================================
# CLEAR: the motto as a fanfare, then the theme in the major (D ionian: the same notes in the light), a calm return, and the first chord again. 120 BPM, 12 bars.
# ===========================================================================================================================================================
def clear():
    song = Song("clear", 120, 12, 1, polar=0)
    key = T.Key("D", "ionian")
    Dc, Gc, Ac, Bm = T.C(0, "maj"), T.C(5, "add9"), T.C(7, "sus4"), T.C(9, "min")
    ch = A.Chart(key, [Dc, [(0, Dc), (8, Ac)], Dc, Gc, Dc, [(0, Gc), (8, Ac)],
                       Dc, Gc, Dc, [(0, Gc), (8, Dc)], Dc, [(0, Gc), (8, Ac)]])
    pv = A.pads(song, 0, "strings", ch, 0, 12, vel=0.42, lo=52, hi=70, send="hall", wet=0.55, overlap=1.0)
    A.pads(song, 0, "horn", ch, 0, 12, vel=0.30, voicings=pv, pick=(0, 1, 2), shift=-12, send="hall", wet=0.4, overlap=0.5, lag=6.0)
    fanfare = [T.N(0, 2, 4), T.N(3, 1, 4), T.N(4, 5, 7), T.N(10, 2, 7), T.N(12, 4, 9),
               T.N(16, 2, 7), T.N(19, 1, 7), T.N(20, 4, 11), T.N(26, 2, 9), T.N(28, 4, 7)]    # the motto, answered a step higher: a fanfare in two bars
    A.lead(song, 0, "trumpet", fanfare, key, 0, vel=0.82, pan=0.0, wet=0.3, octave=0, shape=A.phrase_shape)
    A.lead(song, 0, "horn", fanfare, key, 0, vel=0.62, pan=0.2, wet=0.3, octave=-1, shape=A.phrase_shape)
    A.lead(song, 0, "flute", fanfare, key, 0, vel=0.5, pan=-0.3, wet=0.4, octave=1)
    song.add(0, "crash", 0, 16, 60, 0.8, 0.0, "hall", 0.3, 0.0, 0.0, 0.0)
    for k in range(4):
        song.add(0, "timp", k * 8, 1, 38 if k % 2 == 0 else 45, 0.8, 0.0, "hall", 0.3, 0.0, 2.0, 0.0)
    A.roll(song, 0, "snare", 1, 8, 8, 1, 0.3, 0.7, wet=0.2)
    A.lead(song, 0, "flute", T.THEME_A, key, 2, vel=0.66, pan=-0.15, wet=0.45, shape=A.phrase_shape, octave=0)
    A.lead(song, 0, "celesta", T.THEME_A, key, 2, vel=0.36, pan=0.25, wet=0.55, octave=1, legato=0.7)
    A.lead(song, 0, "horn", T.THEME_A_CLOSED, key, 6, vel=0.72, pan=0.15, wet=0.3, octave=-1, shape=A.phrase_shape)
    A.lead(song, 0, "trumpet", T.THEME_A_CLOSED, key, 6, vel=0.55, pan=-0.1, wet=0.3, octave=0, shape=A.phrase_shape)
    A.lead(song, 0, "flute", T.THEME_A_CLOSED, key, 6, vel=0.5, pan=-0.3, wet=0.4, octave=1)
    song.add(0, "crash", 6 * 16, 16, 60, 0.6, 0.0, "hall", 0.3, 0.0, 0.0, 0.0)
    for b in range(2, 12):
        A.arp(song, 0, "harp", ch, b, 1, [(0, 3, 0), (3, 3, 1), (6, 3, 2), (9, 3, 3), (12, 4, 4)], vel=0.42, lo=52, hi=76, wet=0.5)
    A.bassline(song, 0, "bass", ch, 0, 12, [(0, 6, "r", 0.9), (8, 6, "r", 0.7)], vel=0.55, wet=0.1)
    # the tag: the motto once more in the celesta, and the harp runs up to the top
    A.lead(song, 0, "celesta", T.MOTTO, key, 10, vel=0.5, pan=0.2, wet=0.6, octave=1)
    for i, d in enumerate(range(0, 14)):
        song.add(0, "harp", 11 * 16 + 4 + i * 0.75, 4, key.deg(d), 0.5, -0.3 + i * 0.05, "hall", 0.5, 0.0, 1.0, 0.0)
    song.add(0, "tubular", 11 * 16 + 8, 8, key.deg(7), 0.4, 0.0, "hall", 0.6, 0.0, 0.0, 0.0)
    return song


# ===========================================================================================================================================================
# GAME OVER: the mirror alone, slow, in the minor. It ends on the fifth: it does not come home.
# ===========================================================================================================================================================
def gameover():
    song = Song("gameover", 70, 10, 1, polar=0)
    key = T.Key("D", "aeolian")
    Dm, Gm, Asus, A7 = T.C(0, "min"), T.C(5, "min"), T.C(7, "sus4"), T.C(7, "7")
    ch = A.Chart(key, [Dm, Dm, Gm, Gm, Dm, Dm, Gm, Asus, Asus, A7])
    A.pads(song, 0, "strings", ch, 0, 10, vel=0.30, lo=50, hi=66, send="hall", wet=0.65, overlap=2.0)
    mir = T.augment(M(T.THEME_A))          # the mirror, twice as slow: eight bars, and it stops on the dominant
    A.lead(song, 0, "piano", mir, key, 0, vel=0.60, pan=-0.05, wet=0.5, shape=A.phrase_shape, octave=0, legato=1.0, jitter=10.0)
    A.lead(song, 0, "cello", mir, key, 0, vel=0.34, pan=0.2, send="hall", wet=0.45, octave=-1, legato=1.0)
    for b in range(10):
        c = ch.at(b, 0)
        song.add(0, "cello", b * 16, 15, T.bass_note(c, key, 38, 50), 0.40, -0.2, "hall", 0.5, 0.0, 15.0, 0.0)
    song.add(0, "timp", 0, 1, 38, 0.6, 0.0, "hall", 0.5, 0.0, 0.0, 0.0)
    song.add(0, "tubular", 0, 16, key.deg(0) - 12, 0.45, 0.0, "hall", 0.6, 0.0, 0.0, 0.0)
    A.pads(song, 0, "choir_u", ch, 4, 6, vel=0.22, lo=55, hi=70, size=3, send="hall", wet=0.7, overlap=1.0)
    return song


TRACKS = {"stage_a": stage_a, "stage_b": stage_b, "stage_c": stage_c, "boss_awaken": boss_awaken, "boss_split": boss_split, "boss_core": boss_core,
          "title": title, "clear": clear, "gameover": gameover}
