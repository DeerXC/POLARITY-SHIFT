#!/usr/bin/env python3
"""Builds the music of POLARITY SHIFT: bgm6/score.py -> assets/audio/bgm/<track>_s<N>.ogg (and loops.txt).

  python tools/audio/build_bgm.py                     all tracks (about 10 minutes)
  python tools/audio/build_bgm.py stage_a title       only these
  python tools/audio/build_bgm.py --check             the score is analysed (clashes, ranges, levels) and nothing is written
  python tools/audio/build_bgm.py --wav               also write a stereo preview mix of each track (preview_<track>.wav) into --out

numpy, scipy, soundfile. ffmpeg (with libvorbis) is used for the .ogg when it is installed, otherwise soundfile does it.
"""
import argparse
import os
import sys
import time
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bgm6 import render as R  # noqa: E402
from bgm6 import score as SC  # noqa: E402
from bgm6 import check as CK  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*")
    ap.add_argument("--out", default=os.path.join("assets", "audio", "bgm"))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--wav", action="store_true")
    ap.add_argument("--quality", type=int, default=4)
    a = ap.parse_args()
    names = a.tracks or list(SC.TRACKS)
    os.makedirs(a.out, exist_ok=True)
    for name in names:
        t0 = time.time()
        song = SC.TRACKS[name]()
        print(f"== {name}: {song.bars} bars at {song.bpm} BPM, {len(song.events)} notes, {song.frames} frames ({song.frames / R.SR:.1f} s)")
        CK.report(song)
        if a.check:
            continue
        stems = R.render(song, seed=sum(map(ord, name)), log=print)
        stems, gain_db, worst, rms = R.master(song, stems, target_peak_db=-2.5, target_rms_db=-18.0)
        print(f"   master: {gain_db:+.1f} dB (worst peak before {worst:.1f} dBFS, loudest mix {rms:.1f} dB rms)   {time.time() - t0:.0f} s")
        for i, s in enumerate(stems):
            path = os.path.join(a.out, f"{name}.ogg" if song.nstems == 1 else f"{name}_s{i}.ogg")
            frames = R.write_ogg(path, s, a.quality)
            print(f"   {os.path.basename(path)}: {frames} frames, {os.path.getsize(path) / 1e6:.2f} MB")
        if a.wav:
            import soundfile as sf
            mix = sum(g * s for g, s in zip(R.combos(song)[0], stems))
            sf.write(os.path.join(a.out, f"preview_{name}.wav"), (mix / max(1.0, np.abs(mix).max())).T, R.SR, subtype="PCM_16")
    # every track loops from its first frame (the engine reads a line in loops.txt only for a track with an intro)
    if not a.check and not a.tracks:
        with open(os.path.join(a.out, "loops.txt"), "w", newline="\n") as f:
            f.write("# name<TAB>frame the loop starts at. No line = the track loops from its first frame (all of them do).\n")


if __name__ == "__main__":
    main()
