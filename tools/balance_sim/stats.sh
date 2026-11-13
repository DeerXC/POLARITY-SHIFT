#!/bin/bash
# stats.sh <simulator binary> <skill> <runs> [simulator options...]
# runs the simulator in 4 processes (seeds 1..4) and prints one summary line
BIN=$(readlink -f "$1"); SK=$2; N=$3; shift 3
TMP=$(mktemp -d)
for s in 1 2 3 4; do mkdir -p "$TMP/$s"; ( cd "$TMP/$s" && "$BIN" --runs $((N/4)) --seed $s --skill $SK "$@" > out.csv ) & done
wait
cat "$TMP"/*/out.csv | awk -F, -v sk=$SK '{n++; c+=($3==1); d+=($3==0); w[$4]++; ph[$5]++; hp+=$7; hits+=$8; fr+=$6; bf+=$13; nb+=($13>0)} END {printf "%-8s runs %4d  cleared %3.0f%%  died %3.0f%%  hp %3.0f  hits %5.1f  min %4.1f  boss %4.1f s | died in w1:%d w2:%d w3:%d w4:%d boss:%d (boss phases %d/%d/%d)\n", sk, n, 100*c/n, 100*d/n, hp/n, hits/n, fr/n/3600, bf/(nb>0?nb:1)/60, w[1],w[2],w[3],w[4],w[5],ph[1],ph[2],ph[3]}'
rm -rf "$TMP"
