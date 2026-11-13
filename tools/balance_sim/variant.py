#!/usr/bin/env python3
"""
variant.py <project root> <changes.json> [runs=200] [boss=1]

Copies the project to a temporary folder, applies a list of changes to the copy, builds a simulator from it and measures the three bots
(and the two kinds of module pool). The project itself is not touched. Changes (JSON):
    ["const", "NAME", value]               a constexpr number of src/rogue/Balance.h
    ["array", "NAME", [v1, v2, ...]]       a constexpr array of Balance.h
    ["sub", "relative/path.cpp", "regex", "replacement"]   one regex substitution in a file below src/
Example:  [["const", "PLAYER_MAX_HP", 140], ["sub", "boss/GameBoss.cpp", "P1_HP = [0-9.]+f", "P1_HP = 100.0f"]]
"""
import json, os, re, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(sys.argv[1])
CH = json.load(open(sys.argv[2])) if sys.argv[2] != '-' else []
RUNS = sys.argv[3] if len(sys.argv) > 3 else '200'
BOSS = sys.argv[4] if len(sys.argv) > 4 else '1'
tmp = tempfile.mkdtemp(prefix='variant_')
copy = os.path.join(tmp, 'project')
shutil.copytree(os.path.join(ROOT, 'src'), os.path.join(copy, 'src'))
for c in CH:
    if c[0] in ('const', 'array'):
        p = os.path.join(copy, 'src/rogue/Balance.h'); s = open(p).read()
        if c[0] == 'const':
            pat = re.compile(r'(constexpr (?:int|float|bool) ' + re.escape(c[1]) + r' = )([^;]+)(;)'); val = str(c[2])
        else:
            pat = re.compile(r'(constexpr (?:int|float|bool) ' + re.escape(c[1]) + r'\[\d+\] = \{)([^}]*)(\})'); val = ', '.join(str(v) for v in c[2])
        m = pat.search(s)
        if not m: sys.exit(f'not found: {c}')
        open(p, 'w').write(s[:m.start(2)] + val + s[m.end(2):])
    else:
        p = os.path.join(copy, 'src', c[1]); s = open(p).read()
        s2, n = re.subn(c[2], c[3], s, count=1, flags=re.S)
        if n != 1: sys.exit(f'not found: {c[:3]}')
        open(p, 'w').write(s2)
binary = os.path.join(tmp, 'sim')
# build.sh keeps its objects next to itself, so the variant gets a copy of the tool folder
tool = os.path.join(tmp, 'tool'); shutil.copytree(HERE, tool, ignore=shutil.ignore_patterns('obj', 'sim', 'variant_*'))
subprocess.run([os.path.join(tool, 'build.sh'), copy, binary], capture_output=True)
if not os.path.exists(binary): sys.exit('build failed')
for sk, unlock in (('expert', 'all'), ('average', 'all'), ('novice', 'all'), ('average', 'start'), ('novice', 'start')):
    out = subprocess.run([os.path.join(HERE, 'stats.sh'), binary, sk, RUNS, '--unlock', unlock, '--draft', 'random', '--boss', BOSS], capture_output=True, text=True).stdout.strip()
    print(f'({unlock:5s}) {out}', flush=True)
shutil.rmtree(tmp, ignore_errors=True)
