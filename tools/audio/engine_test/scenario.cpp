#include "core/GameAudio.h"
#include "core/GameSettings.h"
#include "mock_xaudio2.h"
#include <cmath>
#include <cstdio>
using namespace MockXAudio2;
static void Step() { GameAudio::Update(); Advance(1); }
int main()
{
    GameSettings::Edit() = GameSettings::Values(); GameSettings::ResetKeys();
    GameAudio::Initialize();
    GameAudio::SetSeVolume(0.9f); GameAudio::SetBgmVolume(0.8f);
    VoiceImpl *ui = nullptr, *pl = nullptr, *wo = nullptr, *al = nullptr, *music = nullptr;
    int k = 0;
    for (VoiceImpl* v : g_Voices) { if (v->kind == Kind::Submix && v->stage == 1) { VoiceImpl** t[4] = {&ui, &pl, &wo, &al}; *t[k++] = v; } if (v->kind == Kind::Submix && v->stage == 0) music = v; }
    GameAudio::PlayLoop(GameSound::BossBgm);
    FILE* f = fopen("scenario.csv", "w");
    fprintf(f, "t,ui,player,world,alert,music,filter,out_rms_db,out_peak_db,event\n");
    GameAudio::FightState fs; fs.active = true; fs.alive = true; fs.hpRate = 0.9f; fs.bossFight = true;
    for (int s = 0; s < 60 * 14; ++s)
    {
        const char* ev = "";
        const float t = s / 60.0f;
        if (s % 6 == 0) GameAudio::Play(GameSound::BossCannonFire);
        if (s % 4 == 0) GameAudio::Play(GameSound::EnemyHitBlue);
        if (s % 9 == 0) GameAudio::Play(GameSound::PlayerShotBlue);
        if (s % 13 == 0) GameAudio::PlayAbsorb(false);
        if (s % 40 == 7) GameAudio::Play(GameSound::EnemyExplosion);
        if (s == 120) { GameAudio::Play(GameSound::WhiteWarn); ev = "white warning"; }
        if (s == 240) { GameAudio::Play(GameSound::OverloadAlarm); ev = "overload alarm"; fs.overload = true; fs.overloadRate = 0.0f; }
        if (s > 240 && s < 430) fs.overloadRate = (s - 240) / 190.0f;
        if (s == 430) { GameAudio::Play(GameSound::OverloadBreak); ev = "overload broken"; fs.overload = false; }
        if (s == 560) { fs.hpRate = 0.25f; ev = "HP 25 %"; }
        if (s == 700) { fs.hpRate = 0.08f; ev = "HP 8 %"; }
        GameAudio::SetFightState(fs);
        const size_t before = g_Output.size();
        Step();
        float peak = 0, ss = 0; size_t n = g_Output.size() - before;
        for (size_t i = before; i < g_Output.size(); ++i) { peak = fmaxf(peak, fabsf(g_Output[i])); ss += g_Output[i] * g_Output[i]; }
        fprintf(f, "%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%s\n", t, ui->volume, pl->volume, wo->volume, al->volume, music->volume, music->filterFrequency,
                20 * log10(sqrt(ss / n) + 1e-6), 20 * log10(peak + 1e-6), ev);
    }
    fclose(f);
}

