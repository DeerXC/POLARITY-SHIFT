#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""POLARITY SHIFT - cut the fonts down to the characters the game can show (needs:  pip install fonttools).

The game takes every .ttf / .otf in assets/fonts and finds a font by its FAMILY NAME (TextRenderer.cpp: Chakra Petch, Noto Sans SC, M PLUS 1, Smiley Sans,
Dela Gothic One). A CJK font holds tens of thousands of characters; the game shows a few thousand at most (all its text is in assets/lang/*.txt). This keeps only
those characters, and keeps everything else of the font as it is (the family name, the weights, the outlines, the kerning and the OpenType features).

    python tools/fonts/subset_fonts.py                  cut and report into build/fonts_subset/ (the game folder is NOT touched)
    python tools/fonts/subset_fonts.py --install        ... and put the cut fonts into assets/fonts; the FULL fonts are moved to assets_src/fonts_full (your masters: keep them)
    python tools/fonts/subset_fonts.py --extra "ABC你好"  keep these characters too
    python tools/fonts/subset_fonts.py --no-safe-zh     the smallest fonts: ONLY what the text files use (see below)

By default the 3755 most common Chinese characters (GB2312 level 1) are kept as well: a text that you add later and that uses a common character still shows
without running this again. (--no-safe-zh leaves them out: smaller, but every new character needs a new run.)

The cut fonts are made from the FULL fonts: assets_src/fonts_full if it exists (after the first --install), else assets/fonts. RUN THIS AGAIN WHENEVER YOU CHANGE
assets/lang/*.txt: a character that is not in the cut font does not show properly. The report names every character of the text files that a font of its language lacks.

What stays: Basic Latin, Latin-1, general punctuation, CJK punctuation, hiragana, katakana, full-width forms (all cheap) + every character of assets/lang/*.txt
(and of src/ if a text is written in the source) + what you give with --extra / --safe-zh. Fonts under 300 KB are copied as they are.
Licence: the fonts are SIL OFL; cutting a font is a "modified version": read the *OFL.txt files that are in assets/fonts (they stay there) for a Reserved Font Name.
"""
import argparse
import os
import shutil
import sys

try:
    from fontTools import subset
    from fontTools.ttLib import TTFont
except ImportError:
    print("fontTools is missing:  pip install fonttools")
    sys.exit(2)

FONT_EXT = (".ttf", ".otf")
SMALL = 300 * 1024
BASE_RANGES = [
    (0x0020, 0x007E),    # Basic Latin
    (0x00A0, 0x00FF),    # Latin-1
    (0x0100, 0x017F),    # Latin Extended-A
    (0x2000, 0x206F),    # general punctuation
    (0x2190, 0x21FF),    # arrows
    (0x2460, 0x24FF),    # enclosed alphanumerics
    (0x25A0, 0x25FF),    # geometric shapes
    (0x3000, 0x303F),    # CJK punctuation
    (0x3040, 0x30FF),    # hiragana, katakana
    (0xFF00, 0xFFEF),    # full-width forms
]
LANG_FONTS = {          # which fonts show which language file (by the family names the game looks for: TextRenderer.cpp)
    "zh": ("notosanssc", "smileysans"),
    "ja": ("mplus1", "delagothic"),
}


def read_text(path):
    raw = open(path, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    for enc in ("utf-8-sig", "gb18030", "shift_jis"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "replace")


def collect(folder, exts):
    found = {}
    for base, _, names in os.walk(folder):
        for n in names:
            if n.lower().endswith(exts):
                p = os.path.join(base, n)
                found[p] = set(read_text(p))
    return found


def gb2312_level1():
    chars = set()
    for hi in range(0xB0, 0xD8):            # rows 16 - 55
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return chars


def family(font):
    for nid in (16, 1):
        name = font["name"].getDebugName(nid)
        if name:
            return name
    return "?"


def key(name):
    return "".join(c for c in name.lower() if c.isalnum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", default=None, help="folder with the full fonts (default: assets_src/fonts_full if it exists, else assets/fonts)")
    ap.add_argument("--lang", default=os.path.join("assets", "lang"))
    ap.add_argument("--src", default="src")
    ap.add_argument("--out", default=os.path.join("build", "fonts_subset"))
    ap.add_argument("--extra", default="")
    ap.add_argument("--no-safe-zh", action="store_true")
    ap.add_argument("--install", action="store_true")
    a = ap.parse_args()

    live = os.path.join("assets", "fonts")
    masters = os.path.join("assets_src", "fonts_full")
    source = a.fonts or (masters if os.path.isdir(masters) else live)
    if not os.path.isdir(source):
        print("No font folder: %s (run this in the project folder)" % source)
        return 2
    fonts = sorted(n for n in os.listdir(source) if n.lower().endswith(FONT_EXT))
    if not fonts:
        print("No fonts in %s" % source)
        return 2
    if not os.path.isdir(a.lang):
        print("No text files: %s" % a.lang)
        return 2

    # ---- the characters
    per_file = collect(a.lang, (".txt",))
    in_source = set()
    if os.path.isdir(a.src):
        for chars in collect(a.src, (".cpp", ".h")).values():
            in_source |= {c for c in chars if ord(c) > 0x7F}
    wanted = set(chr(c) for lo, hi in BASE_RANGES for c in range(lo, hi + 1))
    for chars in per_file.values():
        wanted |= {c for c in chars if c not in "\r\n\t\ufeff"}
    wanted |= in_source
    wanted |= set(a.extra)
    if not a.no_safe_zh:
        wanted |= gb2312_level1()
    print("characters kept: %d  (from the text files: %d, from the source: %d)" % (len(wanted), len(set().union(*per_file.values())) if per_file else 0, len(in_source)))
    for p, chars in sorted(per_file.items()):
        print("  %-28s %5d different characters" % (os.path.basename(p), len(chars)))

    # ---- cut
    os.makedirs(a.out, exist_ok=True)
    opts = subset.Options()
    opts.layout_features = ["*"]
    opts.name_IDs = ["*"]
    opts.name_languages = ["*"]
    opts.name_legacy = True
    opts.notdef_outline = True
    opts.glyph_names = False
    opts.legacy_kern = True
    opts.hinting = True
    opts.drop_tables += ["DSIG", "FFTM"]
    unicodes = sorted(ord(c) for c in wanted)

    rows = []
    cmaps = {}
    for name in fonts:
        src = os.path.join(source, name)
        dst = os.path.join(a.out, name)
        before = os.path.getsize(src)
        if before < SMALL:
            shutil.copyfile(src, dst)
        else:
            font = subset.load_font(src, opts)
            sub = subset.Subsetter(opts)
            sub.populate(unicodes=unicodes)
            sub.subset(font)
            subset.save_font(font, dst, opts)
            font.close()
        after = os.path.getsize(dst)
        check = TTFont(dst, lazy=True)
        fam, sty = family(check), check["name"].getDebugName(2) or ""
        cmaps[name] = set(check.getBestCmap().keys())
        check.close()
        rows.append((name, fam, sty, before, after))

    print("\n%-26s %-18s %9s %9s" % ("file", "family", "before MB", "after MB"))
    tb = ta = 0
    for name, fam, sty, b, f in rows:
        print("%-26s %-18s %9.2f %9.2f%s" % (name, fam, b / 1e6, f / 1e6, "   (copied: small)" if b < SMALL else ""))
        tb += b
        ta += f
    print("%-26s %-18s %9.2f %9.2f   (%.1f x smaller)" % ("TOTAL", "", tb / 1e6, ta / 1e6, tb / float(max(1, ta))))

    # ---- coverage: the text of each language against the fonts that show it
    print("\nCoverage (a character that a font of the language lacks is drawn by another font or not at all):")
    problems = 0
    for lang, keys in LANG_FONTS.items():
        files = [p for p in per_file if os.path.splitext(os.path.basename(p))[0].lower() == lang]
        if not files:
            continue
        chars = {c for c in per_file[files[0]] if c not in "\r\n\t\ufeff" and ord(c) > 0x7F}
        for name, fam, sty, b, f in rows:
            if key(fam) not in keys and not any(k in key(name) for k in keys):
                continue
            before = TTFont(os.path.join(source, name), lazy=True)
            have_before = set(before.getBestCmap().keys())
            before.close()
            lacking_in_master = sorted(c for c in chars if ord(c) not in have_before)
            lost_by_cut = sorted(c for c in chars if ord(c) in have_before and ord(c) not in cmaps[name])
            tag = "OK"
            if lost_by_cut:
                tag = "PROBLEM: the cut removed %d" % len(lost_by_cut)
                problems += 1
            msg = "  %-3s %-26s %s" % (lang, name, tag)
            if lacking_in_master:
                msg += "   (the full font itself lacks %d of %d: %s)" % (len(lacking_in_master), len(chars), "".join(lacking_in_master[:30]))
            print(msg)
    if problems:
        print("\nPROBLEM: a text character was cut. This should not happen; send me this output.")
        return 1

    if not a.install:
        print("\nNothing was installed (build\\fonts_subset has the cut fonts). To use them:  python tools/fonts/subset_fonts.py --install")
        return 0

    if source == live:
        os.makedirs(masters, exist_ok=True)
        for name in fonts:
            shutil.move(os.path.join(live, name), os.path.join(masters, name))
        print("\nThe full fonts were MOVED to %s: keep them, they are your masters." % masters)
    for name in fonts:
        shutil.copyfile(os.path.join(a.out, name), os.path.join(live, name))
    print("Installed %d fonts into %s. Run the game and look at every screen in all three languages." % (len(fonts), live))
    print("When you change assets\\lang\\*.txt run this script again (with --install).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
