#!/bin/bash
# rival.sh <simulator> <skill> [runs=100]: how long is the mini boss on the field and how much HP does it cost (waves only, no boss)
BIN=$(readlink -f "$1"); SK=$2; N=${3:-100}
TMP=$(mktemp -d)
for s in 1 2 3 4; do mkdir -p "$TMP/$s"; ( cd "$TMP/$s" && "$BIN" --runs $((N/4)) --seed $s --skill $SK --unlock all --draft random --boss 0 > out.csv ) & done
wait
cat "$TMP"/*/out.csv | awk -F, -v sk=$SK '$11>0 {n++; f+=$11; d+=$12; if ($3==0) dead++} END {printf "%-8s rival met in %d runs: on the field %.1f s, cost %.1f HP, the ship died in %d of them\n", sk, n, f/n/60, d/n, dead}'
rm -rf "$TMP"
