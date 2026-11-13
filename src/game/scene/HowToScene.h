#pragma once
// "How to play" page: three pages, and only what a player has to know - BASICS (three animated rows: switch, absorb, the beam; and one line about white bullets),
// TACTICS (the three tactics with the demos of the hangar) and TRAINING (a small stage in which the ship can be flown and the rules practised). It does not start the game
// and it does not list keys: the keys are in the settings, and every prompt here shows the key / button that is bound now.
namespace HowToScene
{
void Open(); // always opens on the BASICS page
void Update();
void Draw();
}

