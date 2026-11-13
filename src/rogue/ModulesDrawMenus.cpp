#include "boss/GameBoss.h"
#include "core/GameInput.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/GameBackground.h"
#include "game/GameSave.h"
#include "game/GameState.h"
#include "game/scene/TitleScene.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "rogue/ModulesDrawInternal.h"
#include "rogue/ModulesInternal.h"
#include "ui/GameHud.h"
#include "ui/GameMenuUi.h"
#include "ui/UiKit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// Drawing of the screens around a run: result card, game over summary, mode selector and hangar on the title screen.
namespace Mod
{
// what a hangar id is: a module, a tactic or a talent (name, the "+" lines, the icon, the colour)
struct HangarEntry
{
    const char* name;
    const char* gain;
    const char* gain2;
    const char* cost;
    const char* typeKey;
    int icon;
    UiKit::Color tint;
};

HangarEntry EntryOf(int id)
{
    if (id >= HANGAR_TALENT)
    {
        const TalentInfo& t = TALENTS[id - HANGAR_TALENT];
        return {t.name, t.gain, t.gain2, nullptr, "TALENT", t.icon, GOLD};
    }
    if (id >= HANGAR_TACTIC)
    {
        const TacticInfo& t = TACTICS[id - HANGAR_TACTIC];
        return {t.name, t.gain, t.gain2, nullptr, "TACTIC", t.icon, UiKit::Rgba(0.80f, 0.62f, 1.00f, 1.0f)};
    }
    const Info& info = INFO[id];
    return {info.name, info.gain, info.gain2, info.cost, VERB_KEY[static_cast<int>(info.verb)], id, VerbColor(info.verb)};
}

int TalentLevelOf(int id)
{
    return id >= HANGAR_TALENT ? Meta::TalentLevel(static_cast<Talent>(id - HANGAR_TALENT)) : 0;
}

// ---------------------------------------------------------------- run result card (game over and clear screens, right side)
void DrawRunResult()
{
    if (!g_HasResult) return;
    const float x = 1010.0f, w = 246.0f, pad = 16.0f;
    const float innerW = w - pad * 2.0f;
    struct Row
    {
        const char* key;
        int value;
    };
    const Row rows[6] = {{"WAVES CLEARED", g_Result.waves}, {"REACHED THE BOSS", g_Result.boss},
                         {"BOSS PHASES", g_Result.phases},  {"BOSS DEFEATED", g_Result.clear},
                         {"ABSORBS", g_Result.absorbs},     {"CHALLENGE BONUS", g_Result.bonus}};
    int rowCount = 0;
    for (const Row& row : rows)
        if (row.value > 0) ++rowCount;
    const bool unlockedLevel = g_Result.unlockedLevel >= 0;
    int missing = 0;
    const int target = Meta::NextTarget(missing);
    const float h =
        58.0f + static_cast<float>(rowCount) * 22.0f + 30.0f + (unlockedLevel ? 32.0f : 0.0f) + (target >= 0 ? 104.0f : 40.0f);
    const float y = 236.0f;
    GameMenuUi::Panel(x + 8.0f, y + 7.0f, w, h, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f), UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f));
    GameMenuUi::Panel(x, y, w, h, 0.0f, UiKit::Rgba(0.016f, 0.020f, 0.034f, 0.93f), UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.20f));
    UiKit::Fill(x, y, 96.0f, 4.0f, GOLD);
    DrawIcon(ICON_SHARD, x + pad, y + 14.0f, 26.0f, GOLD);
    const std::string title = Loc::Fmt("+{0} SHARDS", g_Result.total);
    Txt(title, x + pad + 34.0f, y + 28.0f, UiKit::FitFontPx(title, 20.0f, innerW - 34.0f, 12.0f), GOLD, TextAlignment::Left,
        innerW - 34.0f);
    float rowY = y + 62.0f;
    for (const Row& row : rows)
    {
        if (row.value <= 0) continue;
        const std::string label = Loc::T(row.key);
        Txt(label, x + pad, rowY, UiKit::FitFontPx(label, 12.0f, innerW - 44.0f, 9.0f), DIM, TextAlignment::Left, innerW - 44.0f);
        Txt("+" + std::to_string(row.value), x + w - pad, rowY, 12.0f, WHITE, TextAlignment::Right, 40.0f);
        rowY += 22.0f;
    }
    UiKit::Fill(x + pad, rowY - 6.0f, innerW, 1.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.14f));
    rowY += 10.0f;
    const std::string sh = Loc::T("SHARDS");
    Txt(sh, x + pad, rowY, 12.0f, DIM, TextAlignment::Left, 110.0f);
    Txt(std::to_string(g_Result.balance), x + w - pad, rowY, 16.0f, WHITE, TextAlignment::Right, 100.0f);
    rowY += 30.0f;
    if (unlockedLevel)
    {
        Chip(x + pad, rowY - 12.0f, 22.0f, Loc::Fmt("CHALLENGE {0} UNLOCKED", g_Result.unlockedLevel), 12.0f, GOLD, innerW);
        rowY += 32.0f;
    }
    if (target < 0)
    {
        Txt(Loc::T("ALL UNLOCKED"), x + pad, rowY + 6.0f, 13.0f, UiKit::Rgba(0.45f, 1.0f, 0.66f, 1.0f), TextAlignment::Left, innerW);
        return;
    }
    Txt(Loc::T("NEXT UNLOCK"), x + pad, rowY - 2.0f, 11.0f, DIM, TextAlignment::Left, innerW);
    rowY += 24.0f;
    const HangarEntry entry = EntryOf(target); // (a tactic or a talent: the modules are free)
    DrawIcon(entry.icon, x + pad, rowY - 13.0f, 26.0f, entry.tint);
    const std::string name = Loc::T(entry.name);
    Txt(name, x + pad + 34.0f, rowY, UiKit::FitFontPx(name, 16.0f, innerW - 34.0f, 10.0f), WHITE, TextAlignment::Left,
        innerW - 34.0f);
    rowY += 26.0f;
    // progress bar: a hairline track and a square fill
    const float price = static_cast<float>(Meta::Price(target));
    const float rate = price > 0.0f ? (std::min)(1.0f, static_cast<float>(g_Result.balance) / price) : 1.0f;
    UiKit::Fill(x + pad, rowY - 3.0f, innerW, 6.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.11f));
    if (rate > 0.01f)
    {
        UiKit::Fill(x + pad, rowY - 3.0f, (std::max)(6.0f, innerW * rate), 6.0f,
                    missing == 0 ? GOLD : UiKit::Rgba(0.55f, 0.86f, 1.0f, 1.0f));
    }
    rowY += 22.0f;
    const std::string hint = (missing == 0) ? Loc::T("READY: H ON TITLE") : Loc::Fmt("{0} MORE SHARDS", missing);
    Txt(hint, x + pad, rowY, UiKit::FitFontPx(hint, 12.0f, innerW, 9.0f), missing == 0 ? GOLD : DIM, TextAlignment::Left, innerW);
}

// ---------------------------------------------------------------- game over: how far the run came
// Left column, y = 346 .. 460 (the buttons start at 470). It answers three things at a glance: how much is done (a big percentage and a bar), how much is left, and what the
// next step is (one line). The bar is the whole mission: the five waves, then the boss in the three parts of its own bar. The best run so far is a gold notch on it to beat; a run
// that passes it says so. Boss practice is the boss alone: one bar (and a score, like a run).
namespace
{
constexpr float PROG_X = 84.0f, PROG_W = 420.0f, PROG_BAR_Y = 428.0f, PROG_BAR_H = 8.0f, PROG_GAP = 6.0f;

// where a share v (0..1) of the mission is on the bar
float ProgressBarX(float v, bool bossOnly, float x0)
{
    if (bossOnly) return x0 + PROG_W * v;
    const float share = Balance::MISSION_WAVES_SHARE;
    const float wavesW = PROG_W * share - PROG_GAP * 0.5f;
    const float bossW = PROG_W * (1.0f - share) - PROG_GAP * 0.5f;
    if (v <= share) return x0 + wavesW * (v / share);
    return x0 + PROG_W * share + PROG_GAP * 0.5f + bossW * ((v - share) / (1.0f - share));
}

// the line under the bar: the next step
std::string NextStepLine()
{
    if (g_DeathInBoss)
    {
        const BossHudInfo info = GameBoss::GetHudInfo(); // (the weights of the three phases)
        const int phase = g_DeathBossPhase;
        if (phase >= 3) return Loc::Fmt("ONLY {0}% OF THE BOSS IS LEFT", 100 - g_EndBoss);
        float edge = info.segmentWeight[0];
        if (phase == 2) edge += info.segmentWeight[1];
        const int away = (std::max)(1, static_cast<int>(std::ceil(edge * 100.0f)) - g_EndBoss);
        return Loc::Fmt("PHASE {0} IS {1}% AWAY", phase + 1, away);
    }
    const int wavesToBoss = (std::max)(1, Balance::WAVE_COUNT - g_DeathWave + 1);
    if (wavesToBoss == 1) return Loc::T("LAST WAVE BEFORE THE BOSS");
    return Loc::Fmt("{0} WAVES TO THE BOSS", wavesToBoss);
}

void DrawDefeatProgress(float reveal, float dx)
{
    const bool practice = g_PracticePhase > 0;
    const int pct = practice ? g_EndBoss : g_EndMission;
    const int best = practice ? g_EndBestBoss : g_EndBestMission;
    const bool newBest = best > 0 && pct > best;
    const float a = reveal;
    const float x0 = PROG_X + dx;
    const float right = x0 + PROG_W;

    // the percentage: heavy digits on a hard offset in the colour of the polarity
    const std::string big = std::to_string(pct) + "%";
    GameMenuUi::DrawHeavyText(big, x0 + 3.0f, 381.0f, 56.0f, GameMenuUi::Accent(0.60f * a), TextAlignment::Left, 240.0f);
    GameMenuUi::DrawHeavyText(big, x0, 378.0f, 56.0f, GameMenuUi::Ivory(a), TextAlignment::Left, 240.0f);
    // ... what it is, and what is left
    const float labelX = x0 + UiKit::TextWidth(big, 56.0f) + 20.0f;
    const float labelW = PROG_W - 104.0f - (labelX - x0);
    const std::string what = Loc::T(practice ? "BOSS DOWN" : "MISSION COMPLETE");
    const std::string left = Loc::Fmt("{0}% TO GO", 100 - pct);
    Txt(what, labelX, 364.0f, UiKit::FitFontPx(what, 15.0f, labelW, 10.0f), Alpha(WHITE, a), TextAlignment::Left, labelW);
    Txt(left, labelX, 390.0f, UiKit::FitFontPx(left, 17.0f, labelW, 10.0f), Alpha(GOLD, a), TextAlignment::Left, labelW);
    // the score
    {
        const bool high = GameState::score > 0 && GameState::score >= GameSave::GetRecordData().highScore;
        const std::string tag = Loc::T(high ? "NEW HIGH SCORE" : "SCORE");
        const std::string value = std::to_string(GameState::score);
        Txt(tag, right, 362.0f, UiKit::FitFontPx(tag, 11.0f, 120.0f, 8.0f), Alpha(high ? GOLD : DIM, a), TextAlignment::Right, 130.0f);
        GameMenuUi::DrawHeavyText(value, right, 388.0f, UiKit::FitFontPx(value, 26.0f, 120.0f, 14.0f), Alpha(high ? GOLD : WHITE, a), TextAlignment::Right, 130.0f);
    }

    // the bar: the waves, then the boss
    const float share = Balance::MISSION_WAVES_SHARE;
    const float wavesW = practice ? 0.0f : PROG_W * share - PROG_GAP * 0.5f;
    const float bossX = practice ? x0 : x0 + PROG_W * share + PROG_GAP * 0.5f;
    const float bossW = practice ? PROG_W : PROG_W * (1.0f - share) - PROG_GAP * 0.5f;
    const float m = static_cast<float>(pct) / 100.0f;
    const float wavesFill = practice ? 0.0f : (std::min)(1.0f, m / share);
    const float bossFill = practice ? m : (std::max)(0.0f, (m - share) / (1.0f - share));
    const UiKit::Color track = UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.13f * a);
    const UiKit::Color fill = GameMenuUi::Ivory(0.95f * a);
    const UiKit::Color cut = UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.95f * a); // the gaps between the parts
    if (!practice)
    {
        Txt(Loc::T("WAVES"), x0, 418.0f, 10.0f, Alpha(DIM, 0.85f * a), TextAlignment::Left, 120.0f);
        UiKit::Fill(x0, PROG_BAR_Y, wavesW, PROG_BAR_H, track);
        UiKit::Fill(x0, PROG_BAR_Y, wavesW * wavesFill, PROG_BAR_H, fill);
        for (int i = 1; i < Balance::WAVE_COUNT; ++i) UiKit::Fill(x0 + wavesW * static_cast<float>(i) / static_cast<float>(Balance::WAVE_COUNT) - 1.0f, PROG_BAR_Y, 2.0f, PROG_BAR_H, cut);
    }
    Txt(Loc::T("BOSS"), bossX, 418.0f, 10.0f, Alpha(DIM, 0.85f * a), TextAlignment::Left, 120.0f);
    UiKit::Fill(bossX, PROG_BAR_Y, bossW, PROG_BAR_H, track);
    UiKit::Fill(bossX, PROG_BAR_Y, bossW * bossFill, PROG_BAR_H, fill);
    {
        const BossHudInfo info = GameBoss::GetHudInfo(); // (the three parts of the bar of the boss, by their life)
        float edge = 0.0f;
        for (int i = 0; i < 2; ++i)
        {
            edge += info.segmentWeight[i];
            UiKit::Fill(bossX + bossW * edge - 1.0f, PROG_BAR_Y, 2.0f, PROG_BAR_H, cut);
        }
    }
    // where the run ended: a hard notch standing out of the bar (gold when it is a new best)
    const float endX = ProgressBarX(m, practice, x0);
    UiKit::Fill(endX - 1.0f, PROG_BAR_Y - 5.0f, 2.0f, PROG_BAR_H + 10.0f, newBest ? Alpha(GOLD, a) : GameMenuUi::Ivory(a));
    // the best so far: the notch to beat
    if (best > 0 && !newBest)
    {
        const float bestX = ProgressBarX(static_cast<float>(best) / 100.0f, practice, x0);
        UiKit::Fill(bestX - 1.0f, PROG_BAR_Y - 5.0f, 2.0f, PROG_BAR_H + 10.0f, Alpha(GOLD, 0.9f * a));
    }

    // under the bar: the next step on the left, the best on the right
    const float lineY = 452.0f;
    const std::string step = NextStepLine();
    const float stepW = PROG_W - 130.0f;
    Txt(step, x0, lineY, UiKit::FitFontPx(step, 13.0f, stepW, 9.0f), Alpha(WHITE, 0.92f * a), TextAlignment::Left, stepW);
    if (newBest)
    {
        const std::string tag = Loc::Fmt("NEW BEST +{0}%", pct - best);
        const float w = (std::min)(130.0f, UiKit::TextWidth(tag, 12.0f) + 22.0f);
        Chip(right - w, lineY - 11.0f, 22.0f, tag, 12.0f, Alpha(GOLD, a), w);
    }
    else if (best > 0)
    {
        const std::string tag = Loc::Fmt("BEST {0}%", best);
        Txt(tag, right, lineY, 13.0f, Alpha(GOLD, 0.9f * a), TextAlignment::Right, 130.0f);
    }
}
}

// ---------------------------------------------------------------- game over summary
// Left column of the game over page (virtual 720 px): the title ends at y = 214, this summary runs from y = 196 to about y = 330,
// the progress block (DrawDefeatProgress) fills y = 346 .. 460 below it, the buttons start at y = 470.
void DrawGameOverSummary(float reveal)
{
    reveal = reveal < 0.0f ? 0.0f : (reveal > 1.0f ? 1.0f : reveal);
    const float x = 84.0f;
    const float dx = -(1.0f - reveal) * 120.0f;
    // where the run ended: a red strip
    const std::string reached =
        g_DeathInBoss ? Loc::Fmt("REACHED BOSS PHASE {0}", g_DeathBossPhase) : Loc::Fmt("REACHED WAVE {0}", g_DeathWave);
    GameMenuUi::Plate(x - 14.0f + dx, 230.0f, 520.0f, 44.0f, Alpha(SIGNAL_RED, reveal));

    Txt(reached, x + 18.0f + dx, 252.0f, UiKit::FitFontPx(reached, 22.0f, 470.0f, 12.0f), Alpha(UiKit::Rgba(0.98f, 0.97f, 0.94f, 1.0f), reveal),
        TextAlignment::Left, 480.0f);
    // challenge level / practice tag next to the strip
    if (g_PracticePhase > 0 || g_RunLevel > 0)
    {
        const std::string label = g_PracticePhase > 0 ? Loc::T("BOSS PRACTICE") : Loc::Fmt("CHALLENGE {0}", g_RunLevel);
        Chip(x + 520.0f + dx, 239.0f, 24.0f, label, 12.0f, Alpha(g_PracticePhase > 0 ? PRACTICE_BLUE : LevelColor(g_RunLevel), reveal), 260.0f);
    }
    // time
    char timeText[16];
    std::snprintf(timeText, sizeof timeText, "%d:%02d", g_DeathSeconds / 60, g_DeathSeconds % 60);
    Txt(Loc::T("TIME"), x + dx, 304.0f, 13.0f, Alpha(UiKit::Rgba(0.66f, 0.74f, 0.88f, 0.95f), reveal), TextAlignment::Left, 200.0f);
    Txt(timeText, x + 90.0f + dx, 304.0f, 20.0f, Alpha(WHITE, reveal), TextAlignment::Left, 200.0f);
    // modules used
    if (g_OrderCount == 0)
    {
        Txt(Loc::T("NO MODULES"), x + 230.0f + dx, 304.0f, 13.0f, Alpha(UiKit::Rgba(0.56f, 0.64f, 0.78f, 0.90f), reveal), TextAlignment::Left, 300.0f);
    }
    else
    {
        const float size = 24.0f;
        const float gap = 10.0f;
        float ix = x + 230.0f + dx;
        for (int i = 0; i < g_OrderCount; ++i)
        {
            DrawIcon(g_Order[i], ix, 292.0f, size, Alpha(VerbColor(INFO[g_Order[i]].verb), reveal));
            ix += size + gap;
        }
    }
    DrawDefeatProgress(reveal, dx);
    DrawRunResult();
}

// ---------------------------------------------------------------- title screen: hangar hint + mode selector
// Right of the START GAME row: "<  CHALLENGE 2  >", one pip per mode and what the mode changes.
void DrawModeSelector()
{
    if (!TitleScene::IsStartGameSelected()) return;
    const int count = Meta::ModeCount();
    if (count <= 1) return;
    const int mode = Meta::Mode();
    const UiKit::Color tint = ModeColor(mode);
    const float x = MODE_X, w = MODE_W, h = MODE_H, top = MODE_CY - h * 0.5f;
    GameMenuUi::Plate(x, top, w, h, Alpha(tint, 0.12f));
    GameMenuUi::PlateLine(x, top, w, h, Alpha(tint, 0.55f));
    const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(GameBackground::GetFrame()) * 0.15f);
    UiKit::ImageRotated(UiAtlas::Id::Chevron, x + 22.0f, MODE_CY, 20.0f, 20.0f, 0.0f, Alpha(WHITE, 0.55f + 0.35f * pulse));
    UiKit::ImageRotated(UiAtlas::Id::Chevron, x + w - 22.0f, MODE_CY, 20.0f, 20.0f, 3.1415926f,
                        Alpha(WHITE, 0.55f + 0.35f * pulse));
    const std::string label = ModeLabel(mode);
    Txt(label, x + w * 0.5f, MODE_CY, UiKit::FitFontPx(label, 16.0f, w - 90.0f, 10.0f), tint, TextAlignment::Center, w - 70.0f);
    // one diamond per mode
    const float pipGap = 14.0f;
    float pipX = x + w * 0.5f - pipGap * static_cast<float>(count - 1) * 0.5f;
    for (int i = 0; i < count; ++i)
    {
        GameMenuUi::DiamondFill(pipX, top + h + 12.0f, 3.5f, i == mode ? Alpha(tint, 0.95f) : Alpha(DIM, 0.35f));
        pipX += pipGap;
    }
    // what this mode changes (two short lines)
    std::string line1, line2;
    if (Meta::ModeIsPractice(mode))
    {
        line1 = Loc::Fmt("BOSS FROM PHASE {0}", 1);
        line2 = Loc::T("NO SHARDS IN PRACTICE");
    }
    else if (Meta::ModeLevel(mode) > 0)
    {
        const int level = Meta::ModeLevel(mode);
        line1 = Loc::Fmt("BULLETS +{0}%  SHARDS +{1}%", static_cast<int>(std::lround(LEVEL_BULLET_STEP * 100.0f)) * level,
                         Meta::CHALLENGE_BONUS_PERCENT * level);
        line2 = Loc::T("MORE ELITES");
        if (level >= Meta::CHALLENGE_MAX) line2 += "  /  " + Loc::T("MAX HP -20");
    }
    float lineY = top + h + 28.0f;
    if (!line1.empty())
    {
        Txt(line1, x + w * 0.5f, lineY, UiKit::FitFontPx(line1, 12.0f, w, 9.0f), DIM, TextAlignment::Center, w + 20.0f);
        lineY += 17.0f;
    }
    if (!line2.empty())
    {
        Txt(line2, x + w * 0.5f, lineY, UiKit::FitFontPx(line2, 12.0f, w, 9.0f), DIM, TextAlignment::Center, w + 20.0f);
        lineY += 17.0f;
    }
    if (Meta::ModeIsPractice(mode))
    {
        Txt(Loc::T("USES YOUR LAST MODULES"), x + w * 0.5f, lineY, UiKit::FitFontPx(Loc::T("USES YOUR LAST MODULES"), 12.0f, w, 9.0f),
            DIM, TextAlignment::Center, w + 20.0f);
    }
}

// Right end of the HANGAR row of the title menu: the shard balance, and a pulsing NEW tag when something can be unlocked.
void DrawHangarRowInfo(float rowLeft, float cy, float rowW, bool lit, float alpha)
{
    Meta::Ensure();
    if (alpha <= 0.01f) return;
    const UiKit::Color gold = lit ? GOLD_ON_LIGHT : GOLD;
    const float right = rowLeft + rowW - 24.0f;
    const std::string balance = std::to_string(Meta::Shards());
    const float balanceW = UiKit::TextWidth(balance, 15.0f);
    Txt(balance, right, cy, 15.0f, Alpha(gold, alpha), TextAlignment::Right, balanceW + 40.0f);
    const float iconX = right - balanceW - 26.0f;
    DrawIcon(ICON_SHARD, iconX, cy - 9.0f, 18.0f, Alpha(gold, alpha));
    if (Meta::AnyAffordable())
    {
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(GameBackground::GetFrame()) * 0.15f);
        const float chipW = UiKit::TextWidth("NEW", 11.0f) + 18.0f;
        Chip(iconX - 10.0f - chipW, cy - 10.0f, 20.0f, "NEW", 11.0f, Alpha(gold, pulse * alpha), 60.0f);
    }
}

void DrawTitleHint()
{
    DrawModeSelector();
}

// ---------------------------------------------------------------- hangar (title screen overlay)
// Layout (virtual 1280x720): banner top left, type labels and a 5 x 4 grid of square tiles on the left, the selected module and
// the synergies on the right. Hairlines and hard offsets only: the same grammar as the pick screen and the pages.
// the detail panel (right): what is selected. One header row (icon, name, chips), the effects, and the action line at its foot.
constexpr float DETAIL_X = 730.0f, DETAIL_W = 420.0f, DETAIL_H = 340.0f;
constexpr float DEMO_H = 84.0f; // the looping demo of a module / a tactic / a synergy, in the detail panel
// the synergy cells under it: two columns
constexpr float SYN_CELL_W = 206.0f, SYN_CELL_H = 28.0f, SYN_GAP = 8.0f, SYN_PITCH = 33.0f;
float SynergyTop() { return HANGAR_Y + DETAIL_H + 22.0f; }
void SynergyCellXY(int shown, float& x, float& y)
{
    x = DETAIL_X + static_cast<float>(shown % 2) * (SYN_CELL_W + SYN_GAP);
    y = SynergyTop() + 14.0f + static_cast<float>(shown / 2) * SYN_PITCH;
}
// the combos that were found, in the order they are listed
int FoundCombo(int shown)
{
    int n = 0;
    for (int i = 0; i < Meta::COMBO_COUNT; ++i)
    {
        if (!Meta::ComboFound(i)) continue;
        if (n++ == shown) return i;
    }
    return -1;
}
int FoundCount()
{
    int n = 0;
    for (int i = 0; i < Meta::COMBO_COUNT; ++i) n += Meta::ComboFound(i) ? 1 : 0;
    return n;
}

void DrawHangarTile(int row, int col)
{
    const int id = HANGAR_LAYOUT[row][col];
    if (id < 0) return;
    float x, y;
    HangarTileXY(row, col, x, y);
    const HangarEntry entry = EntryOf(id);
    const bool selected = (id == g_HangarSel);
    // a synergy is highlighted: its two modules are the tiles that stay lit, the others fall back
    const bool synergyOn = g_HangarSyn >= 0 && g_HangarSyn < Meta::COMBO_COUNT;
    const bool inSynergy = synergyOn && (id == static_cast<int>(Meta::COMBOS[g_HangarSyn].a) || id == static_cast<int>(Meta::COMBOS[g_HangarSyn].b));
    const bool unlocked = Meta::IsUnlocked(id);
    const bool equipped = id >= HANGAR_TACTIC && id < HANGAR_TALENT && Meta::EquippedTactic() == id - HANGAR_TACTIC;
    const UiKit::Color verbTint = entry.tint;
    const UiKit::Color tint = selected ? UiKit::Rgba(verbTint.r * 0.50f, verbTint.g * 0.50f, verbTint.b * 0.50f, 1.0f) : verbTint;
    if (selected)
    {
        // THE SEAM: ivory on a hard offset in the colour of the polarity (P5), a glass line of the other colour over it (P3). The seam diamond sits INSIDE the tile (top left):
        // it used to hang in the gap and covered the tile next to it.
        GameMenuUi::SelectTile(x, y, HANGAR_TILE, HANGAR_TILE, 1.0f, true);
    }
    else
    {
        GameMenuUi::Panel(x, y, HANGAR_TILE, HANGAR_TILE, 0.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, equipped ? 0.10f : 0.035f),
                          equipped ? Alpha(verbTint, 0.85f) : UiKit::Rgba(1.0f, 1.0f, 1.0f, unlocked ? 0.22f : 0.12f));
    }
    if (inSynergy)
    {
        // the frame of a synergy: ivory on a hard offset in the colour of the polarity - the grammar of a selected tile, but a frame: the module stays visible
        GameMenuUi::Panel(x + 5.0f, y + 4.0f, HANGAR_TILE, HANGAR_TILE, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f), GameMenuUi::Accent(1.0f));
        GameMenuUi::Hairline(x - 2.0f, y - 2.0f, HANGAR_TILE + 4.0f, HANGAR_TILE + 4.0f, 2.0f, GameMenuUi::Ivory(0.95f));
    }
    if (g_HangarFlash > 0 && g_HangarFlashId == id)
    {
        const float a = static_cast<float>(g_HangarFlash) / 40.0f;
        // unlocked: the frame flashes in the colour of the polarity, a glass line of the other one, the seam diamond
        GameMenuUi::Hairline(x - 4.0f, y - 4.0f, HANGAR_TILE + 8.0f, HANGAR_TILE + 8.0f, 2.0f, GameMenuUi::Accent(a));
        UiKit::Fill(x - 4.0f, y - 10.0f, (HANGAR_TILE + 8.0f) * a, 1.5f, GameMenuUi::Counter(a));
        GameMenuUi::SeamDiamond(x + HANGAR_TILE * 0.5f, y + HANGAR_TILE * 0.5f, 10.0f * a, a);
    }
    const float iconSize = HANGAR_TILE * 0.64f;
    DrawIcon(entry.icon, x + (HANGAR_TILE - iconSize) * 0.5f, y + 4.0f, iconSize, unlocked ? tint : Alpha(tint, 0.30f));
    if (synergyOn && !inSynergy) GameMenuUi::Panel(x, y, HANGAR_TILE, HANGAR_TILE, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.62f), UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f)); // (falls back)
    const float rowY = y + HANGAR_TILE - 11.0f;
    if (id >= HANGAR_TALENT) // three pips: the levels that were bought are lit
    {
        const int level = TalentLevelOf(id);
        for (int k = 0; k < TALENT_LEVELS; ++k)
            GameMenuUi::DiamondFill(x + HANGAR_TILE * 0.5f + (static_cast<float>(k) - 1.0f) * 11.0f, rowY, 3.2f, Alpha(tint, k < level ? 0.95f : 0.25f));
        return;
    }
    if (unlocked)
    {
        GameMenuUi::DiamondFill(x + HANGAR_TILE * 0.5f, rowY, equipped ? 4.5f : 3.5f, Alpha(tint, equipped ? 1.0f : 0.85f));
        return;
    }
    const std::string price = std::to_string(Meta::Price(id));
    const float priceW = UiKit::TextWidth(price, 12.0f);
    const float groupW = 13.0f + 4.0f + priceW;
    const float gx = x + (HANGAR_TILE - groupW) * 0.5f;
    DrawIcon(ICON_LOCK, gx, rowY - 6.5f, 13.0f, selected ? UiKit::Rgba(0.27f, 0.29f, 0.36f, 1.0f) : UiKit::Rgba(0.80f, 0.84f, 0.92f, 0.9f));
    Txt(price, gx + 17.0f, rowY, 12.0f,
        Meta::CanAfford(id) ? (selected ? GOLD_ON_LIGHT : GOLD) : (selected ? UiKit::Rgba(0.27f, 0.29f, 0.36f, 1.0f) : DIM),
        TextAlignment::Left, priceW + 10.0f);
}

// A synergy in the detail panel: the two modules, its name, what it does, and the demo - a short loop that shows it.
void DrawSynergyDetails(int combo)
{
    const float x = DETAIL_X, y = HANGAR_Y, w = DETAIL_W, h = DETAIL_H;
    const Meta::ComboInfo& info = Meta::COMBOS[combo];
    const UiKit::Color none = UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f);
    GameMenuUi::Panel(x + 8.0f, y + 7.0f, w, h, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f), none);
    GameMenuUi::Panel(x, y, w, h, 0.0f, UiKit::Rgba(0.016f, 0.020f, 0.034f, 0.93f), UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.20f));
    UiKit::Fill(x, y, w, 4.0f, GameMenuUi::Accent(1.0f));
    // the two modules and what they make
    const float cy = y + 46.0f;
    const int ids[2] = {static_cast<int>(info.a), static_cast<int>(info.b)};
    for (int k = 0; k < 2; ++k)
    {
        const UiKit::Color tint = VerbColor(INFO[ids[k]].verb);
        const float cx = x + 40.0f + static_cast<float>(k) * 78.0f;
        GameMenuUi::DiamondFill(cx, cy, 26.0f, Alpha(tint, 0.14f));
        GameMenuUi::DiamondOutline(cx, cy, 26.0f, 2.0f, Alpha(tint, 0.70f));
        DrawIcon(ids[k], cx - 15.0f, cy - 15.0f, 30.0f, tint);
    }
    DrawIcon(ICON_PLUS, x + 79.0f - 7.0f, cy - 7.0f, 14.0f, GOLD);
    const float tx = x + 160.0f, tw = w - 160.0f - 16.0f;
    const std::string name = Loc::T(info.name);
    const float namePx = UiKit::FitFontPx(name, 24.0f, tw, 13.0f);
    GameMenuUi::DrawHeavyText(name, tx + 2.0f, y + 36.0f + 1.6f, namePx, GameMenuUi::Accent(0.9f), TextAlignment::Left, tw);
    GameMenuUi::DrawHeavyText(name, tx, y + 36.0f, namePx, WHITE, TextAlignment::Left, tw);
    Chip(tx, y + 56.0f, 20.0f, Loc::T("SYNERGY"), 11.0f, GOLD, 130.0f);
    UiKit::Fill(x + 20.0f, y + 88.0f, w - 40.0f, 1.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.16f));
    // what it does
    DrawGainRow(info.desc, x + 20.0f, y + 108.0f, w - 40.0f, 13.5f);
    // the demo, then what it takes: both modules in one run
    DrawSynergyDemo(combo, x + 20.0f, y + h - DEMO_H - 50.0f, w - 40.0f, DEMO_H, GameBackground::GetFrame());
    const float bottomY = y + h - 22.0f;
    UiKit::Fill(x + 20.0f, bottomY - 20.0f, w - 40.0f, 1.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.10f));
    Txt(Loc::T("OWN BOTH IN ONE RUN"), x + 20.0f, bottomY, 12.5f, DIM, TextAlignment::Left, w - 40.0f);
}

void DrawHangarDetails()
{
    if (g_HangarSyn >= 0 && g_HangarSyn < Meta::COMBO_COUNT)
    {
        DrawSynergyDetails(g_HangarSyn);
        return;
    }
    const float x = DETAIL_X, y = HANGAR_Y, w = DETAIL_W, h = DETAIL_H;
    const UiKit::Color none = UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f);
    GameMenuUi::Panel(x + 8.0f, y + 7.0f, w, h, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f), none);
    GameMenuUi::Panel(x, y, w, h, 0.0f, UiKit::Rgba(0.016f, 0.020f, 0.034f, 0.93f), UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.20f));
    const int id = g_HangarSel;
    UiKit::Fill(x, y, w, 4.0f, id >= 0 ? EntryOf(id).tint : GameMenuUi::Accent(1.0f)); // (one bar, in the colour of the type: like the cards of the pick screen)
    if (id < 0)
    {
        // nothing selected: a quiet invitation in the middle of the panel
        GameMenuUi::SeamDiamond(x + w * 0.5f, y + h * 0.5f - 22.0f, 9.0f, 0.55f);
        const std::string hint = Loc::T("POINT AT A TILE");
        Txt(hint, x + w * 0.5f, y + h * 0.5f + 6.0f, UiKit::FitFontPx(hint, 14.0f, w - 40.0f, 10.0f), DIM, TextAlignment::Center, w - 20.0f);
        return;
    }
    const HangarEntry entry = EntryOf(id);
    const bool unlocked = Meta::IsUnlocked(id);
    const bool isTactic = id >= HANGAR_TACTIC && id < HANGAR_TALENT;
    const bool isTalent = id >= HANGAR_TALENT;
    const bool equipped = isTactic && Meta::EquippedTactic() == id - HANGAR_TACTIC;
    const UiKit::Color tint = entry.tint;
    // header row: the icon on its diamond, the name, the chips under it
    const float cx = x + 50.0f, cy = y + 54.0f;
    GameMenuUi::DiamondFill(cx, cy, 32.0f, Alpha(tint, 0.14f));
    GameMenuUi::DiamondOutline(cx, cy, 32.0f, 2.0f, Alpha(tint, 0.70f));
    DrawIcon(entry.icon, cx - 19.0f, cy - 19.0f, 38.0f, unlocked ? tint : Alpha(tint, 0.40f));
    const float tx = x + 100.0f;
    const float tw = w - 100.0f - 16.0f;
    const std::string name = Loc::T(entry.name);
    const float namePx = UiKit::FitFontPx(name, 24.0f, tw, 13.0f);
    GameMenuUi::DrawHeavyText(name, tx + 2.0f, y + 40.0f + 1.6f, namePx, GameMenuUi::Accent(0.9f), TextAlignment::Left, tw);
    GameMenuUi::DrawHeavyText(name, tx, y + 40.0f, namePx, WHITE, TextAlignment::Left, tw);
    // the chips under the name: the type, and only what carries information - the level of a talent, EQUIPPED, LOCKED. (FREE and OWNED / UNLOCKED were
    // always there and told nothing: gone.)
    std::string status;
    if (isTalent)
        status = Loc::Fmt("LEVEL {0} / {1}", TalentLevelOf(id), TALENT_LEVELS);
    else if (equipped)
        status = Loc::T("EQUIPPED");
    else if (id >= COUNT && !unlocked)
        status = Loc::T("LOCKED");
    const float chipW = Chip(tx, y + 62.0f, 20.0f, Loc::T(entry.typeKey), 11.0f, tint, 130.0f);
    if (!status.empty()) Chip(tx + chipW + 8.0f, y + 62.0f, 20.0f, status, 11.0f, (isTalent || equipped) ? GameMenuUi::Accent(1.0f) : UiKit::Rgba(0.80f, 0.84f, 0.92f, 1.0f), 150.0f);
    UiKit::Fill(x + 20.0f, y + 100.0f, w - 40.0f, 1.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.16f));
    // the effects (a long text wraps; the next row moves down by what the row used)
    float rowY = y + 122.0f;
    const float rowW = w - 40.0f;
    rowY += DrawGainRow(entry.gain, x + 20.0f, rowY, rowW, 13.5f);
    if (entry.gain2 != nullptr) rowY += DrawGainRow(entry.gain2, x + 20.0f, rowY, rowW, 13.5f);
    if (entry.cost != nullptr) DrawCostRow(entry.cost, x + 20.0f, rowY, rowW, 13.5f);
    // the demo: what the module / the tactic does, as a looping diagram (a talent shows its three levels)
    const float demoY = y + h - DEMO_H - 50.0f;
    if (!isTalent)
    {
        DrawModuleDemo(id, x + 20.0f, demoY, w - 40.0f, DEMO_H, GameBackground::GetFrame());
    }
    else
    {
        GameMenuUi::Panel(x + 20.0f, demoY, w - 40.0f, DEMO_H, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.46f), UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.16f));
        const int level = TalentLevelOf(id);
        for (int k = 0; k < TALENT_LEVELS; ++k)
        {
            const float levelX = x + w * 0.5f + (static_cast<float>(k) - 1.0f) * 110.0f, levelY = demoY + DEMO_H * 0.5f - 6.0f;
            const bool on = k < level;
            GameMenuUi::DiamondFill(levelX, levelY, 20.0f, Alpha(tint, on ? 0.30f : 0.08f));
            GameMenuUi::DiamondOutline(levelX, levelY, 20.0f, 2.0f, Alpha(tint, on ? 0.95f : 0.35f));
            Txt(std::to_string(k + 1), cx, cy, 15.0f, on ? WHITE : DIM, TextAlignment::Center, 40.0f);
            Txt(Loc::Fmt("PRICE {0}", TALENT_PRICE[k]), cx, demoY + DEMO_H - 12.0f, 11.5f, on ? DIM : GOLD, TextAlignment::Center, 100.0f);
        }
    }
    // the action line at the foot: price at the left, the key at the right, on one line
    const float bottomY = y + h - 22.0f;
    if (id < COUNT) return;
    UiKit::Fill(x + 20.0f, bottomY - 20.0f, w - 40.0f, 1.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.10f));
    if (isTactic && unlocked)
    {
        const std::string label = Loc::T(equipped ? "PUT BACK" : "TAKE ALONG");
        const float lw = UiKit::TextWidth(label, 14.0f);
        Txt(label, x + w - 20.0f, bottomY, 14.0f, tint, TextAlignment::Right, lw + 20.0f);
        UiKit::KeyCap("ENTER", x + w - 20.0f - lw - 70.0f, bottomY - 12.0f, 24.0f, tint);
        return;
    }
    const int price = Meta::Price(id);
    if (price <= 0)
    {
        Txt(Loc::T("MAX LEVEL"), x + 20.0f, bottomY, 13.0f, DIM, TextAlignment::Left, 200.0f);
        return;
    }
    const bool can = Meta::CanAfford(id);
    DrawIcon(ICON_SHARD, x + 20.0f, bottomY - 10.0f, 20.0f, GOLD);
    GameMenuUi::DrawHeavyText(std::to_string(price), x + 46.0f, bottomY, 18.0f, can ? GOLD : DIM, TextAlignment::Left, 90.0f);
    if (g_HangarMessage > 0)
    {
        const std::string message = Loc::T("NOT ENOUGH SHARDS");
        GameMenuUi::Notice(message, x + w - 20.0f - (UiKit::TextWidth(message, 15.0f) * 1.06f + 58.0f) * 0.5f, bottomY, g_HangarMessage, 70, true);
    }
    else if (can)
    {
        const std::string label = Loc::T(isTalent ? "UPGRADE" : "UNLOCK");
        const float lw = UiKit::TextWidth(label, 14.0f);
        Txt(label, x + w - 20.0f, bottomY, 14.0f, GOLD, TextAlignment::Right, lw + 20.0f);
        UiKit::KeyCap("ENTER", x + w - 20.0f - lw - 70.0f, bottomY - 12.0f, 24.0f, GOLD);
    }
}

// the synergies (right, under the details): two columns of compact cells (two icons + the name), only the ones that were found. They can be chosen: the one
// that is highlighted fills the detail panel and lights its two modules in the grid. One line says how many are left to find.
void DrawHangarSynergies()
{
    const float x = DETAIL_X, w = DETAIL_W;
    const float top = SynergyTop();
    const UiKit::Color none = UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f);
    GameMenuUi::SectionTitle(Loc::T("SYNERGY"), x, top, w, 1.0f);
    int shown = 0, hidden = 0;
    for (int i = 0; i < Meta::COMBO_COUNT; ++i)
    {
        if (!Meta::ComboFound(i))
        {
            ++hidden;
            continue;
        }
        const Meta::ComboInfo& combo = Meta::COMBOS[i];
        float cx, cy;
        SynergyCellXY(shown, cx, cy);
        ++shown;
        const bool lit = g_HangarSyn == i;
        // the module that is selected in the grid is part of this synergy: the cell says so with a thin ivory line on its top edge
        const bool involves = g_HangarSel >= 0 && g_HangarSel < COUNT && (static_cast<int>(combo.a) == g_HangarSel || static_cast<int>(combo.b) == g_HangarSel);
        GameMenuUi::Panel(cx, cy, SYN_CELL_W, SYN_CELL_H, 0.0f, lit ? UiKit::Rgba(0.93f, 0.92f, 0.88f, 0.96f) : UiKit::Rgba(1.0f, 1.0f, 1.0f, involves ? 0.10f : 0.045f), none);
        UiKit::Fill(cx, cy, 3.0f, SYN_CELL_H, lit ? GameMenuUi::Accent(1.0f) : GameMenuUi::Accent(involves ? 1.0f : 0.7f));
        if (involves && !lit) UiKit::Fill(cx + 3.0f, cy, SYN_CELL_W - 3.0f, 1.5f, GameMenuUi::Ivory(0.7f));
        DrawIcon(static_cast<int>(combo.a), cx + 10.0f, cy + 8.0f, 18.0f, VerbColor(INFO[static_cast<int>(combo.a)].verb));
        DrawIcon(ICON_PLUS, cx + 30.0f, cy + 12.0f, 10.0f, lit ? GOLD_ON_LIGHT : GOLD);
        DrawIcon(static_cast<int>(combo.b), cx + 42.0f, cy + 8.0f, 18.0f, VerbColor(INFO[static_cast<int>(combo.b)].verb));
        const std::string nm = Loc::T(combo.name);
        const float tw = SYN_CELL_W - 72.0f;
        GameMenuUi::DrawHeavyText(nm, cx + 68.0f, cy + SYN_CELL_H * 0.5f, UiKit::FitFontPx(nm, 13.0f, tw, 9.0f), lit ? INK : WHITE, TextAlignment::Left, tw + 10.0f);
        if (lit && g_HangarSynFocus) GameMenuUi::SeamDiamond(cx - 12.0f, cy + SYN_CELL_H * 0.5f, 6.0f, 1.0f); // (the keyboard cursor)
    }
    if (hidden > 0)
    {
        const float below = top + 14.0f + static_cast<float>((shown + 1) / 2) * SYN_PITCH + 8.0f;
        GameMenuUi::SeamDiamond(x + 6.0f, below, 4.0f, 0.6f);
        const std::string more = Loc::Fmt(shown == 0 ? "{0} SYNERGIES TO DISCOVER" : "{0} MORE TO DISCOVER", hidden);
        Txt(more, x + 18.0f, below, 12.0f, DIM, TextAlignment::Left, w - 30.0f);
    }
}

// where the logic of the hangar (ModulesHangar.cpp) finds the synergy under the mouse and walks the list with the keys
int HangarSynergyAt(float px, float py)
{
    for (int shown = 0; shown < FoundCount(); ++shown)
    {
        float x, y;
        SynergyCellXY(shown, x, y);
        if (px >= x && px <= x + SYN_CELL_W && py >= y && py <= y + SYN_CELL_H) return FoundCombo(shown);
    }
    return -1;
}

int HangarSynergyFirst()
{
    return FoundCombo(0);
}

int HangarSynergyStep(int current, int dRow, int dCol)
{
    const int count = FoundCount();
    if (count <= 0) return -1;
    if (current < 0) return FoundCombo(0);
    int shown = 0;
    for (int i = 0; i < count; ++i)
        if (FoundCombo(i) == current) shown = i;
    int col = shown % 2, row = shown / 2;
    const int rows = (count + 1) / 2;
    if (dCol != 0) col = 1 - col;
    row = (row + dRow + rows) % rows;
    int next = row * 2 + col;
    if (next >= count) next = count - 1;
    return FoundCombo(next);
}

// RESONANCE (left, under the tiles): what two and three modules of one type give - written out, one row per type: its colour and name, then the bonus of 2 and the
// bonus of 3 side by side. The numbers are the constants of Balance.h (Loc::Fmt puts them into the text), so this table can never disagree with the game.
// The type of a row lights up when the player owns that many - not in the hangar (it is a menu, nothing is owned): all rows are at rest.
void DrawHangarResonance(float top)
{
    constexpr float X = 120.0f, W = 560.0f, ROW = 31.0f, NAME_W = 84.0f;
    GameMenuUi::SectionTitle(Loc::T("RESONANCE"), X, top + 1.0f, W, 1.0f);
    struct Row
    {
        Verb verb;
        const char* typeKey;
        std::string two, three;
    };
    const Row rows[3] = {
        {Verb::Absorb, "ABSORB", Loc::Fmt("RES ABSORB 2", static_cast<int>(std::lround(Balance::RES_ABSORB_RANGE * 100.0f))), Loc::Fmt("RES ABSORB 3", Balance::RES_ABSORB_ARC_EVERY)},
        {Verb::Break, "BREAK", Loc::Fmt("RES BREAK 2", Balance::RES_BREAK_ENERGY_CUT), Loc::Fmt("RES BREAK 3", static_cast<int>(std::lround(static_cast<float>(Balance::RES_BREAK_BLAST_HITS))))},
        {Verb::Survive, "SURVIVE", Loc::Fmt("RES SURVIVE 2", Balance::RES_SURVIVE_HP), Loc::Fmt("RES SURVIVE 3", Balance::RES_SURVIVE_PHASE_HEAL)},
    };
    // the bonus of two modules is the short one, the bonus of three the long one: the columns are not equal (210 | 266 px)
    const float colWidth[2] = {210.0f, W - NAME_W - 210.0f};
    for (int i = 0; i < 3; ++i)
    {
        const float y = top + 20.0f + static_cast<float>(i) * ROW;
        const UiKit::Color tint = VerbColor(rows[i].verb);
        GameMenuUi::Panel(X, y, W, ROW - 4.0f, 0.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.040f), UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f));
        UiKit::Fill(X, y, 3.0f, ROW - 4.0f, tint);
        const std::string type = Loc::T(rows[i].typeKey);
        GameMenuUi::DrawHeavyText(type, X + 14.0f, y + (ROW - 4.0f) * 0.5f, UiKit::FitFontPx(type, 13.0f, NAME_W - 14.0f, 9.0f), tint, TextAlignment::Left, NAME_W);
        for (int k = 0; k < 2; ++k)
        {
            const float cx = X + NAME_W + (k == 0 ? 0.0f : colWidth[0]);
            const float colW = colWidth[k];
            const std::string count = Loc::T(k == 0 ? "RES 2" : "RES 3");
            // the count in a small plate, then the effect after it
            const float cw = UiKit::TextWidth(count, 11.0f) + 14.0f;
            GameMenuUi::Panel(cx, y + 5.0f, cw, ROW - 14.0f, 0.0f, Alpha(tint, 0.22f), UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f));
            GameMenuUi::DrawHeavyText(count, cx + cw * 0.5f, y + (ROW - 4.0f) * 0.5f, 11.0f, WHITE, TextAlignment::Center, cw);
            const std::string& effect = k == 0 ? rows[i].two : rows[i].three;
            const float ew = colW - cw - 14.0f;
            Txt(effect, cx + cw + 8.0f, y + (ROW - 4.0f) * 0.5f, UiKit::FitFontPx(effect, 12.5f, ew, 8.0f), Alpha(WHITE, 0.90f), TextAlignment::Left, ew + 8.0f);
        }
    }
}

// returns true while the hangar is open (DrawTitleUI must not draw anything else then)
bool DrawTitleOverlay()
{
    if (!g_HangarOpen) return false;
    Meta::Ensure();
    Renderer2D_ClearCameraOffset();
    GameMenuUi::DrawStage(GameMenuUi::Stage::Hangar, 1.0f);
    // banner top left; top right ONE plate with the shard balance (the same slanted plate as the rows and the tabs of every page) - no rule above the details
    GameMenuUi::DrawHeader(Loc::T("HANGAR"), std::string(), static_cast<float>(g_HangarTimer) / 12.0f);
    {
        const std::string balance = std::to_string(Meta::Shards());
        const float numW = UiKit::TextWidth(balance, 24.0f);
        const float pw = numW + 150.0f, ph = 40.0f, px = DETAIL_X + DETAIL_W - pw, py = 84.0f;
        const float in = GameMenuUi::Clamp01(static_cast<float>(g_HangarTimer) / 14.0f);
        GameMenuUi::Plate(px + 6.0f, py + 5.0f, pw, ph, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f * in));
        GameMenuUi::Plate(px, py, pw, ph, UiKit::Rgba(0.016f, 0.020f, 0.034f, 0.94f * in));
        GameMenuUi::PlateLine(px, py, pw, ph, GameMenuUi::Ivory(0.30f * in));
        Txt(Loc::T("SHARDS"), px + 24.0f, py + ph * 0.5f, 12.0f, Alpha(DIM, in), TextAlignment::Left, 80.0f);
        DrawIcon(ICON_SHARD, px + pw - numW - 54.0f, py + ph * 0.5f - 12.0f, 24.0f, Alpha(GOLD, in));
        GameMenuUi::DrawHeavyText(balance, px + pw - 22.0f, py + ph * 0.5f, 24.0f, Alpha(GOLD, in), TextAlignment::Right, numW + 30.0f);
    }
    // type labels + tiles
    for (int r = 0; r < HANGAR_ROWS; ++r)
    {
        int first = -1;
        for (int c = 0; c < HANGAR_COLS; ++c)
        {
            if (HANGAR_LAYOUT[r][c] >= 0)
            {
                first = HANGAR_LAYOUT[r][c];
                break;
            }
        }
        if (first >= 0)
        {
            float x, y;
            HangarTileXY(r, 0, x, y);
            const HangarEntry entry = EntryOf(first);
            const UiKit::Color verbColor = entry.tint;
            // the type: a slanted tick of its colour and its name in heavy letters
            const float ty = y + HANGAR_TILE * 0.5f;
            const Renderer2DVertex tick[4] = {{UiKit::NX(124.0f), UiKit::NY(ty - 10.0f), verbColor.r, verbColor.g, verbColor.b, verbColor.a},
                                              {UiKit::NX(129.0f), UiKit::NY(ty - 10.0f), verbColor.r, verbColor.g, verbColor.b, verbColor.a},
                                              {UiKit::NX(123.0f), UiKit::NY(ty + 10.0f), verbColor.r, verbColor.g, verbColor.b, verbColor.a},
                                              {UiKit::NX(118.0f), UiKit::NY(ty + 10.0f), verbColor.r, verbColor.g, verbColor.b, verbColor.a}};
            Renderer2D_DrawPolygon(tick, 4);
            const std::string verb = Loc::T(first >= HANGAR_TALENT ? "TALENTS" : (first >= HANGAR_TACTIC ? "TACTICS" : entry.typeKey));
            GameMenuUi::DrawHeavyText(verb, 136.0f, ty, UiKit::FitFontPx(verb, 13.0f, 80.0f, 9.0f), verbColor, TextAlignment::Left, 84.0f);
        }
        for (int c = 0; c < HANGAR_COLS; ++c)
            if (HANGAR_LAYOUT[r][c] != g_HangarSel) DrawHangarTile(r, c);
    }
    // the selected tile is drawn last: its offset plates lie over the gap and must not be cut by the tiles drawn after it
    for (int r = 0; r < HANGAR_ROWS; ++r)
        for (int c = 0; c < HANGAR_COLS; ++c)
            if (g_HangarSel >= 0 && HANGAR_LAYOUT[r][c] == g_HangarSel) DrawHangarTile(r, c);
    if (g_HangarSyn >= 0 && g_HangarSyn < Meta::COMBO_COUNT)
    {
        // a line between the two modules of the highlighted synergy, with the seam diamond in the middle
        int ra = 0, ca = 0, rb = 0, cb = 0;
        if (HangarFind(static_cast<int>(Meta::COMBOS[g_HangarSyn].a), ra, ca) && HangarFind(static_cast<int>(Meta::COMBOS[g_HangarSyn].b), rb, cb))
        {
            float ax, ay, bx, by;
            HangarTileXY(ra, ca, ax, ay);
            HangarTileXY(rb, cb, bx, by);
            const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(GameBackground::GetFrame()) * 0.12f);
            GameMenuUi::Segment(ax + HANGAR_TILE * 0.5f, ay + HANGAR_TILE * 0.5f, bx + HANGAR_TILE * 0.5f, by + HANGAR_TILE * 0.5f, 2.0f, GameMenuUi::Counter(0.55f * pulse));
            GameMenuUi::SeamDiamond((ax + bx) * 0.5f + HANGAR_TILE * 0.5f, (ay + by) * 0.5f + HANGAR_TILE * 0.5f, 6.0f, 1.0f);
        }
    }
    DrawHangarDetails();
    DrawHangarSynergies();
    // resonance rule + progress (left, under the tiles)
    const float ruleY = HANGAR_Y + static_cast<float>(HANGAR_ROWS) * (HANGAR_TILE + HANGAR_GAP) + 22.0f;
    DrawHangarResonance(ruleY);
    // footer key hints: ENTER buys / takes along what is selected (nothing to do on a module: they are free)
    const bool enterUseful = g_HangarSyn < 0 && g_HangarSel >= COUNT && (Meta::Price(g_HangarSel) > 0 || g_HangarSel < HANGAR_TALENT);
    const int selForHint = g_HangarSel < 0 ? 0 : g_HangarSel;
    float fx = 120.0f;
    const struct
    {
        const char* key;
        const char* text;
        bool show;
    } hints[4] = {{"ARROWS", "SELECT", true},
                  {"ENTER", selForHint >= HANGAR_TALENT ? "UPGRADE" : (Meta::IsUnlocked(selForHint) ? "TAKE ALONG" : "UNLOCK"), enterUseful},
                  {"TAB", g_HangarSynFocus ? "MODULES" : "SYNERGY", FoundCount() > 0},
                  {"ESC", "BACK", true}};

    for (const auto& hint : hints)
    {
        if (!hint.show) continue;
        const float capW = UiKit::KeyCap(hint.key, fx, 678.0f, 26.0f, UiKit::Rgba(0.85f, 0.92f, 1.0f, 0.85f));
        const std::string label = Loc::T(hint.text);
        Txt(label, fx + capW + 10.0f, 691.0f, 14.0f, WHITE, TextAlignment::Left, 160.0f);
        fx += capW + 10.0f + UiKit::TextWidth(label, 14.0f) + 44.0f;
    }
    return true;
}
}









