#include "game/scene/GameClearScene.h"

#include "core/GameInput.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/GameBackground.h"
#include "game/GameFlow.h"
#include "game/GameSave.h"
#include "game/GameState.h"
#include "rogue/Modules.h"
#include "ui/GameMenuUi.h"
#include "ui/TextTheme.h"
#include "ui/Wordmark.h"
#include "ui/UiKit.h"

#include <Windows.h>
#include <string>

namespace
{
int g_SelectedGameClearMenuItem = -1; // -1: nothing highlighted

constexpr float GAME_CLEAR_MENU_Y[2] = {-0.36f, -0.52f};

char GetClearRank()
{
    if (GameState::score >= 21000 && GameState::maxCombo >= 20)
    {
        return 'S';
    }

    if (GameState::score >= 14000 && GameState::maxCombo >= 10)
    {
        return 'A';
    }

    if (GameState::score >= 7000)
    {
        return 'B';
    }

    return 'C';
}

// dark variants of the rank colours: the letter sits on an ivory plate
void GetClearRankColor(char rank, float& red, float& green, float& blue)
{
    red = 0.30f;
    green = 0.34f;
    blue = 0.44f;

    if (rank == 'S')
    {
        red = 0.80f;
        green = 0.42f;
        blue = 0.00f;
    }
    else if (rank == 'A')
    {
        red = 0.58f;
        green = 0.16f;
        blue = 0.82f;
    }
    else if (rank == 'B')
    {
        red = 0.05f;
        green = 0.50f;
        blue = 0.76f;
    }
}

void ExecuteGameClearMenuItem(int menuItem)
{
    if (menuItem == 0)
    {
        GameFlow::StartNewGame();
        return;
    }

    GameFlow::ReturnToTitle();
}

int HoveredItem()
{
    for (int i = 0; i < 2; ++i)
    {
        if (GameMenuUi::IsMouseInsideRect(GameMenuUi::RESULT_MENU_LEFT, GAME_CLEAR_MENU_Y[i] + GameMenuUi::RESULT_MENU_HEIGHT * 0.5f,
                                          GameMenuUi::RESULT_MENU_RIGHT, GAME_CLEAR_MENU_Y[i] - GameMenuUi::RESULT_MENU_HEIGHT * 0.5f))
        {
            return i;
        }
    }
    return -1;
}

float Reveal(int delay, float frames)
{
    return GameMenuUi::EaseOut(static_cast<float>(GameState::resultDisplayTimer - delay) / frames);
}

// A strip of the result list: slanted dark plate, label on the left, value on the right.
void DrawStat(const std::string& label, const std::string& value, float x, float y, float w, UiKit::Color valueColor, float reveal)
{
    const float h = 62.0f;
    const float dx = (1.0f - reveal) * 90.0f;
    GameMenuUi::Plate(x + dx, y, w, h, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.60f * reveal));
    GameMenuUi::PlateLine(x + dx, y, w, h, GameMenuUi::Ivory(0.30f * reveal));
    GameMenuUi::Slab(x + dx + 22.0f, y + h * 0.5f, 4.0f, h * 0.55f, 1.0f, GameMenuUi::Signal(reveal));
    UiKit::Text(label, x + dx + 42.0f, y + h * 0.5f, 15.0f, UiKit::Rgba(0.66f, 0.72f, 0.84f, reveal), TextAlignment::Left, 240.0f, false);
    UiKit::Text(value, x + dx + w - 34.0f, y + h * 0.5f, 34.0f, UiKit::Rgba(valueColor.r, valueColor.g, valueColor.b, reveal),
                TextAlignment::Right, 260.0f, false);
}
}

void GameClearScene::Update()
{
    GameBackground::Update();
    Renderer2D_ClearCameraOffset();
    GameState::resultDisplayTimer++;

    const int activated = GameMenuUi::UpdateMenu(g_SelectedGameClearMenuItem, 2, HoveredItem());
    if (activated >= 0)
    {
        ExecuteGameClearMenuItem(activated);
        return;
    }

    if (GameInput::IsTriggered('R') || GameInput::IsTriggered('N'))
    {
        GameFlow::StartNewGame();
        return;
    }

    if (GameInput::IsTriggered(VK_ESCAPE) || GameInput::IsTriggered(VK_BACK))
    {
        GameFlow::ReturnToTitle();
        return;
    }
}

void GameClearScene::Draw()
{
    Renderer2D_ClearCameraOffset();
    Renderer2D_DrawQuad(-1.00f, 1.00f, 1.00f, -1.00f, 0.000f, 0.010f, 0.040f, 0.50f);
    GameMenuUi::DrawBackdrop(0.55f);

    // shards earned in this run, next unlock
    Mod::DrawRunResult();

    const int timer = GameState::resultDisplayTimer;
    const char clearRank = GetClearRank();
    float rankRed = 1.00f;
    float rankGreen = 1.00f;
    float rankBlue = 1.00f;
    GetClearRankColor(clearRank, rankRed, rankGreen, rankBlue);
    if (clearRank == 'S' && (timer / 8) % 2 == 0)
    {
        rankRed = 0.95f;
        rankGreen = 0.58f;
        rankBlue = 0.04f;
    }

    // title: P5 - a blue band slams across behind it, black over it, halftone at its end
    const float titleReveal = Reveal(0, 14.0f);
    {
        const float b = Reveal(0, 10.0f);
        const float x1 = -40.0f + 1380.0f * b;
        GameMenuUi::Band(-40.0f, x1, 640.0f, 114.0f, 100.0f, -0.04f, GameMenuUi::Accent(0.95f));
        GameMenuUi::Band(-40.0f, x1 * 0.92f, 640.0f, 58.0f, 1.5f, -0.04f, GameMenuUi::Counter(0.85f * b)); // (the glass line of the seam)
        GameMenuUi::Band(-40.0f, x1 - 30.0f, 640.0f, 110.0f, 82.0f, -0.04f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.92f));
        GameMenuUi::Halftone(860.0f, 1280.0f, 64.0f, -0.04f, 15.0f, 5.5f, UiKit::Rgba(1.0f, 0.80f, 0.25f, 0.6f * b));
    }
    GameMenuUi::DrawCutText(Loc::T("GAME CLEAR"), 84.0f - (1.0f - titleReveal) * 140.0f, 124.0f, 84.0f, UiKit::Rgba(1.0f, 0.80f, 0.25f, 1.0f), 6.0f, titleReveal);

    // rank plate: black shadow, red offset, ivory plate, dark letter
    const float rankReveal = Reveal(6, 12.0f);
    const float rx = 84.0f;
    const float ry = 190.0f + (1.0f - rankReveal) * 30.0f;
    const float rw = 214.0f;
    const float rh = 258.0f;
    UiKit::Fill(rx + 12.0f, ry + 11.0f, rw, rh, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f * rankReveal));
    UiKit::Fill(rx + 6.0f, ry + 6.0f, rw, rh, GameMenuUi::Signal(rankReveal));
    UiKit::Fill(rx, ry, rw, rh, GameMenuUi::Ivory(rankReveal));
    UiKit::Text(Loc::T("RANK"), rx + 20.0f, ry + 24.0f, 15.0f, GameMenuUi::Ink(rankReveal), TextAlignment::Left, 160.0f, false);
    UiKit::Fill(rx + 20.0f, ry + 40.0f, 54.0f, 3.0f, GameMenuUi::Signal(rankReveal));
    // the rank: heavy, in its colour, on a hard black offset
    GameMenuUi::DrawHeavyText(std::string(1, clearRank), rx + rw * 0.5f + 6.0f, ry + rh * 0.55f + 4.8f, 176.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.85f * rankReveal),
                              TextAlignment::Center, rw);
    GameMenuUi::DrawHeavyText(std::string(1, clearRank), rx + rw * 0.5f, ry + rh * 0.55f, 176.0f, UiKit::Rgba(rankRed, rankGreen, rankBlue, rankReveal),
                              TextAlignment::Center, rw);

    // result list
    const float count = GameMenuUi::EaseOut(static_cast<float>(timer - 22) / 40.0f);
    const int shownScore = static_cast<int>(static_cast<float>(GameState::score) * count + 0.5f);
    const int shownCombo = static_cast<int>(static_cast<float>(GameState::maxCombo) * count + 0.5f);
    DrawStat(Loc::T("SCORE"), std::to_string(shownScore), 346.0f, 190.0f, 430.0f, UiKit::Rgba(1.0f, 0.82f, 0.18f, 1.0f), Reveal(14, 12.0f));
    DrawStat(Loc::T("MAX COMBO"), std::to_string(shownCombo), 346.0f, 266.0f, 430.0f, UiKit::Rgba(0.55f, 0.82f, 1.0f, 1.0f), Reveal(20, 12.0f));

    // a new best is marked
    const GameRecordData& record = GameSave::GetRecordData();
    if (GameState::score > 0 && GameState::score >= record.highScore && timer > 44)
    {
        const float a = GameMenuUi::Clamp01(static_cast<float>(timer - 44) / 10.0f);
        GameMenuUi::Plate(346.0f, 342.0f, 170.0f, 30.0f, UiKit::Rgba(1.0f, 0.80f, 0.30f, a));
        UiKit::Text(Loc::T("NEW BEST"), 346.0f + 85.0f, 357.0f, 13.0f, GameMenuUi::Ink(a), TextAlignment::Center, 170.0f, false);
    }

    // what the modules did (the same panel as the game over page)
    Mod::DrawModuleReport(430.0f, 384.0f, 470.0f, 292.0f, Reveal(34, 16.0f));
    GameMenuUi::DrawResultMenuItem(Loc::T("NEW RUN"), GAME_CLEAR_MENU_Y[0], g_SelectedGameClearMenuItem == 0, false);
    GameMenuUi::DrawResultMenuItem(Loc::T("TITLE"), GAME_CLEAR_MENU_Y[1], g_SelectedGameClearMenuItem == 1, false);

    UiKit::Text(Loc::T("ENTER OR CLICK"), 84.0f, 612.0f, 12.0f, UiKit::Rgba(0.56f, 0.64f, 0.78f, 0.78f), TextAlignment::Left, 400.0f, false);
    Wordmark::DrawBadge(1240.0f, 694.0f, 20.0f, 0.80f * GameMenuUi::Clamp01(static_cast<float>(timer - 20) / 14.0f)); // (the wordmark, small: the corner of the page)
}

void GameClearScene::ResetSelection()
{
    g_SelectedGameClearMenuItem = -1;
    GameMenuUi::SetBackdropPolarity(false);
}




