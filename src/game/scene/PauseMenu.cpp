#include "game/scene/PauseMenu.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/GameFlow.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/scene/SettingsScene.h"
#include "game/GameWave.h"
#include "rogue/Modules.h"
#include "ui/GameMenuUi.h"
#include "ui/TextTheme.h"
#include <Windows.h>
#include <string>
namespace
{
constexpr int PAUSE_MENU_ITEM_COUNT = 4;
constexpr float PAUSE_MENU_LEFT = -0.40f;
constexpr float PAUSE_MENU_RIGHT = 0.40f;
constexpr float PAUSE_MENU_HEIGHT = 0.120f;
constexpr float PAUSE_MENU_Y[PAUSE_MENU_ITEM_COUNT] = {0.20f, 0.05f, -0.15f, -0.30f};
bool g_IsPaused = false;
int g_SelectedPauseMenuItem = -1;  // -1: nothing highlighted
bool g_IsPauseSettingsOpen = false;
bool IsPointInsidePauseMenuItem(float mouseX, float mouseY, int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= PAUSE_MENU_ITEM_COUNT)
    {
        return false;
    }
    const float centerY = PAUSE_MENU_Y[itemIndex];
    const float top = centerY + PAUSE_MENU_HEIGHT * 0.5f;
    const float bottom = centerY - PAUSE_MENU_HEIGHT * 0.5f;
    return mouseX >= PAUSE_MENU_LEFT && mouseX <= PAUSE_MENU_RIGHT && mouseY <= top && mouseY >= bottom;
}
int GetHoveredPauseMenuIndex()
{
    if (!GameInput::IsMouseInsideWindow())
    {
        return -1;
    }
    const float mouseX = GameInput::GetMouseX();
    const float mouseY = GameInput::GetMouseY();
    for (int itemIndex = 0; itemIndex < PAUSE_MENU_ITEM_COUNT; itemIndex++)
    {
        if (IsPointInsidePauseMenuItem(mouseX, mouseY, itemIndex))
        {
            return itemIndex;
        }
    }
    return -1;
}
void DrawPauseMenuItem(const std::string& text, int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= PAUSE_MENU_ITEM_COUNT)
    {
        return;
    }
    const bool isSelected = g_SelectedPauseMenuItem == itemIndex;
    const bool isDangerItem = itemIndex == 2 || itemIndex == 3;
    const float centerY = PAUSE_MENU_Y[itemIndex];
    const float top = centerY + PAUSE_MENU_HEIGHT * 0.5f;
    const float bottom = centerY - PAUSE_MENU_HEIGHT * 0.5f;
    GameMenuUi::RowStyle rowStyle = GameMenuUi::RowStyle::Normal;
    if (isSelected)
    {
        rowStyle = isDangerItem ? GameMenuUi::RowStyle::DangerSelected : GameMenuUi::RowStyle::Selected;
    }
    else if (isDangerItem)
    {
        rowStyle = GameMenuUi::RowStyle::Danger;
    }
    GameMenuUi::DrawRow(PAUSE_MENU_LEFT, top, PAUSE_MENU_RIGHT, bottom, rowStyle, true);
    const bool lit = GameMenuUi::SelectionAmount(PAUSE_MENU_LEFT, top) > 0.5f;
    TextStyle textStyle = TextTheme::GetStyle(lit ? TextRole::ButtonSelected : TextRole::Button);
    textStyle.size = 0.0125f;
    textStyle.alignment = TextAlignment::Center;
    textStyle.layoutWidth = PAUSE_MENU_RIGHT - PAUSE_MENU_LEFT;
    textStyle.shadow = false;
    if (!lit && isDangerItem)
    {
        textStyle.red = 1.00f;
        textStyle.green = 0.55f;
        textStyle.blue = 0.50f;
        textStyle.alpha = 0.92f;
    }
    if (lit)
    {
        textStyle.red = 0.05f;
        textStyle.green = 0.06f;
        textStyle.blue = 0.09f;
        textStyle.alpha = 1.00f;
    }
    textStyle.alpha *= GameMenuUi::RowAlpha(top);
    TextRenderer::QueueText(text, PAUSE_MENU_LEFT + GameMenuUi::RowShift(top), centerY + 0.018f, textStyle);
}
// the run so far, quiet, at the bottom right (the seam rule, the wave, the score, the HP)
void DrawRunInfo()
{
    const float a = GameMenuUi::Entrance(10.0f, 12.0f);
    if (a <= 0.0f) return;
    const float r = 1200.0f;
    GameMenuUi::SeamRule(r - 260.0f, 520.0f, 260.0f, a, a);
    UiKit::Text(Loc::Fmt("WAVE {0}", GameWave::GetCurrentWave()), r, 540.0f, 12.0f, UiKit::Rgba(0.80f, 0.82f, 0.88f, 0.85f * a), TextAlignment::Right,
                300.0f, false);
    GameMenuUi::DrawHeavyText(std::to_string(GameState::score), r + 2.0f, 576.0f + 1.6f, 36.0f, GameMenuUi::Accent(0.9f * a), TextAlignment::Right, 500.0f);
    GameMenuUi::DrawHeavyText(std::to_string(GameState::score), r, 576.0f, 36.0f, GameMenuUi::Ivory(a), TextAlignment::Right, 500.0f);
    const int maxHp = Mod::MaxHp() > 0 ? Mod::MaxHp() : 1;
    const int hpNow = GameState::playerHp > 0 ? GameState::playerHp : 0;
    const float hp = static_cast<float>(hpNow) / static_cast<float>(maxHp);
    UiKit::Fill(r - 260.0f, 612.0f, 260.0f, 1.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.25f * a));
    UiKit::Fill(r - 260.0f, 608.0f, 260.0f * hp, 4.0f, hp < 0.3f ? UiKit::Rgba(1.0f, 0.25f, 0.20f, a) : GameMenuUi::Ivory(0.9f * a));
    UiKit::Text(Loc::T("HP") + std::string("  ") + std::to_string(hpNow), r, 628.0f, 11.0f, UiKit::Rgba(0.75f, 0.76f, 0.80f, 0.8f * a),
                TextAlignment::Right, 200.0f, false);
}

void DrawPauseMenu()
{
    Renderer2D_ClearCameraOffset();
    Renderer2D_DrawQuad(-1.00f, 1.00f, 1.00f, -1.00f, 0.00f, 0.005f, 0.025f, 0.64f);
    GameMenuUi::DrawBackdrop(0.34f);
    GameMenuUi::DrawPageHeader(Loc::T("PAUSE"));
    DrawPauseMenuItem(Loc::T("RESUME"), 0);
    DrawPauseMenuItem(Loc::T("SETTINGS"), 1);
    DrawPauseMenuItem(Loc::T("RESTART"), 2);
    DrawPauseMenuItem(Loc::T("TITLE"), 3);
    TextStyle helpStyle = TextTheme::GetStyle(TextRole::Help);
    helpStyle.size = 0.0085f;
    helpStyle.red = 0.56f;
    helpStyle.green = 0.64f;
    helpStyle.blue = 0.78f;
    helpStyle.alpha = 0.82f;
    helpStyle.alignment = TextAlignment::Center;
    helpStyle.layoutWidth = 0.70f;
    TextRenderer::QueueText(Loc::T("ESC TO RESUME"), -0.35f, -0.45f, helpStyle);
    DrawRunInfo();
}
}
void PauseMenu::Update()
{
    if (g_IsPauseSettingsOpen)
    {
        // the whole settings page (SettingsScene), over the game: ESC / BACK come back here
        if (SettingsScene::UpdateEmbedded())
        {
            g_IsPauseSettingsOpen = false;
            g_SelectedPauseMenuItem = 1;
            GameMenuUi::StartEntrance();
        }
        return;
    }
    if (GameInput::IsTriggered(VK_ESCAPE) || GameInput::IsTriggered(VK_BACK) || GameInput::IsTriggered(VK_RBUTTON))
    {
        g_IsPaused = false;
        g_IsPauseSettingsOpen = false;
        GameAudio::Play(GameSound::MenuDecide);
        return;
    }
    const int hoveredIndex = GetHoveredPauseMenuIndex();
    const int activated = GameMenuUi::UpdateMenu(g_SelectedPauseMenuItem, PAUSE_MENU_ITEM_COUNT, hoveredIndex, false, GameSound::MenuCursor);
    if (activated < 0)
    {
        return;
    }
    GameAudio::Play(GameSound::MenuDecide);
    switch (activated)
    {
    case 0:
        g_IsPaused = false;
        break;
    case 1:
        g_IsPauseSettingsOpen = true;
        SettingsScene::OpenEmbedded();
        break;
    case 2:
        GameFlow::StartNewGame();
        break;
    case 3:
        GameFlow::ReturnToTitle();
        break;
    default:
        g_IsPaused = false;
        break;
    }
}

void PauseMenu::Draw()
{
    if (g_IsPauseSettingsOpen)
    {
        SettingsScene::DrawEmbedded();
    }
    else
    {
        DrawPauseMenu();
    }
}
bool PauseMenu::IsOpen()
{
    return g_IsPaused;
}
void PauseMenu::Open()
{
    g_IsPaused = true;
    g_SelectedPauseMenuItem = -1;
    GameMenuUi::SetBackdropPolarity(GameState::player.polarity == Polarity::Red, true); // (the seam of the pause: the colour of the ship)
    SettingsScene::CloseEmbedded(); // (opened again by the window being switched while the settings were open)
    g_IsPauseSettingsOpen = false;
    GameMenuUi::StartEntrance(); // the rows of the menu come in one after the other
}
void PauseMenu::Reset()
{
    g_IsPaused = false;
    g_SelectedPauseMenuItem = -1;
    SettingsScene::CloseEmbedded();
    g_IsPauseSettingsOpen = false;
}


