#include "game/scene/GameOverScene.h"

#include "core/GameInput.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/GameBackground.h"
#include "game/GameFlow.h"
#include "rogue/Modules.h"
#include "ui/GameMenuUi.h"
#include "ui/TextTheme.h"
#include "ui/Wordmark.h"

#include <Windows.h>
#include <cmath>

namespace
{
int g_SelectedGameOverMenuItem = -1; // -1: nothing highlighted
int g_Timer = 0;

constexpr float GAME_OVER_MENU_Y[2] = {-0.36f, -0.52f};

void ExecuteGameOverMenuItem(int menuItem)
{
    // 0 = play again, 1 = title. There is no free checkpoint retry any more.
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
        if (GameMenuUi::IsMouseInsideRect(GameMenuUi::RESULT_MENU_LEFT, GAME_OVER_MENU_Y[i] + GameMenuUi::RESULT_MENU_HEIGHT * 0.5f,
                                          GameMenuUi::RESULT_MENU_RIGHT, GAME_OVER_MENU_Y[i] - GameMenuUi::RESULT_MENU_HEIGHT * 0.5f))
        {
            return i;
        }
    }
    return -1;
}
}

void GameOverScene::Update()
{
    GameBackground::Update();
    Renderer2D_ClearCameraOffset();
    ++g_Timer;

    // a short lock so that a mashed SPACE / click at the moment of death does not skip the result screen
    if (Mod::ConsumeGameOverLock())
    {
        return;
    }

    const int activated = GameMenuUi::UpdateMenu(g_SelectedGameOverMenuItem, 2, HoveredItem());
    if (activated >= 0)
    {
        ExecuteGameOverMenuItem(activated);
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

void GameOverScene::Draw()
{
    Renderer2D_ClearCameraOffset();
    Renderer2D_DrawQuad(-1.00f, 1.00f, 1.00f, -1.00f, 0.030f, 0.002f, 0.008f, 0.58f);
    GameMenuUi::DrawBackdrop(0.50f);

    const float p = GameMenuUi::EaseOut(static_cast<float>(g_Timer) / 14.0f);
    const float shake = g_Timer < 14 ? std::sin(static_cast<float>(g_Timer) * 2.7f) * (1.0f - static_cast<float>(g_Timer) / 14.0f) * 8.0f : 0.0f;

    // P5: a red band slams across behind the title, black over it, halftone at its end
    {
        const float b = GameMenuUi::EaseOut(static_cast<float>(g_Timer) / 10.0f);
        const float x1 = -40.0f + 1380.0f * b;
        GameMenuUi::Band(-40.0f, x1, 640.0f, 146.0f, 116.0f, -0.05f, GameMenuUi::Accent(0.95f));
        GameMenuUi::Band(-40.0f, x1 * 0.92f, 640.0f, 82.0f, 1.5f, -0.05f, GameMenuUi::Counter(0.85f * b)); // (the glass line of the seam)
        GameMenuUi::Band(-40.0f, x1 - 30.0f, 640.0f, 140.0f, 96.0f, -0.05f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.92f));
        GameMenuUi::Halftone(860.0f, 1280.0f, 82.0f, -0.05f, 15.0f, 5.5f, GameMenuUi::Accent(0.6f * b));
    }
    // the title is cut across the page: ivory text on a hard red offset
    GameMenuUi::DrawCutText(Loc::T("GAME OVER"), 84.0f - (1.0f - p) * 140.0f + shake, 148.0f, 92.0f, GameMenuUi::Accent(1.0f), 7.0f, p);
    GameMenuUi::Slab(380.0f, 214.0f, 640.0f, 3.0f, 0.0f, GameMenuUi::Signal(0.9f * p));

    // where the run ended, how long it took, which modules were used
    Mod::DrawGameOverSummary(GameMenuUi::EaseOut(static_cast<float>(g_Timer - 8) / 14.0f));

    GameMenuUi::DrawResultMenuItem(Loc::T("PLAY AGAIN"), GAME_OVER_MENU_Y[0], g_SelectedGameOverMenuItem == 0, false);
    GameMenuUi::DrawResultMenuItem(Loc::T("TITLE"), GAME_OVER_MENU_Y[1], g_SelectedGameOverMenuItem == 1, true);

    UiKit::Text(Loc::T("ENTER OR CLICK"), 84.0f, 612.0f, 12.0f, UiKit::Rgba(0.64f, 0.54f, 0.62f, 0.78f), TextAlignment::Left, 400.0f, false);
    Wordmark::DrawBadge(1240.0f, 694.0f, 20.0f, 0.80f * GameMenuUi::Clamp01(static_cast<float>(g_Timer - 20) / 14.0f)); // (the wordmark, small: the corner of the page)
}

void GameOverScene::ResetSelection()
{
    g_SelectedGameOverMenuItem = -1;
    g_Timer = 0;
    GameMenuUi::SetBackdropPolarity(true);
}




