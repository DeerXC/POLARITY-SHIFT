#include "rogue/Modules.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "game/scene/TitleScene.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/ModulesDrawInternal.h"
#include "rogue/ModulesInternal.h"

#include <Windows.h>

// Hangar and mode selector on the title screen (logic).
namespace Mod
{
void HangarTileXY(int row, int col, float& x, float& y)
{
    x = HANGAR_X + static_cast<float>(col) * (HANGAR_TILE + HANGAR_GAP);
    y = HANGAR_Y + static_cast<float>(row) * (HANGAR_TILE + HANGAR_GAP);
}

bool HangarFind(int id, int& row, int& col)
{
    for (int r = 0; r < HANGAR_ROWS; ++r)
    {
        for (int c = 0; c < HANGAR_COLS; ++c)
        {
            if (HANGAR_LAYOUT[r][c] == id)
            {
                row = r;
                col = c;
                return true;
            }
        }
    }
    return false;
}

// moves the selection by one cell, skipping empty cells (wraps around)
void HangarMove(int dRow, int dCol)
{
    int row = 0, col = 0;
    if (g_HangarSel < 0)
    {
        // nothing selected yet: a key selects the next thing to work for (or the first tile)
        int missing = 0;
        const int next = Meta::NextTarget(missing);
        g_HangarSel = next >= 0 ? next : HANGAR_LAYOUT[0][0];
        return;
    }
    if (!HangarFind(g_HangarSel, row, col)) return;
    for (int step = 0; step < HANGAR_ROWS * HANGAR_COLS; ++step)
    {
        row = (row + dRow + HANGAR_ROWS) % HANGAR_ROWS;
        col = (col + dCol + HANGAR_COLS) % HANGAR_COLS;
        if (HANGAR_LAYOUT[row][col] >= 0)
        {
            g_HangarSel = HANGAR_LAYOUT[row][col];
            return;
        }
    }
}

void OpenHangar()
{
    Meta::Ensure();
    g_HangarOpen = true;
    g_HangarTimer = 0;
    g_HangarMessage = 0;
    g_HangarFlash = 0;
    g_HangarSel = -1; // nothing is highlighted until the mouse points at a tile or a key is pressed
    g_HangarSyn = -1;
    g_HangarSynFocus = false;
    GameAudio::Play(GameSound::MenuDecide);
}

// ENTER in the hangar: a module is free (there is nothing to do); a tactic is bought, or taken along (again: put back); a talent goes up one level
void UnlockSelected()
{
    const int id = g_HangarSel;
    if (id < COUNT) return;
    if (id < HANGAR_TALENT && Meta::IsUnlocked(id))
    {
        Meta::EquipTactic(id - HANGAR_TACTIC);
        g_HangarFlash = 24;
        g_HangarFlashId = id;
        GameAudio::Play(GameSound::MenuDecide);
        return;
    }
    if (Meta::Price(id) <= 0) return; // (a talent at its last level)
    if (Meta::TryUnlock(id))
    {
        g_HangarFlash = 40;
        g_HangarFlashId = id;
        GameAudio::Play(GameSound::Unlock);
        WriteLogRow("unlock", id < HANGAR_TALENT ? TACTICS[id - HANGAR_TACTIC].code : TALENTS[id - HANGAR_TALENT].code);
    }
    else
    {
        g_HangarMessage = 70;
    }
}

// LEFT / RIGHT (or a click on the selector) changes the mode while START GAME is highlighted
void UpdateModeSelector(bool mouseInside, float mx, float my)
{
    if (!TitleScene::IsStartGameSelected() || Meta::ModeCount() <= 1) return;

    int direction = 0;
    if (GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A'))
        direction = -1;
    else if (GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D'))
        direction = 1;
    else if (mouseInside && GameInput::IsTriggered(VK_LBUTTON))
    {
        const float left = MODE_X / 640.0f - 1.0f;
        const float right = (MODE_X + MODE_W) / 640.0f - 1.0f;
        const float top = 1.0f - (MODE_CY - MODE_H * 0.5f) / 360.0f;
        const float bottom = 1.0f - (MODE_CY + MODE_H * 0.5f) / 360.0f;
        if (mx >= left && mx <= right && my <= top && my >= bottom)
        {
            direction = (mx < (left + right) * 0.5f) ? -1 : 1;
        }
    }

    if (direction != 0)
    {
        Meta::CycleMode(direction);
        GameAudio::Play(GameSound::MenuCursor);
    }
}

void OpenHangarFromTitle()
{
    OpenHangar();
}

bool IsMouseOnModeSelector()
{
    if (!TitleScene::IsStartGameSelected() || Meta::ModeCount() <= 1 || !GameInput::IsMouseInsideWindow()) return false;
    const float px = (GameInput::GetMouseX() + 1.0f) * 640.0f;
    const float py = (1.0f - GameInput::GetMouseY()) * 360.0f;
    return px >= MODE_X - 24.0f && px <= MODE_X + MODE_W + 10.0f && py >= MODE_CY - MODE_H * 0.5f - 10.0f && py <= MODE_CY + 96.0f;
}

// Called every frame from UpdateTitle(). Returns true while the hangar is open (the title must not react then).
bool UpdateTitleOverlay()
{
    Meta::Ensure();

    const bool mouseInside = GameInput::IsMouseInsideWindow();
    const float mx = mouseInside ? GameInput::GetMouseX() : 0.0f;
    const float my = mouseInside ? GameInput::GetMouseY() : 0.0f;

    if (!g_HangarOpen)
    {
        if (GameInput::IsTriggered('H')) // (the HANGAR row of the title menu opens it too)
        {
            OpenHangar();
            return true;
        }
        UpdateModeSelector(mouseInside, mx, my);
        return false;
    }

    ++g_HangarTimer;
    if (g_HangarMessage > 0) --g_HangarMessage;
    if (g_HangarFlash > 0) --g_HangarFlash;

    const int before = g_HangarSel;
    const int beforeSyn = g_HangarSyn;
    const bool left = GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A');
    const bool right = GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D');
    const bool up = GameInput::IsTriggered(VK_UP) || GameInput::IsTriggered('W');
    const bool down = GameInput::IsTriggered(VK_DOWN) || GameInput::IsTriggered('S');
    const bool tab = GameInput::IsTriggered(VK_TAB) || GameInput::IsTriggered('E') || GameInput::IsTriggered('Q');
    if (g_HangarSynFocus)
    {
        // the keyboard is in the list of synergies: the arrows walk it, TAB goes back to the tiles
        if (tab)
        {
            g_HangarSynFocus = false;
            g_HangarSyn = -1;
            g_HangarSel = HANGAR_LAYOUT[0][0];
        }
        else if (left || right || up || down) g_HangarSyn = HangarSynergyStep(g_HangarSyn, (down ? 1 : 0) - (up ? 1 : 0), (left || right) ? 1 : 0);
    }
    else if (tab && HangarSynergyFirst() >= 0)
    {
        g_HangarSynFocus = true;
        g_HangarSyn = HangarSynergyFirst();
        g_HangarSel = -1;
    }
    else
    {
        if (left) HangarMove(0, -1);
        if (right) HangarMove(0, 1);
        if (up) HangarMove(-1, 0);
        if (down) HangarMove(1, 0);
    }

    int hovered = -1;
    int hoveredSyn = -1;
    if (mouseInside)
    {
        const float pxX = (mx + 1.0f) * 640.0f;
        const float pxY = (1.0f - my) * 360.0f;
        hoveredSyn = HangarSynergyAt(pxX, pxY);
        for (int r = 0; r < HANGAR_ROWS; ++r)
        {
            for (int c = 0; c < HANGAR_COLS; ++c)
            {
                if (HANGAR_LAYOUT[r][c] < 0) continue;
                float x, y;
                HangarTileXY(r, c, x, y);
                if (pxX >= x && pxX <= x + HANGAR_TILE && pxY >= y && pxY <= y + HANGAR_TILE) hovered = HANGAR_LAYOUT[r][c];
            }
        }
    }
    if (hovered >= 0)
    {
        g_HangarSel = hovered; // a tile and a synergy are never highlighted together
        g_HangarSyn = -1;
        g_HangarSynFocus = false;
    }
    else if (hoveredSyn >= 0)
    {
        g_HangarSyn = hoveredSyn;
        g_HangarSel = -1;
        g_HangarSynFocus = false;
    }
    else if (mouseInside && GameInput::MouseMoved()) // the mouse left the tiles and the synergies: no highlight
    {
        g_HangarSel = -1;
        g_HangarSyn = -1;
        g_HangarSynFocus = false;
    }
    if (g_HangarSel != before || g_HangarSyn != beforeSyn) GameAudio::Play(GameSound::MenuCursor);
    if (g_HangarSel >= 0 && (GameInput::IsTriggered(VK_RETURN) || GameInput::IsTriggered(VK_SPACE)
        || (hovered >= 0 && GameInput::IsTriggered(VK_LBUTTON))))
    {
        UnlockSelected();
    }

    if (g_HangarTimer > 8
        && (GameInput::IsTriggered(VK_ESCAPE) || GameInput::IsTriggered(VK_BACK) || GameInput::IsTriggered(VK_RBUTTON)
            || GameInput::IsTriggered('H')))
    {
        g_HangarOpen = false;
        GameAudio::Play(GameSound::MenuCursor);
    }
    return true; // also on the closing frame, so that ESC does not reach the title menu
}
}



