#pragma once

// Change of screen. The colour of the screen changes by itself (GameMenuUi: the edges of the red and blue shapes move from the old
// layout to the new one, see LAYOUTS there). This file takes care of everything else that changes with a screen:
//
//   - the old picture (the last presented frame, DirectXSystem keeps a copy of the back buffer) leaves quickly: it slides away along
//     the red / blue cut and fades in 9 frames, so the old texts and panels are gone before the new shapes have settled. Exits are
//     short, entrances are long (the shapes need about half a second).
//   - the texts of the new screen fade in when the shapes are nearly in place.
//   - the cut flashes when the shapes snap into place (GameMenuUi::Impact).
//
//   Begin()      a top-level screen has just changed (title, how to, records, settings, game, result, hangar)
//   Draw()       last D3D call of Game_Draw
//   TextAlpha()  fade of the UI text of the new screen (TextRenderer::SetGlobalAlpha)
//   Enter()      0..1 progress of the whole change, for the things that slide in with the new screen (pages, cards)
//
// Runs on the clock, so it takes the same time at any refresh rate. Without a picture of the old screen (the very first frame)
// nothing is drawn.
namespace SceneFx
{
void Begin(bool forward);
void Draw();
float TextAlpha();
float Enter();
}
