#!/usr/bin/env python3
# modvalue.py <binary> <skill> <runs per build> [boss 0|1]: clear rate of every single-module build against the build without modules
import subprocess, sys, os, tempfile
from concurrent.futures import ThreadPoolExecutor
BIN, SKILL, RUNS = sys.argv[1], sys.argv[2], int(sys.argv[3])
BOSS = sys.argv[4] if len(sys.argv) > 4 else '0'
NAMES = ['afterimage','overdrive','phase_shift','siphon','echo','reactor','wide_beam','ember','purge','backup_core','prism','tide','leech','pulse','lance','overflow','momentum','breach','aegis','mend']
builds = [('(none)', '')] + [(n, str(i)) for i, n in enumerate(NAMES)]
extra = sys.argv[5:] 
def run(args):
    name, mods, seed = args
    d = tempfile.mkdtemp(dir='/tmp') if os.path.isdir('/tmp') else tempfile.mkdtemp()
    cmd = [BIN, '--runs', str(RUNS // 4), '--seed', str(seed), '--skill', SKILL, '--unlock', 'all', '--draft', 'none', '--boss', BOSS] + (['--mods', mods] if mods else []) + extra
    out = subprocess.run(cmd, cwd=d, capture_output=True, text=True).stdout
    rows = [l.split(',') for l in out.strip().split('\n') if l]
    return name, rows
jobs = [(n, m, s) for n, m in builds for s in (1, 2, 3, 4)]
res = {}
with ThreadPoolExecutor(max_workers=4) as ex:
    for name, rows in ex.map(run, jobs):
        res.setdefault(name, []).extend(rows)
base = None
print(f'{"module":14s} {"clear":>6s} {"delta":>7s} {"hp left":>8s} {"hits":>6s} {"absorbs":>8s}   ({SKILL}, {RUNS} runs per build, boss={BOSS})')
for name, _ in builds:
    rows = res[name]; n = len(rows)
    clear = sum(1 for r in rows if r[2] == '1') / n * 100
    hp = sum(int(r[6]) for r in rows) / n; hits = sum(int(r[7]) for r in rows) / n; ab = sum(int(r[8]) for r in rows) / n
    if base is None: base = clear
    print(f'{name:14s} {clear:5.1f}% {clear - base:+6.1f}% {hp:8.1f} {hits:6.1f} {ab:8.1f}')
