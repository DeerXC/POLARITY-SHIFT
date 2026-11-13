#pragma once

void Game_Initialize();
void Game_Update();
void Game_Draw();
void Game_OnExit(); // the window is closing: a run that is on is ended (its score and its shards count like those of a run that was left)

bool Game_ShouldHideMouseCursor();


