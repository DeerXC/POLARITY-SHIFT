// pick_test: the logic of the module pick screen (the real Mod::UpdateChoice) with scripted keys: the reroll button under the cards.
//   - R deals new cards once per run; the cards that were on offer do not come back; a second R shakes the button and changes nothing
//   - DOWN puts the highlight on the button, ENTER on it rerolls, LEFT / RIGHT / UP take the highlight back to the cards, DOWN does nothing when no reroll is left
//   - 1 / 2 / 3 still pick a card while the highlight is on the button
//   (the highlight of the cards and the mouse belong to GameMenuUi::UpdateMenu, which is a stand-in here, see stubs.cpp)
//   pick_test returns 1 when a check fails
#include "game/GameState.h"
#include "game/GamePlayer.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include "sim_input.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

// one update step of the pick screen with the given keys held for this step
static void Step(std::initializer_list<int> keys)
{
    SimInput::Clear();
    for (int k : keys) SimInput::down[k] = true;
    Mod::UpdateChoice();
    SimInput::EndFrame();
}
static void Idle()
{
    Step({});
}

static void Open()
{
    GamePlayer::Initialize(GameState::player);
    GameState::ResetRun();
    Mod::Reset();
    Mod::g_ChoiceDone = false;
    Mod::g_PicksLeft = 1;
    Mod::OpenChoice();
    for (int i = 0; i < Mod::CHOICE_INPUT_LOCK + 4; ++i) Idle(); // the cards have come in
}

static bool Same(const int a[3], int na, const int b[3], int nb)
{
    if (na != nb) return false;
    for (int i = 0; i < na; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

int main()
{
    std::srand(7);
    {
        std::printf("R rerolls once\n");
        Open();
        Check(Mod::g_ChoiceOpen && Mod::g_ChoiceCount == 3, "three cards are on offer");
        Check(Mod::RerollsLeft() == 1, "one reroll in this run");
        int before[3];
        for (int i = 0; i < 3; ++i) before[i] = Mod::g_Choice[i];
        Step({'R'});
        Check(Mod::RerollsLeft() == 0, "the reroll is used");
        bool none = true;
        for (int i = 0; i < Mod::g_ChoiceCount; ++i)
            for (int k = 0; k < 3; ++k)
                if (Mod::g_Choice[i] == before[k] && !Mod::g_ChoiceUpgrade[i]) none = false;
        Check(none, "none of the cards that were on offer comes back");
        Check(Mod::g_ChoiceTimer <= 1, "the new cards come in again");
        for (int i = 0; i < Mod::CHOICE_INPUT_LOCK + 4; ++i) Idle();
        const int cardsAfter = Mod::g_ChoiceCount;
        int after[3];
        for (int i = 0; i < 3; ++i) after[i] = Mod::g_Choice[i];
        Step({'R'});
        Check(Mod::RerollsLeft() == 0 && Same(after, cardsAfter, Mod::g_Choice, Mod::g_ChoiceCount), "a second R changes nothing");
        Check(Mod::g_RerollShake > 0, "the button shakes");
        Step({'S'});
        Check(!Mod::g_RerollFocus, "DOWN does not highlight a button that has nothing left");
    }
    {
        std::printf("the button with the keyboard / pad\n");
        Open();
        Step({'S'});
        Check(Mod::g_RerollFocus && Mod::g_ChoiceSel == -1, "DOWN highlights the button and takes the highlight off the cards");
        Idle();
        Check(Mod::g_RerollFocus, "it stays there");
        Step({'A'});
        Check(!Mod::g_RerollFocus && Mod::g_ChoiceSel == Mod::g_ChoiceCount / 2, "LEFT takes it back to the middle card");
        Step({'S'});
        Check(Mod::g_RerollFocus, "DOWN again");
        int before[3];
        for (int i = 0; i < 3; ++i) before[i] = Mod::g_Choice[i];
        Step({VK_RETURN});
        Check(Mod::RerollsLeft() == 0 && !Mod::g_RerollFocus, "ENTER on the button rerolls");
        Check(!Same(before, 3, Mod::g_Choice, Mod::g_ChoiceCount), "the cards are new");
        Check(Mod::g_ChoiceOpen, "the pick screen is still open (ENTER did not pick a card)");
    }
    {
        std::printf("a card can be picked while the button is highlighted\n");
        Open();
        Step({'S'});
        const int module = Mod::g_Choice[1];
        const bool upgrade = Mod::g_ChoiceUpgrade[1];
        Step({'2'});
        Check(!Mod::g_ChoiceOpen, "key 2 picks the second card");
        Check(upgrade || Mod::Has(static_cast<Mod::Id>(module)), "the module is in the ship");
    }
    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
