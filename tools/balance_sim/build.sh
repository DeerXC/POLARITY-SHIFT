#!/bin/bash
# build.sh <project root> <output binary>
# Builds the simulator: the REAL game logic of <project root>/src plus the stand-ins of this folder (native/Windows.h, stubs.cpp, sim_main.cpp).
# Linux / WSL / macOS, g++ 11 or newer. The objects are kept next to the binary (<binary>.obj): a second build only compiles what changed
# (a changed header rebuilds everything).
# (The tests are built by tools/Makefile: `make -C tools test`. This script is what variant.py uses to build a copy of the project. Its list of files is the same as LOGIC in
#  tools/Makefile: when a file of the game logic is added or split, change both.)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$1" && pwd)"
OUT="$(readlink -f "$2")"
OBJ="$OUT.obj"
mkdir -p "$OBJ"
SRC="boss/BossDraw.cpp boss/BossMask.cpp boss/GameBoss.cpp core/Collision.cpp core/DebugOptions.cpp core/Loc.cpp game/BeamFx.cpp game/Combat.cpp game/EnemyArt.cpp
game/EnemyDraw.cpp game/GameEffect.cpp game/GameEnemy.cpp game/GamePlayer.cpp game/GamePlayerBullet.cpp game/GameSpecialShot.cpp game/GameState.cpp game/GameVfx.cpp
game/GameWave.cpp game/ScreenShake.cpp rogue/Meta.cpp rogue/Modules.cpp rogue/ModulesData.cpp rogue/ModulesDirector.cpp rogue/ModulesEffects.cpp rogue/ModulesPick.cpp"
export FLAGS="-std=c++20 -O2 -w -I$HERE/native -I$ROOT/src -I$HERE"
export OBJ
NEWEST_HEADER=$(find "$ROOT/src" "$HERE" -name '*.h' -printf '%T@\n' | sort -n | tail -1 | cut -d. -f1)
export NEWEST_HEADER
compile() {
  o="$OBJ/$(echo "$1" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$1" -nt "$o" ] || [ "$(stat -c %Y "$o")" -lt "$NEWEST_HEADER" ]; then g++ $FLAGS -c "$1" -o "$o"; fi
}
export -f compile
printf '%s\n' $(for f in $SRC; do echo "$ROOT/src/$f"; done) "$HERE/stubs.cpp" "$HERE/sim_main.cpp" | xargs -P 8 -I{} bash -c 'compile {}'
g++ "$OBJ"/*.o -o "$OUT"
echo "built $OUT"
