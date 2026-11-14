# POLARITY SHIFT — Architecture

C++20 / Direct3D 11 / XAudio2 / Win32. No game engine. The only third-party code in the build is `stb_vorbis` (public domain, Ogg decoding, `src/third_party`); everything else is the Windows SDK.
A 2D horizontal shooter: the ship absorbs bullets of its own polarity (red / blue) and is hurt by the other one.

## Layers

```
main.cpp ─ Game (scene switch, frame loop, fixed 60 Hz step + render interpolation)
│
├─ src/core      platform, rendering and audio (knows nothing about the game)
│   DirectX        device, swap chain (the back buffer is always the size of the screen), the 16:9 game rectangle on any screen shape (GetGameRect), the render size (SetRenderSize)
│   DisplayMath    the arithmetic of the display with no Windows in it: the 16:9 rectangle in any screen, the sizes on offer, the Lanczos scale-down (tested on 19 monitor shapes)
│   Renderer2D     batched sprites / quads / convex polygons, premultiplied alpha, additive draws grouped by texture
│   TextureLoader  PNG -> premultiplied RGBA8 texture with a GPU mip chain (WIC decode, written for this project)
│   PostFx         HDR scene target -> bloom (5-level dual filter) -> grade / lens / danger -> back buffer; fills non-16:9 bars; scales a supersampled picture down
│   TextRenderer   Direct2D / DirectWrite text over the picture
│   DisplaySystem  windowed / full screen, resolutions
│   GameAudio      XAudio2 engine; AudioLogic / AudioHeat / AudioMap are its plain C++ logic (see "Audio")
│   OggDecode      Ogg Vorbis -> PCM, a thin wrapper round stb_vorbis
│   GameInput, GameSettings, GameTimer, Loc (zh / ja / en tables), Collision, DebugOptions (config.ini)
│
├─ src/game      the shooter
│   Game / GameFlow / GameState / GameSave / scene/*   scenes: title, how to play, records, settings, play, pause, results; the save files
│   GamePlayer, GamePlayerBullet, GameSpecialShot       the ship, its shots, the piercing beam
│   GameEnemy (behaviour) + EnemyDraw (drawing) + EnemyArt (animation layer: anchors, wind-up light)
│                                            7 enemy types; the logic and the drawing share EnemyInternal.h
│   GameWave                                 wave script
│   Combat                                   absorb / hurt / kill rules
│   BeamFx                                   every line of light (see "Beams")
│   GameEffect, GameVfx, GameBackground, ScreenShake   effects in the hard-edged style; the sky
│
├─ src/boss      the three-phase boss ("ARBITER"): GameBoss (state machine, attacks, drones, rail gun) + BossDraw (drawing),
│                sharing BossInternal.h; BossMask (hit masks)
│
├─ src/rogue     the roguelite layer (see "The roguelite layer"): modules, picks and routes, the books of what a module did, meta progression, balance tables
│
├─ src/ui        UI: GameHud (battle HUD), GameMenuUi (the menu language "THE SEAM": rows, headers, notices, backdrop),
│                CutIn (WAVE CLEAR / WARNING / BREAK cards), SceneFx (screen transitions), UiKit (virtual 1280 x 720 primitives)
│
└─ src/third_party   stb_vorbis
```

Dependencies point down: `ui` and `game` use `core`; apart from the entry point `main.cpp`, `core` never includes `game`, `ui`, `boss` or `rogue`.

### Logic and drawing are separate files

The two biggest systems, the boss and the enemies, are split in two: `GameBoss.cpp` / `GameEnemy.cpp` hold the behaviour and never draw; `BossDraw.cpp` / `EnemyDraw.cpp` draw and never change the game. What the two halves share (types, numbers, the few pieces of state that the drawing reads) is in `BossInternal.h` / `EnemyInternal.h`. The tests link the drawing too, with a renderer that draws nothing, so a change to either half is covered.

## The roguelite layer (`src/rogue`)

15 modules in three types (absorb, break, survive), upgraded once (level 2), with seven hidden pairs (synergies). The run is five waves and a boss; after every wave the ship picks a card.

**The picks lead.** A build is made of modules that work together, and a player cannot build what is never offered. When a card that completes a synergy with a module the ship owns exists, it is on the pick screen with the chance `Balance::LINK_CHANCE_PERCENT`, and always after the route CALIBRATION. The card says what it links to (`LINKS WITH ECHO`; the name of the synergy once it has been found), so the pick before it was the set-up and this one is the pay-off (`ModulesPick.cpp: PlaceLinkCard`).

**The routes** are trades of what the ship already has, never a new currency:

| after | choice | what it does |
|---|---|---|
| wave 2 | ELITE FLEET | an extra elite in waves 3 and 4; each one that is shot down pays a free upgrade pick |
| wave 2 | CALIBRATION | no extra elites; the picks of waves 3 and 4 always show a linking card; one more reroll |
| wave 3 | NEW MODULE | one pick of three new modules |
| wave 3 | FORGE | two upgrades, no new module |
| wave 3 | WAGER | pay 30 % of the maximum HP: a new module and an upgrade |

Every reward is ONE pick as a base, so none of the three is better than the others in every case (`g_ForgeQueue`: the last picks of a sequence are forges). `tools/balance_sim/run_report_test.cpp` plays every sequence.

**What a module did is booked** (`ModulesStats.cpp`) and shown on the game over and the clear page (`Mod::DrawModuleReport`): damage (shots), energy (units of the bar) and HP, and a count of what it did (arcs, blocks ...). A module is credited with what would NOT have happened without it: what it does itself (an arc of ECHO, the fire of EMBER, the HP that MEND repairs), and, for the modules that only multiply (AFTERGLOW, FOCUS, BREACH, MOMENTUM, TWIN, OVERFLOW), the part of every hit that would be missing without them. Credits of different modules can overlap: they are not a split of one cake. The simulator prints the same numbers (`SIM_STATS_FILE`), which is how a card that does nothing is found.

## Rendering pipeline (one frame)

1. `PostFx::BeginScene` binds the HDR target (R16G16B16A16_FLOAT, the render size: the size of the 16:9 game rectangle, or larger for a supersample).
2. World: background → enemies → boss → effects → enemy bullets → ship, through `Renderer2D` (one draw call per texture / blend run;
   additive draws are order-independent and are grouped by texture: 50–230 batches a frame become a handful).
3. `PostFx::EndScene`: bright-pass → 5 downsamples → 5 upsamples (additive) → composite (exposure, soft shoulder, saturation,
   contrast, tint, vignette, chromatic aberration only as a short kick, grain, danger edge) → back buffer; on a 16:10 / 21:9 screen
   `PSFill` first fills the bars with a dark extension of the picture and the seam hairlines.
4. Text (DirectWrite) on top, at the size of the screen, then Present.

Blending is premultiplied everywhere: `TextureLoader` stores premultiplied texels, `shader_pixel_2d.hlsl` premultiplies the vertex tint, the blend states are `ONE, INV_SRC_ALPHA` (alpha) and `ONE, ONE` (additive).
Shaders are compiled at build time (FxCompile, shader model 5.0) into `.cso` files next to the program: `shader_vertex_2d / shader_pixel_2d` and `postfx_{vs, prefilter, down, up, composite, fill, resample_h, resample_v}` (all from `shaders/postfx.hlsli`).

### Three sizes: output, game, render

- **Output** is the back buffer: the pixels of the window or of the screen. It is never scaled by Windows (a back buffer that is not the size of the window is stretched by the desktop with a plain bilinear filter, which made a 4K picture on a 1440p screen *softer* than a 1440p one: a line one pixel wide came out at a quarter of its brightness). The text is drawn at this size, so it is always sharp.
- **Game** is the largest 16:9 rectangle in the output, in its middle; the rest of a 16:10 or 21:9 screen is filled by `PostFx` (`DisplayMath::GameRect`, shared by the swap chain, the text and the mouse).
- **Render** is the size of the picture of the world. It equals the game rectangle, or is larger (the RESOLUTION setting in full screen, up to twice the screen: a supersample). A larger picture is graded at its size, then scaled down by two Lanczos passes in *linear light* (`PSResampleH`, `PSResampleV`: the graded picture is read as an sRGB texture, the middle target is float, the last pass writes the sRGB encoding with a triangular dither). `tools/balance_sim/display_test.cpp` tests the arithmetic and the filter (`DisplayMath::ResampleAxis` is the CPU twin of the shader).

## Beams (`game/BeamFx`)

The piercing beam of the ship, the beams and the laser of the boss, the rails of the elite sniper, the beam of the tactic CHARGE and the band of fire that EMBER leaves are all drawn by one module. The rule that keeps them readable in a dense fight:

- **The zone is straight and never moves.** A hard core line and two thin rails at the edge of the danger are the hit box; the player reads them.
- **The plasma moves, inside the rails.** A sheath that writhes, two strands (the two polarity colours) braided round the core, pulses that run along it, arcs and sparks. It is all additive and dimmer than a bullet, so a bullet inside a beam is still the brightest thing on the screen.

The look is made of vertex-coloured ribbons and polygons only, with no texture, so it is as sharp as the screen at any resolution.

## Audio

`core/GameAudio.cpp` is an XAudio2 engine; everything that does not need the operating system (curves, fades, ducking, priorities, the music clock, the "heat" of the fight, the colour cross-fade, the limiter) is in plain headers (`AudioLogic.h`, `AudioHeat.h`, `AudioMap.h`) and is unit-tested.

- **Buses.** Effects play into four buses (UI, PLAYER, WORLD, ALERT) with their own ducking: an alert lowers the world for a moment so that it is heard through the music. A voice budget drops the least important sounds first (Critical / High / Normal / Low).
- **Music.** Ogg Vorbis, decoded once into memory. A battle track is layers of one piece: the foundation, a blue face, a red face and a peak. A "heat" director (driven by what happens in the fight, with a floor that depends on the wave and the boss phase) decides which layers play; the blue and red faces are cross-faded by the colour of the ship. Layers come in on the next bar, stingers land on a beat.
- **Care.** A heartbeat and a muffled music at low HP; a look-ahead limiter on the master; recovery when the audio device is lost.
- **The sound itself is generated** by Python scripts (`tools/audio`, see its [README](tools/audio/README.md)): the music (`build_bgm.py`, a score written as code, played by small physical models of instruments) and the effects (`build_sfx.py`, one recipe per sound). One theme (a three-note motto) is transformed for every track, and every tone of every effect is one of D E G A C, the notes that all the keys of the music share, so an effect is never a wrong note. The loudness of every class of sound is measured (A-weighted) and set in the file; `build_sfx.py --check` also compares the pack with the table of the engine.

## Simulation time and headless testing

The game logic runs at a fixed 60 Hz step; rendering interpolates between the last two steps (`GameTimer::Blend`). The logic never reads the clock or the renderer, which is what lets `tools/balance_sim` run the real game code without a window.

## Tools (`tools/`)

`make -C tools test` builds and runs the tests with any C++20 compiler (the GitHub workflow in `.github/workflows` does it on every push).

**Tests and simulation**

| tool | what it does |
|---|---|
| `balance_sim` (`sim_main`) | runs the real game logic with a bot: thousands of runs per minute, CSV of damage / energy / clear times per wave and module. With `SIM_STATS_FILE=stats.csv` it also writes what every module did in every run (the numbers of the game over page) |
| `balance_sim/modvalue.py`, `boss_hits.py`, `variant.py` | analysis on top of the simulator: the clear rate of every single-module build; the HP lost per boss attack; apply a list of constant changes to a copy of the project, build it and measure the three skills of the bot |
| `boss_hit_test`, `boss_p2_test`, `boss_score_test` | the real boss, frozen or driven: every phase can be hurt by plain shots; the drones of phase 2; the score that the boss pays by its bar |
| `step_b_test`, `pick_test`, `upgrade_test`, `module_fx_test` | the waves, the elites and the twin rivals; the module picks; the module upgrades; the state behind the module effects |
| `run_report_test` | the books of the modules (damage, energy, HP: what each one did and what it only made bigger), PULSE on a switch of colour, the cards that lead to a synergy, every route and reward sequence |
| `abandon_test`, `save_test` | a run that is left keeps its score; the real save code against files |
| `display_test` | the display arithmetic on 19 monitor shapes (1366 x 768 to 5120 x 1440, 16:10, 21:9, portrait): the game rectangle, the sizes on offer, the mouse, and the filter of the scale-down |
| `layout_check` | do the texts of the modules fit the cards, in every language |
| `audio/test_audio_logic`, `audio/test_heat_floor` | the plain C++ part of the audio engine |
| `audio/engine_test` | the real `GameAudio.cpp` against a mock of XAudio2 |
| `preview_render` / `preview_enemies` | a software rasterizer for the `Renderer2D` API: renders enemies, menus, effects to images on any machine |

**Generators** (Python; the results are committed, so none of them is needed to build or play)

| tool | what it makes |
|---|---|
| `audio/build_bgm.py`, `audio/build_sfx.py` | the music and the sound effects (see "Audio") |
| `make_galaxy.py` | the Milky Way of the background: a simulated spiral galaxy, ray-marched, drawn in three flat tones (`galaxy.png`) plus its brightest stars (`GalaxyStars.h`) |
| `make_enemy_glow.py` | splits every enemy sprite into a neutral hull and an emissive layer, which the game draws additively in the colour of the enemy (so its plating is red or blue like its bullets and can pulse) |
| `make_wordmark.py` | the letters of the POLARITY SHIFT logo as convex polygons (`WordmarkGlyphs.h`), with a check that no two pieces of a letter overlap |
| `make_icon.py` | the icon of the program (`res/icon.ico`) from the emblem of the game |
| `fonts/subset_fonts.py` | cuts every font down to the characters that the game can show (all its text is in `assets/lang`) |
| `add_pass15_module_icons.py` | **a one-time migration** of the module icon atlas (five new icons, four UI icons moved). It has been run; running it again would damage `module_icons.png` |

The source images of the sprites (the models and their renders) are not in this repository.

## Conventions

- Coordinates: the world is normalized (-1..1, y up, `ASPECT = 9 / 16`); UI is virtual 1280 × 720 px (y down) mapped by `UiKit`.
- Private state lives in an anonymous namespace in the `.cpp`; headers expose functions only. The one exception is what the logic and the drawing of the boss and of the enemies share (`BossInternal.h`, `EnemyInternal.h`).
- Balance numbers and the switches of the game (for example `CHALLENGES_ENABLED`, `PRACTICE_SCORES`) live in `rogue/Balance.h`; asset paths in `core/AssetManager.cpp`; texts in `assets/lang/*.txt`.

## Known issues

- `boss_break_test`: four checks about the ram lane that leans fail, as they did before the code was split in two. It is kept out of `make test` (`make -C tools known-failing` runs it).
- The process is DPI aware for the system, not per monitor: a window dragged to a screen with another scaling is stretched by Windows.
- The sizes of the windowed mode start at 1280 x 720: on a screen with a work area under about 760 lines (a 1366 x 768 laptop) that window does not fit; use full screen there.
- The simulator's bot fires the beam only on a full bar, so it never shows what TWIN is worth, and it dodges white bullets so well that DEFLECT does nothing in six runs of ten: those two cards are judged by the numbers of the report in play, not by the bot.
