#!/bin/bash
# Builds and runs the engine test. Linux / WSL / macOS; run it from the GAME FOLDER (the one with assets/audio/...): the test reads the real files.
#     bash tools/audio/engine_test/build.sh            (the tests)
#     bash tools/audio/engine_test/build.sh scenario   (also writes scenario.csv: the gains of the buses over 14 s of a boss fight)
D="$(cd "$(dirname "$0")" && pwd)"
R="$D/../../.."
FLAGS="-std=c++17 -O2 -Wno-unknown-pragmas -I$D/mock -I$R/src"
SRC="$R/src/core/GameAudio.cpp $R/src/core/GameSettings.cpp $D/mock/mock_xaudio2.cpp"
g++ $FLAGS "$D/test_engine.cpp" $SRC -o "$D/test_engine" || exit 1
"$D/test_engine" || exit 1
if [ "$1" = "scenario" ]; then
  g++ $FLAGS "$D/scenario.cpp" $SRC -o "$D/scenario" && "$D/scenario" && echo "scenario.csv written"
fi
