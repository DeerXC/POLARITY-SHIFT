#include "rogue/Modules.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "rogue/Balance.h"
#include "game/GameEffect.h"
#include "game/GamePlayer.h"
#include "game/GameState.h"
#include "game/GameWave.h"
#include "ui/GameMenuUi.h"
#include "rogue/Meta.h"
#include "rogue/ModulesInternal.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
// Wave events, the module pick screen and the route screen (logic).
namespace Mod
{
float g_CardAnim[3]{};
float g_BranchAnim[3]{};
float g_RerollAnim = 0.0f;
bool g_RerollFocus = false;
int g_RerollShake = 0;
int g_SlowT = 0;       // frames the pick / route screen has been open (also between two picks of the same wave)
int g_ResumeTimer = 0; // frames left of the speed-up after a pick
float g_StepAccumulator = 0.0f;
Mote g_Motes[MOTE_MAX]{};

void ResetPickUi()
{
    for (float& a : g_CardAnim)
        a = 0.0f;
    for (float& a : g_BranchAnim)
        a = 0.0f;
    g_RerollAnim = 0.0f;
    g_RerollFocus = false;
    g_RerollShake = 0;
    g_SlowT = 0;
    g_ResumeTimer = 0;
    g_StepAccumulator = 0.0f;
    for (Mote& m : g_Motes)
        m = Mote{};
}

namespace
{
float Smooth(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

float TimeScale()
{
    if (ChoiceOpen())
    {
        return 1.0f - Smooth(static_cast<float>(g_SlowT) / static_cast<float>(SLOW_FRAMES));
    }
    if (g_ResumeTimer > 0)
    {
        return Smooth(1.0f - static_cast<float>(g_ResumeTimer) / static_cast<float>(RESUME_FRAMES));
    }
    return 1.0f;
}
}

// Called once per PlayScene update, whatever the speed of the world is.
void UpdateUi()
{
    // energy motes: bullets that were turned into energy fly to the ship
    for (Mote& m : g_Motes)
    {
        if (!m.active) continue;
        if (m.delay > 0)
        {
            --m.delay;
            m.x += m.vx;
            m.y += m.vy;
            m.vx *= 0.92f;
            m.vy *= 0.92f;
            continue;
        }
        m.speed = (std::min)(m.speed + 0.0016f, 0.032f);
        const float dx = GameState::player.x - m.x;
        const float dy = (GameState::player.y - m.y) * ASPECT_Y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.055f)
        {
            m.active = false;
            GainEnergy(m.value > 0 ? m.value : 1);
            continue;
        }
        m.x += dx / length * m.speed;
        m.y += dy / length * m.speed / ASPECT_Y;
    }

    // the blast of the backup core runs on the clock of the screen, not of the world: the light gathers while the world is held, the rings are born when it goes on
    if (g_ReviveTimer > 0)
    {
        --g_ReviveTimer;
        if (REVIVE_FRAMES - g_ReviveTimer == REVIVE_BURST_AT)
        {
            GameEffect::SpawnCrystals(g_ReviveX, g_ReviveY, 18, 0.034f, GameEffect::CrystalTint::Mixed, 0.0f, 3.1416f, 0.012f, 0.034f, 18, 34);
            GameEffect::SpawnCrystals(g_ReviveX, g_ReviveY, 12, 0.026f, GameEffect::CrystalTint::White, 1.5708f, 3.1416f, 0.006f, 0.020f, 14, 26);
        }
    }

    if (ChoiceOpen())
    {
        if (g_SlowT < 1000) ++g_SlowT;
        g_ResumeTimer = 0;
    }
    else if (g_SlowT > 0)
    {
        g_SlowT = 0;
        g_ResumeTimer = RESUME_FRAMES;
    }
    else if (g_ResumeTimer > 0)
    {
        --g_ResumeTimer;
    }
}

// The world follows the time scale: it is slowed down (not frozen at once) when a pick screen opens and speeds up again
// after the pick. Returns true when the world should run this update step.
bool StepWorld()
{
    g_StepAccumulator += TimeScale();
    if (g_StepAccumulator >= 1.0f)
    {
        g_StepAccumulator -= 1.0f;
        return true;
    }
    return false;
}

// the bullets that are still on the screen when a wave is cleared turn into energy
void ConvertBulletsToMotes()
{
    int made = 0;
    for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
    {
        EnemyBullet& bullet = GameState::enemyBullets[i];
        if (!bullet.active) continue;
        bullet.active = false;
        g_PrevBulletActive[i] = false;
        if (made >= MOTE_MAX) continue;
        for (Mote& m : g_Motes)
        {
            if (m.active) continue;
            m = Mote{};
            m.active = true;
            m.x = bullet.x;
            m.y = bullet.y;
            const float a = static_cast<float>(std::rand() % 628) / 100.0f;
            m.vx = std::cos(a) * 0.006f;
            m.vy = std::sin(a) * 0.006f / ASPECT_Y;
            m.delay = 8 + std::rand() % 10;
            m.polarity = bullet.polarity;
            ++made;
            break;
        }
    }
}

// ---------------------------------------------------------------- wave events + pick screen
// Does picking this module complete a synergy with a module that the ship owns? (Meta::ComboCompletedBy)
static bool LinksToOwned(int module)
{
    return Meta::ComboCompletedBy(g_Owned, module) >= 0;
}

// The picks of a run are not independent draws: a build is made of modules that work together, and the player cannot build what is never offered. So the cards LEAD: when a card
// that links to a module of the ship exists (it completes a synergy), it is on the screen with the chance Balance::LINK_CHANCE_PERCENT - always after the route CALIBRATION
// in waves 3 and 4. The card says what it links to (DrawCard), so the pick before it was the setup and this one is the payoff. Never three of the same type on one screen.
static void PlaceLinkCard(int pool[], int poolCount)
{
    if (g_ChoiceCount < 2) return;
    const int wave = GameWave::GetCurrentWave();
    const bool calibrated = g_CalibrateOn && (wave == ROUTE_WAVE || wave == ROUTE_WAVE + 1 || g_RouteStage == 2);
    if (!calibrated && std::rand() % 100 >= Balance::LINK_CHANCE_PERCENT) return;
    for (int i = 0; i < g_ChoiceCount; ++i)
        if (LinksToOwned(pool[i])) return; // already there
    int link = -1;
    for (int i = g_ChoiceCount; i < poolCount && link < 0; ++i) // the pool is shuffled: the first one that links is a random one
        if (LinksToOwned(pool[i])) link = i;
    if (link < 0) return;
    // it takes the card of a verb that is not alone on the screen if there is one, so that the three do not become the same type
    int slot = std::rand() % g_ChoiceCount;
    for (int attempt = 0; attempt < g_ChoiceCount; ++attempt)
    {
        const int candidate = (slot + attempt) % g_ChoiceCount;
        int same = 0;
        for (int k = 0; k < g_ChoiceCount; ++k)
            if (INFO[pool[k]].verb == INFO[pool[candidate]].verb) ++same;
        if (same > 1 || attempt == g_ChoiceCount - 1)
        {
            slot = candidate;
            break;
        }
    }
    std::swap(pool[slot], pool[link]);
}

void RollChoices()
{
    int pool[COUNT];
    int poolCount = 0;
    int upgrades[COUNT];
    int upgradeCount = 0;
    for (int i = 0; i < COUNT; ++i)
    {
        if (!g_Owned[i] && Meta::IsUnlocked(i))
            pool[poolCount++] = i;
        else if (CanUpgrade(i))
            upgrades[upgradeCount++] = i;
    }
    for (bool& flag : g_ChoiceUpgrade)
        flag = false;
    if (g_ForgeNext)
    {
        // the forge: the cards are upgrades of modules the ship owns
        for (int i = upgradeCount - 1; i > 0; --i)
            std::swap(upgrades[i], upgrades[std::rand() % (i + 1)]);
        g_ChoiceCount = upgradeCount < 3 ? upgradeCount : 3;
        for (int i = 0; i < g_ChoiceCount; ++i)
        {
            g_Choice[i] = upgrades[i];
            g_ChoiceUpgrade[i] = true;
        }
        return;
    }
    g_ChoiceCount = poolCount < 3 ? poolCount : 3;
    // a reroll does not deal the cards that were on offer a moment ago again (unless the pool is too small to avoid it)
    int fresh[COUNT];
    int freshCount = 0;
    int stale[COUNT];
    int staleCount = 0;
    for (int i = 0; i < poolCount; ++i)
    {
        bool offered = false;
        if (g_Rerolling)
        {
            for (int k = 0; k < g_PrevChoiceCount; ++k)
                if (g_PrevChoice[k] == pool[i]) offered = true;
        }
        if (offered)
            stale[staleCount++] = pool[i];
        else
            fresh[freshCount++] = pool[i];
    }
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        for (int i = freshCount - 1; i > 0; --i)
            std::swap(fresh[i], fresh[std::rand() % (i + 1)]);
        for (int i = staleCount - 1; i > 0; --i)
            std::swap(stale[i], stale[std::rand() % (i + 1)]);
        for (int i = 0; i < freshCount; ++i)
            pool[i] = fresh[i];
        for (int i = 0; i < staleCount; ++i)
            pool[freshCount + i] = stale[i];
        PlaceLinkCard(pool, poolCount);
        if (g_ChoiceCount < 3) break;
        const Verb a = INFO[pool[0]].verb, b = INFO[pool[1]].verb, c = INFO[pool[2]].verb;
        if (!(a == b && b == c)) break; // never offer three of the same verb
    }
    for (int i = 0; i < g_ChoiceCount; ++i)
        g_Choice[i] = pool[i];
    // a module the ship already owns comes again as an UPGRADE: it takes a card about every other pick (the last card that is not the link), or a free card. (The elite fleet pays
    // its upgrades as picks of their own now, see UpdateWaveEvents.)
    if (upgradeCount > 0 && (g_ChoiceCount < 3 || std::rand() % 100 < 45))
    {
        int slot = g_ChoiceCount < 3 ? g_ChoiceCount : 2;
        if (g_ChoiceCount >= 3)
            for (int k = 2; k >= 0; --k)
                if (!LinksToOwned(g_Choice[k]))
                {
                    slot = k;
                    break;
                }
        if (slot >= g_ChoiceCount) g_ChoiceCount = slot + 1;
        g_Choice[slot] = upgrades[std::rand() % upgradeCount];
        g_ChoiceUpgrade[slot] = true;
    }
}
void OpenChoice()
{
    RollChoices();
    if (g_ChoiceCount <= 0)
    {
        g_ChoiceDone = true;
        g_ForgeNext = false;
        return;
    }
    g_ChoiceOpen = true;
    g_ChoiceSel = -1; // nothing is highlighted until the player moves the mouse or presses a key
    for (float& a : g_CardAnim)
        a = 0.0f;
    g_ChoiceTimer = 0;
    g_RerollFocus = false;
    g_RerollAnim = 0.0f;
    GameMenuUi::SetBackdropPolarity(GameState::player.polarity == Polarity::Red, true);
}
int WagerCost()
{
    return MaxHp() * Balance::WAGER_COST_PERCENT / 100;
}
bool RouteLocked(int kind)
{
    switch (kind)
    {
    case ROUTE_FORGE:
        return !AnyUpgradable();
    case ROUTE_WAGER:
        return !AnyUpgradable() || GameState::playerHp <= WagerCost() + 10;
    default:
        return false;
    }
}
// Route screens. After wave 2, the way of waves 3 and 4:
//   ELITE FLEET  an extra elite in each of the two waves; every one of them that is shot down pays a free UPGRADE pick (risk for power)
//   CALIBRATION  no extra elites; the picks of waves 3 and 4 always show a card that links to a module the ship owns, and there is one more reroll (a safe way to build a synergy)
// After wave 3, what the reward is. They all give ONE pick as a base, so that none of them is better than the others in every case:
//   NEW MODULE   one pick of three new modules (a new effect, resonance with its type, a chance at a synergy)
//   FORGE        two upgrades: the build that is there gets much stronger (and has no drawback any more)
//   WAGER        pay 30 % of the maximum HP: a new module AND an upgrade
// No new currency: a route trades what the ship already has (HP, a module slot, a risk). (Before, the elite fleet and the supply run both gave two picks, so NEW MODULE was a module
// and a pick, a WAGER the same for HP and a FORGE two picks of upgrades: the three were not a choice, and the left card was always taken.)
void OpenBranch(int stage)
{
    g_RouteStage = stage;
    if (stage == 1)
    {
        g_RouteKind[0] = ROUTE_ELITE;
        g_RouteKind[1] = ROUTE_CALIBRATE;
        g_RouteCount = 2;
    }
    else
    {
        g_RouteKind[0] = ROUTE_MODULE;
        g_RouteKind[1] = ROUTE_FORGE;
        g_RouteKind[2] = ROUTE_WAGER;
        g_RouteCount = 3;
    }
    g_BranchOpen = true;
    g_BranchSel = -1;
    for (float& a : g_BranchAnim)
        a = 0.0f;
    g_BranchTimer = 0;
    GameMenuUi::SetBackdropPolarity(GameState::player.polarity == Polarity::Red, true);
    g_BranchMessage = 0;
}
void UpdateWaveEvents()
{
    const bool starting = GameWave::IsWaveStarting();
    const bool cleared = GameWave::IsWaveCleared();
    // the pick at the start of the run: it opens as soon as the ship has flown in (not in boss practice, where the modules of the last run are used)
    if (!g_StartPickDone && g_PracticePhase <= 0 && GameWave::GetCurrentWave() == 1 && !g_ChoiceOpen && !g_BranchOpen && !GamePlayer::IsIntro() && GameState::playerHp > 0)
    {
        g_StartPickDone = true;
        g_ChoiceDone = false;
        g_PicksLeft = 1;
        OpenChoice();
    }
    if (starting && !g_PrevStarting)
    {
        g_WaveStartFrame = g_RunFrames;
        g_PlayFrames = 0;
        g_ElitesSpawned = 0;
        g_DirectorElitesDown = 0;
        OnPhaseStart(false);
        g_WaveSpawned = 0;
        g_WaveKilled = 0;
        LogEvent("wave_start", "");
        if (GameWave::GetCurrentWave() == 1) // what the run was started with (the log says it once: the tactic and the levels of the talents)
        {
            std::string loadout = std::string("tactic=") + (g_Tactic >= 0 ? TACTICS[g_Tactic].code : "none");
            for (int i = 0; i < TALENT_COUNT; ++i)
                loadout += std::string(";") + TALENTS[i].code + "=" + std::to_string(Meta::TalentLevel(static_cast<Talent>(i)));
            LogEvent("loadout", loadout.c_str());
        }
    }
    if (cleared && !g_PrevCleared)
    {
        const int wave = GameWave::GetCurrentWave();
        g_ChoiceDone = (wave == ROUTE2_WAVE); // after wave 3 the reward is chosen on the route screen
        ++g_WavesCleared;
        ConvertBulletsToMotes();
        g_PicksLeft = 1;
        g_ForgeQueue = 0;
        // the elite fleet pays an UPGRADE for the extra elite of waves 3 and 4 that was shot down: after wave 4 it is a pick of its own (a module, then a forge); after wave 3 it
        // is added to the reward that the player chooses on the next screen (ConfirmBranch)
        g_EliteBonus = g_Branch == 1 && (wave == ROUTE_WAVE || wave == ROUTE_WAVE + 1) && g_DirectorElitesDown > 0;
        if (g_EliteBonus && wave != ROUTE2_WAVE)
        {
            g_PicksLeft = 2;
            g_ForgeQueue = 1;
            LogEvent("elite_reward", "upgrade");
        }
        // every cleared wave repairs the ship by how much of it was shot down (the ships that flew away do not count as killed)
        g_LastClearPercent = g_WaveSpawned > 0 ? (std::min)(100, g_WaveKilled * 100 / g_WaveSpawned) : 100;
        const int heal = g_LastClearPercent >= 100 ? Balance::CLEAR_HEAL_FULL
                         : (g_LastClearPercent >= 80 ? Balance::CLEAR_HEAL_MOST : (g_LastClearPercent >= 50 ? Balance::CLEAR_HEAL_HALF : 0));
        const int hpBefore = GameState::playerHp;
        if (heal > 0 && GameState::playerHp > 0 && GameState::playerHp < MaxHp()) HealPlayer(heal);
        g_LastClearHeal = GameState::playerHp - hpBefore;
        LogEvent("wave_clear", std::to_string(g_LastClearPercent).c_str());
        GameAudio::Play(GameSound::WaveClear); // (a stinger: it waits for the next beat of the music, 1/3 s at most)
    }
    if (cleared && !g_ChoiceDone && !g_ChoiceOpen && GameState::playerHp > 0 && GameWave::GetTransitionTimer() <= CHOICE_OPEN_AT_TIMER)
    {
        OpenChoice();
    }
    // after the picks of wave 2: choose the route of wave 3
    if (cleared && g_ChoiceDone && !g_ChoiceOpen && !g_BranchOpen && !g_BranchDone && GameState::playerHp > 0
        && GameWave::GetCurrentWave() == ROUTE_WAVE - 1 && GameWave::GetTransitionTimer() <= CHOICE_OPEN_AT_TIMER)
    {
        OpenBranch(1);
    }
    // after wave 3: the reward (no regular pick comes first)
    if (cleared && g_ChoiceDone && !g_ChoiceOpen && !g_BranchOpen && !g_Branch2Done && GameState::playerHp > 0
        && GameWave::GetCurrentWave() == ROUTE2_WAVE && GameWave::GetTransitionTimer() <= CHOICE_OPEN_AT_TIMER)
    {
        OpenBranch(2);
    }
    g_PrevStarting = starting;
    g_PrevCleared = cleared;
}
bool MouseOnReroll()
{
    if (!GameInput::IsMouseInsideWindow()) return false;
    const float px = (GameInput::GetMouseX() + 1.0f) * 640.0f;
    const float py = (1.0f - GameInput::GetMouseY()) * 360.0f;
    return px >= 640.0f - REROLL_W * 0.5f && px <= 640.0f + REROLL_W * 0.5f && py >= REROLL_Y - 6.0f && py <= REROLL_Y + REROLL_H + 6.0f;
}

void CardPixels(int index, int count, float& x, float& y, float& w, float& h)
{
    w = count <= 2 ? 360.0f : 296.0f;
    h = CARD_H;
    const float gap = count <= 2 ? 44.0f : 36.0f;
    const float total = static_cast<float>(count) * w + static_cast<float>(count - 1) * gap;
    x = 640.0f - total * 0.5f + static_cast<float>(index) * (w + gap);
    y = CARD_Y;
}

int HoveredCard(int count)
{
    if (!GameInput::IsMouseInsideWindow()) return -1;
    const float px = (GameInput::GetMouseX() + 1.0f) * 640.0f;
    const float py = (1.0f - GameInput::GetMouseY()) * 360.0f;
    for (int i = 0; i < count; ++i)
    {
        float x, y, w, h;
        CardPixels(i, count, x, y, w, h);
        if (px >= x && px <= x + w && py >= y && py <= y + h) return i;
    }
    return -1;
}

void ConfirmChoice(int slot)
{
    if (slot < 0 || slot >= g_ChoiceCount) return;
    const int index = g_Choice[slot];
    if (g_ChoiceUpgrade[slot])
    {
        UpgradeModule(static_cast<Id>(index));
        LogEvent("module_upgrade", INFO[index].code);
    }
    else
    {
        Grant(static_cast<Id>(index));
        LogEvent("module_pick", INFO[index].code);
    }
    // synergies completed by this pick are discovered (and saved) here, not when a debug config grants modules
    int found[Meta::COMBO_COUNT];
    const int newCombos = Meta::DiscoverNew(g_Owned, found, Meta::COMBO_COUNT);
    if (newCombos > 0)
    {
        g_ToastCombo = found[0];
        g_ToastTimer = 260;
        LogEvent("synergy", Meta::COMBOS[found[0]].code);
        GameAudio::Play(GameSound::SynergyFound);
    }
    g_ForgeNext = false;
    g_ChoiceOpen = false;
    g_ChoiceDone = true;
    GameAudio::Play(GameSound::MenuDecide);
    if (--g_PicksLeft > 0) // another pick follows (the forge, the wager, an elite upgrade)
    {
        g_ChoiceDone = false;
        g_ForgeNext = g_PicksLeft <= g_ForgeQueue; // the last picks of the queue are forges
        OpenChoice();
    }
}
void ConfirmBranch(int slot)
{
    if (slot < 0 || slot >= g_RouteCount) return;
    const int kind = g_RouteKind[slot];
    if (RouteLocked(kind)) // nothing to upgrade / not enough HP
    {
        g_BranchMessage = 70;
        GameAudio::Play(GameSound::MenuError);
        return;
    }
    g_BranchOpen = false;
    if (g_RouteStage == 1)
    {
        g_Branch = (kind == ROUTE_ELITE) ? 1 : 2;
        g_BranchDone = true;
        if (kind == ROUTE_CALIBRATE)
        {
            g_CalibrateOn = true;
            g_RerollsLeft += Balance::CALIBRATE_REROLLS;
        }
        LogEvent("route", kind == ROUTE_ELITE ? "elite" : "calibration");
    }
    else
    {
        g_Branch2Done = true;
        g_ChoiceDone = false;
        // the picks of the reward: ONE as the base (a module), the forge makes it two upgrades, the wager a module and an upgrade; and the elite fleet adds an upgrade for the elite
        // that was shot down
        g_PicksLeft = 1;
        g_ForgeQueue = 0;
        if (kind == ROUTE_FORGE)
        {
            g_PicksLeft = 2;
            g_ForgeQueue = 2; // two upgrades, no new module (with nothing left to upgrade the sequence simply ends)
        }
        else if (kind == ROUTE_WAGER)
        {
            GameState::playerHp -= WagerCost();
            g_PicksLeft = 2;
            g_ForgeQueue = 1;
        }
        if (g_EliteBonus)
        {
            ++g_PicksLeft;
            ++g_ForgeQueue;
            LogEvent("elite_reward", "upgrade");
        }
        g_ForgeNext = g_PicksLeft <= g_ForgeQueue;
        LogEvent("route2", kind == ROUTE_MODULE ? "module" : (kind == ROUTE_FORGE ? "forge" : "wager"));
        OpenChoice();
    }
    GameAudio::Play(GameSound::MenuDecide);
}
void UpdateBranch()
{
    ++g_BranchTimer;
    if (g_BranchMessage > 0) --g_BranchMessage;
    int activated = GameMenuUi::UpdateMenu(g_BranchSel, g_RouteCount, HoveredCard(g_RouteCount), true, GameSound::MenuCursor);
    for (int i = 0; i < 3; ++i)
    {
        const float target = (i == g_BranchSel) ? 1.0f : 0.0f;
        g_BranchAnim[i] += (target - g_BranchAnim[i]) * 0.35f;
    }
    if (g_BranchTimer <= CHOICE_INPUT_LOCK) return;
    for (int i = 0; i < g_RouteCount; ++i)
    {
        if (GameInput::IsTriggered('1' + i)) activated = i;
    }
    if (activated >= 0)
    {
        ConfirmBranch(activated);
    }
}

void UpdateChoice()
{
    if (g_BranchOpen)
    {
        UpdateBranch();
        return;
    }
    ++g_ChoiceTimer;
    const int count = g_ChoiceCount;
    if (count <= 0)
    {
        g_ChoiceOpen = false;
        g_ChoiceDone = true;
        return;
    }
    // The reroll button sits under the cards. The mouse highlights it by pointing at it; with the keyboard or the pad DOWN moves the highlight onto it
    // and UP / LEFT / RIGHT take it back to the cards.
    const bool mouseOnButton = MouseOnReroll();
    const bool unlocked = g_ChoiceTimer > CHOICE_INPUT_LOCK;
    if (GameInput::MouseMoved()) g_RerollFocus = false;
    bool skipMenu = false;
    if (unlocked)
    {
        const bool down = GameInput::IsTriggered(VK_DOWN) || GameInput::IsTriggered('S');
        const bool away = GameInput::IsTriggered(VK_UP) || GameInput::IsTriggered('W') || GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A')
                          || GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D');
        if (!g_RerollFocus && down && g_RerollsLeft > 0)
        {
            g_RerollFocus = true;
            g_ChoiceSel = -1;
            GameAudio::Play(GameSound::MenuCursor);
        }
        else if (g_RerollFocus && away)
        {
            g_RerollFocus = false;
            g_ChoiceSel = count / 2;
            skipMenu = true; // this key press has moved the highlight already
            GameAudio::Play(GameSound::MenuCursor);
        }
    }
    int activated = -1;
    if (!g_RerollFocus && !skipMenu) activated = GameMenuUi::UpdateMenu(g_ChoiceSel, count, HoveredCard(count), true, GameSound::MenuCursor);
    for (int i = 0; i < 3; ++i)
    {
        const float target = (i == g_ChoiceSel) ? 1.0f : 0.0f;
        g_CardAnim[i] += (target - g_CardAnim[i]) * 0.35f;
    }
    const float hotTarget = ((g_RerollFocus || mouseOnButton) && g_RerollsLeft > 0) ? 1.0f : 0.0f;
    g_RerollAnim += (hotTarget - g_RerollAnim) * 0.35f;
    if (g_RerollShake > 0) --g_RerollShake;
    if (!unlocked) return; // the cards are still coming in
    // The reroll of the run (one per run, Balance::REROLLS_PER_RUN) deals the cards again; the cards that were on offer do not come back.
    // R, a click on the button, or ENTER while the button is highlighted. With none left the button shakes and says why.
    const bool wantReroll = GameInput::IsTriggered('R') || (GameInput::IsTriggered(VK_LBUTTON) && mouseOnButton)
                            || (g_RerollFocus && (GameInput::IsTriggered(VK_RETURN) || GameInput::IsTriggered(VK_SPACE)));
    if (wantReroll)
    {
        if (g_RerollsLeft > 0)
        {
            --g_RerollsLeft;
            for (int i = 0; i < g_ChoiceCount; ++i)
                g_PrevChoice[i] = g_Choice[i];
            g_PrevChoiceCount = g_ChoiceCount;
            g_Rerolling = true;
            RollChoices();
            g_Rerolling = false;
            g_ChoiceSel = -1;
            g_ChoiceTimer = 0;
            g_RerollFocus = false;
            for (float& a : g_CardAnim)
                a = 0.0f;
            GameAudio::Play(GameSound::MenuOpen);
            LogEvent("reroll", "");
        }
        else
        {
            g_RerollShake = 16;
            GameAudio::Play(GameSound::MenuError);
        }
        return;
    }
    for (int i = 0; i < count; ++i)
    {
        if (GameInput::IsTriggered('1' + i)) activated = i;
    }
    if (activated >= 0)
    {
        ConfirmChoice(activated);
    }
}

}




