#!/usr/bin/env python3
# boss_hits.py <binary> <skill> <runs>: HP lost to the boss by attack and phase
import subprocess, sys, re, collections, tempfile, os
from concurrent.futures import ThreadPoolExecutor
BIN, SK, N = sys.argv[1], sys.argv[2], int(sys.argv[3])
NAMES = ['none','TwinBurst','PolarityFan','ColorLanes','CrossWall','CombSweep','HomingPair','CrossFan','RingBurst','Pincer','TripleLock','DoubleSpiral','PrismRing','RailShot','OverloadBeam','PolarityStorm','NovaRings']
def run(seed):
    d = tempfile.mkdtemp()
    r = subprocess.run([BIN, '--runs', str(N // 4), '--seed', str(seed), '--skill', SK, '--unlock', 'all', '--draft', 'random', '--boss', '1'], cwd=d, capture_output=True, text=True, env=dict(os.environ, SIM_DEBUG='1'))
    return r.stderr, r.stdout
hp = collections.Counter(); hits = collections.Counter(); res = []
with ThreadPoolExecutor(4) as ex:
    for err, out in ex.map(run, (1, 2, 3, 4)):
        for line in err.split('\n'):
            m = re.match(r'atk=(\d+) bossphase=(\d+) f=\d+ hp (\d+)->(-?\d+) wave (\d+)', line)
            if not m or m.group(2) == '0': continue
            d = int(m.group(3)) - max(0, int(m.group(4)))
            key = (int(m.group(2)), NAMES[int(m.group(1))])
            hp[key] += d; hits[key] += 1
        res += [l.split(',') for l in out.strip().split('\n') if l]
n = len(res); cleared = sum(1 for r in res if r[2] == '1'); reached = sum(1 for r in res if r[3] == '5')
print(f'{SK}: {n} runs, {reached} reached the boss, {cleared} cleared; deaths in boss phase 1/2/3: ' + '/'.join(str(sum(1 for r in res if r[2] == "0" and r[3] == "5" and r[4] == str(p))) for p in (1, 2, 3)))
tot = sum(hp.values()) or 1
for (ph, name), v in sorted(hp.items(), key=lambda kv: -kv[1])[:16]:
    print(f'  phase {ph}  {name:14s} {v:6d} HP  ({100*v/tot:4.1f}%)  {hits[(ph, name)]} hits')
