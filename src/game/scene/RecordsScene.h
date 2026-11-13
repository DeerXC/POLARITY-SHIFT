#pragma once
// Records page. OUTSIDE: the career (high score, best streak, clears, fastest clear), the trend of the last runs, and the log of the runs - only what a player
// wants to know at a glance. INSIDE (ENTER / a click on a run): everything about that run - its numbers, how it ended, the modules with their names.
namespace RecordsScene
{
void Open(); // the page is entered from the title screen: back to the newest run, nothing highlighted
void Update();
void Draw();
}
