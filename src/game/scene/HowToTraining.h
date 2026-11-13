#pragma once
// The small training stage of the HOW TO PLAY page: a strip of the screen in which the ship can be flown for real (the same keys, buttons and stick as in the game) and the
// rules are practised one after the other: MOVE, SWITCH the polarity, ABSORB bullets of your colour (the other colour hurts), fire the PIERCING BEAM at a shielded dummy,
// DODGE white bullets. Nothing here touches the run, the saves or the records.
namespace HowToTraining
{
// the stage: virtual 1280 x 720 px
constexpr float X = 365.0f;
constexpr float Y = 206.0f;
constexpr float W = 550.0f;
constexpr float H = 300.0f;

void Reset();
// once per update step while the page is shown. Returns true while the stage uses the key (ENTER restarts it when the training is done): the page must not act on it.
void Update();
void Draw(float alpha, float slide);
bool Done();
int Step(); // 0 .. 5 (5 = done)
}
