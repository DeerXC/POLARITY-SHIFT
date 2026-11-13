"""Looks at a score before it is rendered: ranges, density, and the clashes between voices (a minor second or a tritone that sounds together for long)."""
import numpy as np
from . import synth as S

PITCHED_LEAD = {"flute", "celesta", "glock", "ocarina", "horn", "trumpet", "piano", "stab", "cello", "trombone", "harp", "vibes", "marimba", "strings", "stac", "pizz", "choir", "choir_o", "choir_u", "bass"}
MELODIC = {"flute", "celesta", "ocarina", "horn", "trumpet", "cello", "trombone", "piano", "stab"}


def report(song):
    ev = [e for e in song.events if e[1] not in S.UNPITCHED]
    per_stem = {}
    for e in song.events:
        per_stem.setdefault(e[0], {}).setdefault(e[1], 0)
        per_stem[e[0]][e[1]] += 1
    for st in sorted(per_stem):
        print(f"   s{st}: " + ", ".join(f"{k} {v}" for k, v in sorted(per_stem[st].items())))
    # clashes between the melodic voices of DIFFERENT stems and between a melodic voice and the pad / arpeggio of any stem: sustained overlaps with a minor 2nd / major 7th / tritone
    mel = [e for e in ev if e[1] in MELODIC and e[3] >= 3]
    bad = {}
    for a in mel:
        a0, a1, am = a[2], a[2] + a[3], a[4]
        for b in ev:
            if b is a or b[1] in ("bass", "timp", "taiko", "kick", "timp_roll"):
                continue
            b0, b1, bm = b[2], b[2] + b[3], b[4]
            ov = min(a1, b1) - max(a0, b0)
            if ov < 2.0:
                continue
            if b[1] in MELODIC and b[2] < a[2]:
                continue                                    # (count a melodic pair once)
            d = int(abs(am - bm))
            # a true minor second / minor ninth is harsh at any length; a major seventh (the 13th over the 7th, the #11 over the maj7: the colour of dorian and lydian) only when it is held
            if d in (1, 13, 25) or (d in (11, 23) and ov >= 7.0):
                bar = int(max(a0, b0) // 16)
                bad.setdefault(bar, []).append((a[1], b[1], int(am), int(bm), round(ov, 1)))
    n = sum(len(v) for v in bad.values())
    print(f"   clashes (a minor second / major seventh sounding together for 2+ steps): {n}")
    for bar in sorted(bad)[:12]:
        print(f"     bar {bar + 1}: " + "; ".join(f"{x[0]} {x[2]} vs {x[1]} {x[3]}" for x in bad[bar][:3]))
