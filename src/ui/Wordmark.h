#pragma once

#include "ui/UiKit.h"

#include <string>

// Wordmark: the logo of POLARITY SHIFT, drawn from polygons (not from a font), so it is the same in every language and at every size.
//
// Block letters of one stroke weight with hard 45 degree cuts, leaning like the panels of the menus (the top is pushed to the right). The O of POLARITY
// holds the seam diamond (half red, half blue, a white core): the symbol of the game is part of its name. POLARITY has a hard red offset, SHIFT a hard blue
// one, and a solid white block runs from the end of SHIFT to the end of POLARITY - the logic of the covers of Persona 3 Reload, in our own shapes.
// Tool: tools/make_wordmark.py makes the letters (src/ui/WordmarkGlyphs.inl).
//
// All positions are virtual 1280 x 720 pixels (y down), `unit` is the cap height in pixels.
namespace Wordmark
{
// Width of a word in pixels (letters + the gaps between them, without the offset).
float WordWidth(const std::string& word, float unit);

// One word. (x, y) = the left end of the cap line and its TOP. `shadowPx`: the hard offset (down and to the right) in `shadow`.
// The O of the word is hollow: with `seamInO` the seam diamond sits in it.
void DrawWord(const std::string& word, float x, float y, float unit, UiKit::Color face, UiKit::Color shadow, float shadowPx, float alpha, bool seamInO);

// THE EMBLEM - the mark of the game: a diamond FRAME (ivory) and, inside it, the diamond split on the vertical, the red half slid UP and the blue half
// slid DOWN: the polarity shifts, and the halves break out of the frame at the top and the bottom (an ink outline cuts them free of it). A white pivot
// diamond sits on the split. r = the radius of the frame; the whole mark is about 1.04 r high above and below its centre. `framed` false: the halves
// only (for the O of the wordmark and for very small sizes). The same geometry is in tools/make_icon.py (res/icon.ico).
void DrawEmblem(float cx, float cy, float r, float alpha, bool framed);

// The whole logo: POLARITY, and SHIFT under it, indented, with the white block after it. (x, y) = the left end and the TOP of the first line.
// `a1` / `a2`: the opacity of the two lines (the title lets them come in one after the other); `slide1` / `slide2`: how far each is shifted to the left
// (pixels, the entrance and the exit); `block`: 0..1, how much of the white block is drawn.
// Returns the bottom of the logo (y).
float DrawLogo(float x, float y, float unit, float a1, float a2, float slide1, float slide2, float block);

// The small badge for the corner of a card: both words on one line, a little hairline between them.
void DrawBadge(float xRight, float yBottom, float unit, float alpha);
}

