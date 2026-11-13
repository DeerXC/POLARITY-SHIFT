#!/usr/bin/env python3
"""Builds the sound effects of POLARITY SHIFT: sfx/ (the recipes) -> assets/audio/se/<name>[_2 ...].wav (16 bit, mono, 44.1 kHz).

  python tools/audio/build_sfx.py                      write the whole pack (about 30 s)
  python tools/audio/build_sfx.py cursor decide        only these
  python tools/audio/build_sfx.py --check              do not write: render, measure, and compare the pack with the table of the engine (GameAudio.cpp, SOUNDS)
  python tools/audio/build_sfx.py --preview PATH.mp3   also write a listening copy (every sound in the order of the game) and PATH_index.txt (where each one starts)

THE LEVELS. The engine plays every effect at gainDb 0: the LEVEL of a sound is in its file. Every class of sound (LEVELS) has a loudness: the A-weighted RMS of the part of
the file that is loud (frames within 25 dB of the loudest), in dBFS. Frequent sounds are quiet, the rare ones big, the alerts are the loudest of the small ones (and they sit in
a band, 1.5 - 5 kHz, where the music is not). A sound that would peak over the ceiling of its class (CEILINGS) is squashed by a soft clip.
numpy, scipy, soundfile (ffmpeg for the preview).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sfx import SFX  # noqa: E402
from sfx.analysis import centroid, gated_level  # noqa: E402
from sfx.dsp import SR  # noqa: E402

LEVELS = {  # dBFS, A-weighted, gated
    "tick": -32.0,       # cursor, menu_move, menu_adjust: heard hundreds of times
    "shot": -31.0,       # the shots of the ship
    "absorb": -29.0,     # the chain
    "hit": -27.0,        # a shot on a light fighter
    "ui": -26.0,         # decide, apply, open, error, hint; the switch of colour; the small effects of the modules and the tactics
    "chime": -26.0,
    "hit_heavy": -25.0,  # elite / armour / glass / blades
    "boss_hit": -24.0,   # the boss; the shield (a knock that says "nothing happened": quieter than a hit that hurts)
    "kill": -23.0,       # a light fighter blows up
    "entrance": -22.0,   # the second rival arrives: heard, not an alarm
    "jingle": -21.0,     # wave clear, unlock, synergy, heal, energy full, the cards
    "damage": -20.0,     # the ship is hurt
    "beam": -19.0,       # the piercing beam
    "big": -19.0,        # elite and boss explosions, the cannon, the laser
    "death": -19.0,
    "alert": -18.0,      # the cues: they must be heard through everything
    "boss_alert": -17.0,
    "boss_big": -17.0,   # the phase, the berserk, the break of an overload
    "boss_final": -16.0,
}
PEAK_TARGET = {"peak": -13.0}   # sounds that are set by their peak, not their loudness (the heartbeat: felt more than heard)
CEILING = -1.0                  # dBFS: the highest peak of a class that is not in CEILINGS
# the highest peak a class may have (dBFS): the small sounds are small in the file too, the big ones may use the whole range
CEILINGS = {"tick": -8.0, "shot": -8.0, "absorb": -8.0, "hit": -6.0, "ui": -6.0, "chime": -6.0, "hit_heavy": -4.0, "boss_hit": -3.0, "kill": -3.0, "entrance": -3.0, "jingle": -3.0}
MAX_SQUASH = 2.0                # the soft clip may see a peak this many times the ceiling
# how far the tail of a sound is kept (dB under its peak): the rest of a tail is under the music and is cut, so a sound that is heard hundreds of times does not occupy a voice
TAIL_DB = {"tick": -40, "shot": -40, "absorb": -40, "hit": -40, "ui": -44, "chime": -46, "hit_heavy": -42, "boss_hit": -44, "kill": -44, "alert": -50, "peak": -50}
DEFAULT_TAIL_DB = -54


def finish(x, cls):
    """trim the silence at both ends, cut the tail at the depth of its class, remove the DC offset, and fade the last 20 ms."""
    x = x - np.mean(x)
    peak = np.abs(x).max()
    if peak <= 0:
        return x
    idx = np.nonzero(np.abs(x) > peak * 10 ** (-60 / 20))[0]
    x = x[idx[0]: idx[-1] + 1]
    idx = np.nonzero(np.abs(x) > peak * 10 ** (TAIL_DB.get(cls, DEFAULT_TAIL_DB) / 20))[0]
    x = x[: idx[-1] + 1]
    fade = min(int(0.02 * SR), len(x) // 3)
    x[-fade:] *= np.linspace(1, 0, fade) ** 2
    x[:2] *= [0.3, 0.7]
    return x


def soft_clip(x, ceiling):
    return ceiling * np.tanh(x / ceiling)


def master(variants, cls):
    """one gain for all the variants of a sound (they keep their balance): the level of the class, unless the soft clip would have to squash a peak more than MAX_SQUASH.
    Returns (the variants, whether any of them is soft-clipped)."""
    if cls in PEAK_TARGET:
        gain = 10 ** (PEAK_TARGET[cls] / 20) / max(np.abs(x).max() for x in variants)
        return [x * gain for x in variants], False
    ceiling = 10 ** (CEILINGS.get(cls, CEILING) / 20)
    gain = min(min(10 ** ((LEVELS[cls] - gated_level(x)) / 20), MAX_SQUASH * ceiling / np.abs(x).max()) for x in variants)
    out = [soft_clip(x * gain, ceiling) if np.abs(x).max() * gain > ceiling * 0.8 else x * gain for x in variants]
    return out, any(np.abs(x).max() * gain > ceiling for x in variants)


def file_name(name, variant):
    return f"{name}.wav" if variant == 0 else f"{name}_{variant + 1}.wav"


def engine_table(root):
    """{file stem: variants} from the SOUNDS table of GameAudio.cpp (None when the sources are not next to the tools)."""
    path = os.path.join(root, "src", "core", "GameAudio.cpp")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        text = f.read()
    pattern = r'SE(?:_DUCK|_CHAIN)?\(\w+,\s*"([^"]+)\.wav",\s*(\d+),\s*([-\d.]+)f,\s*(\d+),\s*([-\d.]+)f,\s*(\d+)'
    return {m.group(1): int(m.group(6)) for m in re.finditer(pattern, text)}


def render_all(names):
    """{(name, variant): (samples, class, soft_clipped)}; the random generator of a variant is seeded by its name, so the pack is the same every time it is built."""
    out = {}
    for name in names:
        recipe, cls, variants = SFX[name]
        raw = []
        for v in range(variants):
            rng = np.random.default_rng(sum(ord(c) * (i + 1) for i, c in enumerate(name)) * 31 + v)
            raw.append(finish(np.asarray(recipe(rng, v), dtype=np.float64), cls))
        done, squashed = master(raw, cls)
        for v, x in enumerate(done):
            out[(name, v)] = (x, cls, squashed)
    return out


def check_against_engine(table, problems):
    mine = {name: SFX[name][2] for name in SFX}
    for stem, variants in table.items():
        if stem not in mine:
            print("MISSING in the pack:", stem)
            problems += 1
        elif mine[stem] != variants:
            print(f"VARIANTS differ: {stem}: engine {variants}, pack {mine[stem]}")
            problems += 1
    for stem in mine:
        if stem not in table:
            print("NOT in the engine table:", stem)
            problems += 1
    print(f"engine table: {len(table)} effects with files, pack: {len(mine)} recipes")
    return problems


def write_preview(path, rendered):
    seq, index, at = [], [], 0.0
    for (name, variant), (x, _cls, _clipped) in rendered.items():
        if variant == 0:
            index.append(f"{int(at // 60)}:{at % 60:05.2f}  {name}")
            gap = np.zeros(int(SR * (0.35 if len(x) < 0.5 * SR else 0.6)))
            seq += [x, gap]
            at += (len(x) + len(gap)) / SR
    with open(path[:-4] + "_index.txt", "w", encoding="utf-8") as f:
        f.write("\n".join(index) + "\n")
    wav = path[:-4] + ".wav"
    sf.write(wav, np.concatenate(seq), SR, subtype="PCM_16")
    if shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-codec:a", "libmp3lame", "-q:a", "3", path], check=True)
        os.remove(wav)
    print("preview:", path if shutil.which("ffmpeg") else wav)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="*")
    parser.add_argument("--out", default=os.path.join("assets", "audio", "se"))
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--preview", default=None)
    args = parser.parse_args()
    unknown = [n for n in args.names if n not in SFX]
    if unknown:
        parser.error("unknown sound(s): " + ", ".join(unknown))
    rendered = render_all(args.names or list(SFX))
    root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    problems = 0
    print(f"{'file':28s} {'ms':>5s} {'peak':>6s} {'level':>6s} {'centroid':>8s}  class")
    for (name, variant), (x, cls, clipped) in rendered.items():
        flag = "  (soft-clipped)" if clipped else ""
        if np.abs(x).max() >= 1.0 or np.isnan(x).any():
            flag += "  CLIPS"
            problems += 1
        print(f"{file_name(name, variant):28s} {len(x) * 1000 / SR:5.0f} {20 * np.log10(np.abs(x).max() + 1e-12):6.1f} {gated_level(x):6.1f} {centroid(x):8.0f}  {cls}{flag}")
    table = engine_table(root)
    if table is not None and not args.names:
        problems = check_against_engine(table, problems)
    if args.check:
        print("problems:", problems)
        return problems
    os.makedirs(args.out, exist_ok=True)
    for (name, variant), (x, _cls, _clipped) in rendered.items():
        sf.write(os.path.join(args.out, file_name(name, variant)), x, SR, subtype="PCM_16")
    print(f"written {len(rendered)} files to {args.out}")
    if args.preview:
        write_preview(args.preview, rendered)
    return problems


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
