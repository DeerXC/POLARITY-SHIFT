# balance_sim

A headless simulator of POLARITY SHIFT. It does **not** re-implement the game: it compiles the real game logic from `src/`
(enemies, waves, the boss, the modules, combat, the player) into a console program. Only the outside world is replaced:

| real game                         | in the simulator                         |
| --------------------------------- | ---------------------------------------- |
| Direct3D renderer, text, audio    | empty stand-ins (`stubs.cpp`)            |
| keyboard / mouse / gamepad        | the keys that the **bot** presses        |
| `PlayScene::Update` + the screens | `PlayStep()` in `sim_main.cpp` (same order of calls, no screens) |

So a number that is tuned with the simulator is the number the game uses (`rogue/Balance.h`, `GameWave.cpp`, `GameBoss.cpp`, ...).

## Build (Linux / WSL / macOS with g++ 11+)

    cd tools/balance_sim
    ./build.sh ../..  ./sim          # <project root> <output>

## Run

    ./sim --runs 200 --seed 1 --skill average --unlock all --draft random --boss 1

| option | meaning |
| ------ | ------- |
| `--skill novice|average|expert` | the bot: how late it reacts, how well it judges, whether it uses the precision mode (SHIFT) |
| `--unlock all|start` | `all`: every module is in the pool; `start`: only the six modules that are free in the hangar (a new player) |
| `--draft random|none` | `random`: the bot picks a random card on every pick screen (the luck of the draft); `none`: no picks, the build is `--mods` |
| `--mods 3,5` | module ids (see `ModuleDefs.h`) that the run starts with (with `--draft none`) |
| `--boss 0|1` | `0`: the run counts as won when the boss appears (waves only) |

One line per run: `seed,skill,result,wave,bossPhase,frames,hp,hits,absorbs,moduleMask` (result 0 died, 1 cleared, 2 time out).
`SIM_DEBUG=1` prints every point of damage to stderr (which attack, which phase, where the ship was).

Helper scripts: `stats.sh` (summary of a binary), `modvalue.py` (clear rate of every single-module build), `boss_hits.py` (HP lost per boss attack),
`variant.py` (apply a list of constant changes to a copy of the project, build it and measure the three skills), `layout_check.cpp` (do the module texts fit the cards, in every language).

## What the bots are (and are not)

They are not people. They are a repeatable yardstick: *novice* looks 12 frames ahead and reacts after 9 frames, *average* looks 16 and reacts after 6,
*expert* looks 22 and reacts after 3. Use the numbers to compare two versions of the game, or two modules, with the same bot - not as a promise of what a
particular player will experience.
