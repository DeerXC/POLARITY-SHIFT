#include "game/scene/RecordsScene.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "game/GameBackground.h"
#include "game/GameFlow.h"
#include "game/GameSave.h"
#include "rogue/Meta.h"
#include "rogue/ModulesDrawInternal.h"
#include "rogue/Balance.h"
#include "ui/GameMenuUi.h"
#include "ui/UiKit.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

using UiKit::Color;
using UiKit::Rgba;

// Layout (virtual 1280 x 720 px). The red field of this page ends at a steep edge about x = 930 and the red / blue cut leaves the page
// at about y = 550 (see LAYOUTS in GameMenuUi.cpp), so the list is 6 rows high and 440 px wide: nothing of it touches a line.
namespace
{
constexpr float STAT_X = 96.0f;
constexpr float STAT_W = 330.0f;
constexpr float STAT_Y = 164.0f;
constexpr float STAT_H = 40.0f;
constexpr float STAT_STEP = 44.0f;

constexpr float LIST_X = 470.0f;
constexpr float LIST_W = 440.0f;
constexpr float LIST_HEAD_Y = 176.0f;
constexpr float LIST_Y = 196.0f;
constexpr float LIST_H = 52.0f;
constexpr float LIST_STEP = 56.0f;
constexpr int ROWS_PER_PAGE = 6;

int g_Selected = -1; // index of the highlighted run, -1 = nothing is highlighted (a menu only highlights what is pointed at)
int g_Page = 0;
int g_View = -1;     // >= 0: the run that is open (its details fill the page); -1 = the list
int g_ViewT = 0;     // frames since a run was opened (the parts build up)

float NormX(float px)
{
    return px / 640.0f - 1.0f;
}

float NormY(float py)
{
    return 1.0f - py / 360.0f;
}

// the rows build up one after the other, from the top (GameMenuUi::RowAlpha / RowShift work on the height of a row)
float RowA(float topPx)
{
    return GameMenuUi::RowAlpha(NormY(topPx));
}

float RowS(float topPx)
{
    return GameMenuUi::RowShift(NormY(topPx)) * 640.0f;
}

int PageCount(int runs)
{
    return (std::max)(1, (runs + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
}

std::string FormatDate(long long time, bool withYear)
{
    std::time_t t = static_cast<std::time_t>(time);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, withYear ? "%Y/%m/%d  %H:%M" : "%m/%d %H:%M", &local);
    return text;
}

std::string FormatTime(int seconds)
{
    char text[24];
    std::snprintf(text, sizeof text, "%d:%02d", seconds / 60, seconds % 60);
    return text;
}

std::string ResultText(const RunRecord& r)
{
    return Loc::T(r.result == 1 ? "CLEARED" : (r.result == 2 ? "ABANDONED" : "DEFEATED"));
}

// where the run ended ("" for a cleared run: it went through everything)
std::string WhereText(const RunRecord& r)
{
    if (r.result == 1) return std::string();
    return (r.bossPhase > 0 || r.wave > Balance::WAVE_COUNT) ? Loc::Fmt("BOSS PHASE {0}", (std::max)(1, r.bossPhase)) : Loc::Fmt("WAVE {0}", r.wave); // (old records: the boss was "wave 5" and always had a phase)
}

std::string ModeName(int mode)
{
    if (mode == RUN_MODE_PRACTICE) return Loc::T("BOSS PRACTICE");
    return mode <= 0 ? Loc::T("STANDARD") : Loc::Fmt("CHALLENGE {0}", mode);
}

Color ModeColor(int mode)
{
    return mode == RUN_MODE_PRACTICE ? Mod::PRACTICE_BLUE : Mod::LevelColor(mode);
}

Color ResultColor(const RunRecord& r, bool onLight)
{
    if (r.result == 1) return onLight ? Mod::GOLD_ON_LIGHT : Mod::GOLD;
    if (r.result == 0) return onLight ? Rgba(0.58f, 0.10f, 0.08f, 1.0f) : Mod::DANGER_RED;
    return onLight ? Rgba(0.27f, 0.29f, 0.36f, 1.0f) : Mod::DIM;
}

// ---- the four totals ------------------------------------------------------------------------------------------------------
void DrawStat(const char* labelKey, const std::string& value, int index, Color valueColor, bool important)
{
    const float top = STAT_Y + STAT_STEP * static_cast<float>(index);
    GameMenuUi::DrawRow(NormX(STAT_X), NormY(top), NormX(STAT_X + STAT_W), NormY(top + STAT_H),
                        important ? GameMenuUi::RowStyle::Important : GameMenuUi::RowStyle::Normal, true);
    const float a = RowA(top);
    const float s = RowS(top);
    const float cy = top + STAT_H * 0.5f;
    const std::string label = Loc::T(labelKey);
    Mod::Txt(label, STAT_X + 40.0f + s, cy, UiKit::FitFontPx(label, 14.0f, 170.0f, 9.0f), Mod::Alpha(important ? Mod::WHITE : Mod::DIM, a),
             TextAlignment::Left, 190.0f);
    // the value heavy, with a small hard offset in the colour of the polarity (THE SEAM)
    GameMenuUi::DrawHeavyText(value, STAT_X + STAT_W - 14.5f + s, cy + 1.6f, 22.0f, GameMenuUi::Accent(0.6f * a), TextAlignment::Right, 150.0f);
    GameMenuUi::DrawHeavyText(value, STAT_X + STAT_W - 16.0f + s, cy, 22.0f, Mod::Alpha(valueColor, a), TextAlignment::Right, 150.0f);
}

// ---- the list --------------------------------------------------------------------------------------------------------------
void DrawRunRow(int rowOnPage, int index)
{
    const RunRecord& r = GameSave::GetRunRecord(index);
    const float top = LIST_Y + LIST_STEP * static_cast<float>(rowOnPage);
    const bool selected = g_Selected == index;
    GameMenuUi::DrawRow(NormX(LIST_X), NormY(top), NormX(LIST_X + LIST_W), NormY(top + LIST_H),
                        selected ? GameMenuUi::RowStyle::Selected : GameMenuUi::RowStyle::Normal, true);
    const bool lit = GameMenuUi::SelectionAmount(NormX(LIST_X), NormY(top)) > 0.5f;
    const float a = RowA(top);
    const float s = RowS(top);
    const float cy = top + LIST_H * 0.5f;
    const float leftX = LIST_X + 40.0f + s;
    const float leftW = LIST_W - 40.0f - 120.0f;

    const Color ink = Rgba(0.05f, 0.06f, 0.09f, 1.0f);
    const Color sub = lit ? Rgba(0.27f, 0.29f, 0.36f, 1.0f) : Mod::DIM;
    std::string line1 = ResultText(r);
    const std::string where = WhereText(r);
    if (!where.empty()) line1 += "  /  " + where;
    Mod::Txt(line1, leftX, cy - 9.0f, UiKit::FitFontPx(line1, 15.0f, leftW, 10.0f), Mod::Alpha(ResultColor(r, lit), a), TextAlignment::Left, leftW);
    const std::string line2 = FormatDate(r.time, false) + "  /  " + ModeName(r.mode);
    Mod::Txt(line2, leftX, cy + 11.0f, UiKit::FitFontPx(line2, 11.0f, leftW, 8.0f), Mod::Alpha(sub, a), TextAlignment::Left, leftW);

    const float rightX = LIST_X + LIST_W - 16.0f + s;
    const std::string score = std::to_string(r.score);
    GameMenuUi::DrawHeavyText(score, rightX, cy - 9.0f, UiKit::FitFontPx(score, 19.0f, 112.0f, 11.0f), Mod::Alpha(lit ? Mod::GOLD_ON_LIGHT : Mod::GOLD, a),
                              TextAlignment::Right, 112.0f);
    // (the time and the rest are in the run itself: ENTER / click)
    Mod::Txt(Loc::T("OPEN"), rightX, cy + 11.0f, 11.0f, Mod::Alpha(sub, a * (lit ? 0.9f : 0.0f)), TextAlignment::Right, 112.0f);
    (void)ink;
}

bool HoveredRow(int rowOnPage)
{
    const float top = LIST_Y + LIST_STEP * static_cast<float>(rowOnPage);
    return GameMenuUi::IsMouseInsideRect(NormX(LIST_X), NormY(top), NormX(LIST_X + LIST_W), NormY(top + LIST_H));
}

constexpr float ARROW_LEFT_X = LIST_X + LIST_W - 96.0f;
constexpr float ARROW_RIGHT_X = LIST_X + LIST_W - 10.0f;

bool OverArrow(float centerX)
{
    return GameMenuUi::IsMouseInsideRect(NormX(centerX - 14.0f), NormY(LIST_HEAD_Y - 14.0f), NormX(centerX + 14.0f), NormY(LIST_HEAD_Y + 14.0f));
}

void DrawListHeader(int runs, int pages)
{
    const float a = RowA(LIST_HEAD_Y - 12.0f);
    const float s = RowS(LIST_HEAD_Y - 12.0f);
    GameMenuUi::SectionTitle(Loc::T("RUN LOG"), LIST_X + s, LIST_HEAD_Y, LIST_W - 120.0f, a);
    if (runs <= 0) return;
    const std::string page = std::to_string(g_Page + 1) + " / " + std::to_string(pages);
    Mod::Txt(page, LIST_X + LIST_W - 52.0f + s, LIST_HEAD_Y, 13.0f, Mod::Alpha(Mod::WHITE, a), TextAlignment::Center, 70.0f);
    if (pages > 1)
    {
        const float hotL = OverArrow(ARROW_LEFT_X) ? 1.0f : 0.0f;
        const float hotR = OverArrow(ARROW_RIGHT_X) ? 1.0f : 0.0f;
        GameMenuUi::DrawArrow(NormX(ARROW_LEFT_X + s), NormY(LIST_HEAD_Y), true, (0.62f + 0.38f * hotL) * a);
        GameMenuUi::DrawArrow(NormX(ARROW_RIGHT_X + s), NormY(LIST_HEAD_Y), false, (0.62f + 0.38f * hotR) * a);
    }
}

// ---- the career (outside): what a player wants to know at a glance --------------------------------------------------------------------
// the fastest cleared standard / challenge run of the log (seconds), -1 when there is none
int FastestClear(int runs)
{
    int best = -1;
    for (int i = 0; i < runs; ++i)
    {
        const RunRecord& r = GameSave::GetRunRecord(i);
        if (r.result == 1 && r.mode != RUN_MODE_PRACTICE && r.seconds > 0 && (best < 0 || r.seconds < best)) best = r.seconds;
    }
    return best;
}

// the last runs as bars: the height is the score, the colour the result (gold cleared, red defeated, grey left). The oldest is at the left.
void DrawTrend(int runs)
{
    constexpr float Y = 372.0f, BASE = 540.0f, MAXH = 120.0f, W = 22.0f, GAP = 6.0f;
    const int n = (std::min)(12, runs);
    if (n < 2) return;
    const float headA = RowA(Y);
    GameMenuUi::SectionTitle(Loc::T("RECENT RUNS"), STAT_X + RowS(Y), Y, STAT_W, headA);
    int top = 1;
    for (int k = 0; k < n; ++k) top = (std::max)(top, GameSave::GetRunRecord(k).score);
    for (int k = 0; k < n; ++k)
    {
        const RunRecord& r = GameSave::GetRunRecord(n - 1 - k);
        const float a = RowA(BASE - 40.0f);
        const float h = (std::max)(6.0f, MAXH * static_cast<float>(r.score) / static_cast<float>(top));
        const bool newest = k == n - 1;
        Color c = r.result == 1 ? Mod::GOLD : (r.result == 0 ? Mod::DANGER_RED : Mod::DIM);
        const float x = STAT_X + static_cast<float>(k) * (W + GAP) + RowS(BASE - 40.0f);
        GameMenuUi::Plate(x, BASE - h, W, h, Mod::Alpha(c, (newest ? 0.95f : 0.55f) * a));
        if (newest) GameMenuUi::SeamDiamond(x + W * 0.5f, BASE + 14.0f, 4.0f, a);
    }
    UiKit::Fill(STAT_X, BASE + 1.0f, STAT_W, 1.0f, Rgba(1.0f, 1.0f, 1.0f, 0.18f * RowA(BASE - 40.0f)));
}

// ---- one run, opened (inside) ---------------------------------------------------------------------------------------------------------
// the left column: the numbers of the run, the same rows as the career; the right column: how it ended, the score and the modules with their names
void DrawView(const RunRecord& r, int index, int runs)
{
    const float t = static_cast<float>(g_ViewT);
    auto in = [&](float delay) { return GameMenuUi::EaseOut(GameMenuUi::Clamp01((t - delay) / 10.0f)); };
    // left: six rows
    const std::string where = WhereText(r);
    const struct
    {
        const char* key;
        std::string value;
        Color color;
        bool important;
    } stats[6] = {{"TIME", FormatTime(r.seconds), Mod::WHITE, true},
                  {"BEST STREAK", std::to_string(r.maxCombo), Rgba(0.70f, 0.52f, 1.00f, 1.0f), false},
                  {"ABSORBS", std::to_string(r.absorbs), Rgba(0.48f, 0.82f, 1.00f, 1.0f), false},
                  {"HITS TAKEN", std::to_string(r.hits), r.hits == 0 ? Mod::GOLD : Mod::WHITE, false},
                  {"SHARDS", r.shards > 0 ? "+" + std::to_string(r.shards) : std::string("-"), r.shards > 0 ? Mod::GOLD : Mod::DIM, false},
                  {"MODULES", std::to_string(static_cast<int>(Meta::ModulesFromCsv(r.modules).size())), Mod::WHITE, false}};
    for (int i = 0; i < 6; ++i) DrawStat(stats[i].key, stats[i].value, i, stats[i].color, stats[i].important);
    // which run of how many, with the arrows that go to the neighbours
    {
        const float y = STAT_Y + STAT_STEP * 6.0f + 18.0f;
        const float a = RowA(y);
        const std::string n = std::to_string(index + 1) + " / " + std::to_string(runs);
        Mod::Txt(n, STAT_X + STAT_W * 0.5f + RowS(y), y, 14.0f, Mod::Alpha(Mod::WHITE, a), TextAlignment::Center, 120.0f);
        GameMenuUi::DrawArrow(NormX(STAT_X + STAT_W * 0.5f - 56.0f), NormY(y), true, (runs > 1 ? 0.7f : 0.2f) * a);
        GameMenuUi::DrawArrow(NormX(STAT_X + STAT_W * 0.5f + 56.0f), NormY(y), false, (runs > 1 ? 0.7f : 0.2f) * a);
    }
    // right: the ending
    const float x0 = LIST_X, w = LIST_W;
    const std::string result = ResultText(r);
    const float a1 = in(0.0f);
    GameMenuUi::DrawHeavyText(result, x0 + (1.0f - a1) * 40.0f, 188.0f, 34.0f, Mod::Alpha(ResultColor(r, false), a1), TextAlignment::Left, w);
    if (!where.empty()) Mod::Txt(where, x0 + (1.0f - a1) * 40.0f + 2.0f, 220.0f, 16.0f, Mod::Alpha(Mod::WHITE, 0.9f * a1), TextAlignment::Left, w);
    const float a2 = in(3.0f);
    Mod::Txt(ModeName(r.mode) + "   /   " + FormatDate(r.time, true), x0 + (1.0f - a2) * 40.0f + 2.0f, where.empty() ? 222.0f : 244.0f, 13.0f,
             Mod::Alpha(ModeColor(r.mode), a2), TextAlignment::Left, w);
    const float a3 = in(5.0f);
    const std::string score = std::to_string(r.score);
    GameMenuUi::DrawHeavyText(score, x0 + w - 8.0f + 3.0f, 196.0f + 3.0f, 44.0f, GameMenuUi::Accent(0.55f * a3), TextAlignment::Right, w);
    GameMenuUi::DrawHeavyText(score, x0 + w - 8.0f, 196.0f, 44.0f, Mod::Alpha(Mod::GOLD, a3), TextAlignment::Right, w);
    UiKit::Fill(x0, 276.0f, w, 1.0f, Rgba(1.0f, 1.0f, 1.0f, 0.22f * in(6.0f)));
    // the modules: an icon and the name, two columns
    const float a4 = in(7.0f);
    GameMenuUi::SectionTitle(Loc::T("MODULES"), x0, 300.0f, w, a4);
    const std::vector<int> modules = Meta::ModulesFromCsv(r.modules);
    if (modules.empty())
    {
        Mod::Txt(Loc::T("NO MODULES"), x0 + 4.0f, 338.0f, 14.0f, Mod::Alpha(Mod::DIM, a4), TextAlignment::Left, w);
        return;
    }
    constexpr float COL_W = 214.0f, ROW_H = 38.0f, ICON = 26.0f;
    for (size_t k = 0; k < modules.size() && k < 16; ++k)
    {
        const int id = modules[k];
        const float ka = in(8.0f + static_cast<float>(k) * 1.5f);
        const float x = x0 + static_cast<float>(k % 2) * (COL_W + 12.0f) + (1.0f - ka) * 24.0f;
        const float y = 322.0f + static_cast<float>(k / 2) * ROW_H;
        const Color tint = Mod::VerbColor(Mod::INFO[id].verb);
        GameMenuUi::Panel(x, y, COL_W, ROW_H - 6.0f, 0.0f, Rgba(0.0f, 0.0f, 0.0f, 0.42f * ka), Rgba(1.0f, 1.0f, 1.0f, 0.0f));
        UiKit::Fill(x, y, 3.0f, ROW_H - 6.0f, Mod::Alpha(tint, ka));
        Mod::DrawIcon(id, x + 10.0f, y + (ROW_H - 6.0f - ICON) * 0.5f, ICON, Mod::Alpha(tint, ka));
        const std::string name = Loc::T(Mod::INFO[id].name);
        Mod::Txt(name, x + 46.0f, y + (ROW_H - 6.0f) * 0.5f, UiKit::FitFontPx(name, 13.0f, COL_W - 56.0f, 9.0f), Mod::Alpha(Mod::WHITE, ka), TextAlignment::Left, COL_W - 50.0f);
    }
}

void DrawKeyHints(int mode) // 0 list, 1 list without runs, 2 one run
{
    struct Hint
    {
        const char* key;
        const char* text;
    };
    const Hint hints[3] = {{"ARROWS", "SELECT / PAGE"}, {"ENTER", "OPEN"}, {"ESC", "BACK"}};
    const Hint viewHints[2] = {{"ARROWS", "OTHER RUNS"}, {"ESC", "BACK"}};
    float x = STAT_X;
    const float a = GameMenuUi::Clamp01(GameMenuUi::Entrance(14.0f, 12.0f));
    auto one = [&](const Hint& h)
    {
        const float capW = UiKit::KeyCap(h.key, x, 664.0f, 26.0f, Rgba(0.85f, 0.92f, 1.0f, 0.85f * a));
        const std::string label = Loc::T(h.text);
        Mod::Txt(label, x + capW + 10.0f, 677.0f, 14.0f, Mod::Alpha(Mod::WHITE, a), TextAlignment::Left, 220.0f);
        x += capW + 10.0f + UiKit::TextWidth(label, 14.0f) + 40.0f;
    };
    if (mode == 2)
    {
        for (const Hint& h : viewHints) one(h);
    }
    else
    {
        for (int i = mode == 1 ? 2 : 0; i < 3; ++i) one(hints[i]);
    }
}
}

void RecordsScene::Open()
{
    g_Selected = -1;
    g_Page = 0;
    g_View = -1;
}

void RecordsScene::Update()
{
    GameBackground::Update();
    ++g_ViewT;
    const bool back = GameInput::IsTriggered(VK_ESCAPE) || GameInput::IsTriggered(VK_BACK) || GameInput::IsTriggered(VK_RBUTTON);
    if (back && g_View >= 0)
    {
        // a run is open: back to the list (the run stays highlighted)
        g_Selected = g_View;
        g_View = -1;
        GameAudio::Play(GameSound::MenuDecide);
        return;
    }
    if (back)
    {
        GameAudio::Play(GameSound::MenuDecide);
        GameFlow::ReturnToTitle();
        return;
    }
    const int runs = GameSave::GetRunRecordCount();
    if (runs <= 0)
    {
        return;
    }
    if (g_View >= 0)
    {
        // the neighbours of the open run
        int step = 0;
        if (GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D') || GameInput::IsTriggered(VK_DOWN) || GameInput::IsTriggered('S')) step = 1;
        if (GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A') || GameInput::IsTriggered(VK_UP) || GameInput::IsTriggered('W')) step = -1;
        if (GameInput::IsTriggered(VK_LBUTTON))
        {
            const float y = STAT_Y + STAT_STEP * 6.0f + 18.0f;
            if (GameMenuUi::IsMouseInsideRect(NormX(STAT_X + STAT_W * 0.5f - 78.0f), NormY(y - 16.0f), NormX(STAT_X + STAT_W * 0.5f - 34.0f), NormY(y + 16.0f))) step = -1;
            if (GameMenuUi::IsMouseInsideRect(NormX(STAT_X + STAT_W * 0.5f + 34.0f), NormY(y - 16.0f), NormX(STAT_X + STAT_W * 0.5f + 78.0f), NormY(y + 16.0f))) step = 1;
        }
        if (step != 0 && runs > 1)
        {
            g_View = (g_View + step + runs) % runs;
            g_ViewT = 0;
            GameAudio::Play(GameSound::MenuAdjust);
        }
        g_View = (std::max)(0, (std::min)(g_View, runs - 1));
        return;
    }
    const int pages = PageCount(runs);
    g_Page = (std::max)(0, (std::min)(g_Page, pages - 1));
    const int before = g_Selected;
    const int pageFirst = g_Page * ROWS_PER_PAGE;
    const int pageCount = (std::min)(ROWS_PER_PAGE, runs - pageFirst);

    const bool down = GameInput::IsTriggered(VK_DOWN) || GameInput::IsTriggered('S');
    const bool up = GameInput::IsTriggered(VK_UP) || GameInput::IsTriggered('W');
    const bool right = GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D') || GameInput::IsTriggered(VK_NEXT);
    const bool left = GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A') || GameInput::IsTriggered(VK_PRIOR);
    int pageStep = 0;
    if (down)
        g_Selected = g_Selected < 0 ? pageFirst : (g_Selected + 1) % runs;
    else if (up)
        g_Selected = g_Selected < 0 ? pageFirst + pageCount - 1 : (g_Selected + runs - 1) % runs;
    else if (right)
        pageStep = 1;
    else if (left)
        pageStep = -1;

    // the arrows next to the page number
    if (GameInput::IsTriggered(VK_LBUTTON) && pages > 1)
    {
        if (OverArrow(ARROW_LEFT_X)) pageStep = -1;
        if (OverArrow(ARROW_RIGHT_X)) pageStep = 1;
    }
    if (pageStep != 0 && pages > 1)
    {
        const int rowOnPage = g_Selected >= 0 ? g_Selected - pageFirst : -1;
        g_Page = (g_Page + pageStep + pages) % pages;
        const int newFirst = g_Page * ROWS_PER_PAGE;
        const int newCount = (std::min)(ROWS_PER_PAGE, runs - newFirst);
        g_Selected = rowOnPage >= 0 ? newFirst + (std::min)(rowOnPage, newCount - 1) : -1;
        GameAudio::Play(GameSound::MenuAdjust);
    }
    if (g_Selected >= 0)
    {
        g_Page = g_Selected / ROWS_PER_PAGE; // an arrow key walked into the next page
    }

    // the mouse: pointing at a run highlights it, leaving the rows clears the highlight
    int hovered = -1;
    for (int i = 0; i < ROWS_PER_PAGE && g_Page * ROWS_PER_PAGE + i < runs; ++i)
    {
        if (HoveredRow(i)) hovered = g_Page * ROWS_PER_PAGE + i;
    }
    if (GameInput::MouseMoved() || (GameInput::IsTriggered(VK_LBUTTON) && hovered >= 0))
    {
        if (hovered >= 0)
            g_Selected = hovered;
        else if (GameInput::MouseMoved())
            g_Selected = -1;
    }
    if (g_Selected != before && g_Selected >= 0 && pageStep == 0)
    {
        GameAudio::Play(GameSound::MenuMove);
    }
    // ENTER / SPACE / a click on a run opens it
    if (g_Selected >= 0 && (GameInput::IsTriggered(VK_RETURN) || GameInput::IsTriggered(VK_SPACE) || (GameInput::IsTriggered(VK_LBUTTON) && hovered == g_Selected)))
    {
        g_View = g_Selected;
        g_ViewT = 0;
        GameAudio::Play(GameSound::MenuDecide);
    }
}

void RecordsScene::Draw()
{
    Renderer2D_ClearCameraOffset();
    const GameRecordData& recordData = GameSave::GetRecordData();
    GameMenuUi::DrawBackdrop(0.55f);
    GameMenuUi::DrawPageHeader(Loc::T("RECORDS"));
    const int runs = GameSave::GetRunRecordCount();
    if (g_View >= 0 && g_View < runs)
    {
        DrawView(GameSave::GetRunRecord(g_View), g_View, runs);
        DrawKeyHints(2);
        return;
    }

    // the career: four numbers and the trend of the last runs
    const int fastest = FastestClear(runs);
    DrawStat("HIGH SCORE", std::to_string(recordData.highScore), 0, Mod::GOLD, true);
    DrawStat("BEST STREAK", std::to_string(recordData.maxCombo), 1, Rgba(0.70f, 0.52f, 1.00f, 1.0f), true);
    DrawStat("CLEARS", std::to_string(recordData.clearCount) + " / " + std::to_string(recordData.playCount), 2, Rgba(0.34f, 1.00f, 0.62f, 1.0f), false);
    DrawStat("FASTEST CLEAR", fastest >= 0 ? FormatTime(fastest) : std::string("-"), 3, fastest >= 0 ? Rgba(0.48f, 0.82f, 1.00f, 1.0f) : Mod::DIM, false);
    DrawTrend(runs);

    const int pages = PageCount(runs);
    g_Page = (std::max)(0, (std::min)(g_Page, pages - 1));
    DrawListHeader(runs, pages);
    if (runs <= 0)
    {
        const float a = RowA(LIST_Y);
        UiKit::Fill(LIST_X, LIST_Y + 22.0f, LIST_W, 1.0f, Rgba(1.0f, 1.0f, 1.0f, 0.22f * a));
        Mod::Txt(Loc::T("NO RUNS YET"), LIST_X + LIST_W * 0.5f, LIST_Y + 62.0f, 15.0f, Mod::Alpha(Mod::DIM, a), TextAlignment::Center, LIST_W);
        DrawKeyHints(1);
        return;
    }
    for (int i = 0; i < ROWS_PER_PAGE; ++i)
    {
        const int index = g_Page * ROWS_PER_PAGE + i;
        if (index >= runs) break;
        DrawRunRow(i, index);
    }
    DrawKeyHints(0);
}
