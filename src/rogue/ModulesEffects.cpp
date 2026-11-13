#include "rogue/Modules.h"
#include "game/GameEnemy.h"
#include "boss/GameBoss.h"
#include "core/Collision.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "game/Combat.h"
#include "game/EnemyArt.h"
#include "game/GameEffect.h"
#include "game/GamePlayer.h"
#include "game/GameSpecialShot.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/ScreenShake.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/ModulesInternal.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>

// What the modules do: absorbing, damage, the beam, the tactics, per-frame hooks, backup core.
namespace Mod
{
// ---------------------------------------------------------------- energy / absorb
// A kill drops energy: motes that fly to the ship (more with the streak), and it counts for the clear rate of the wave.
void OnEnemyKilled(const Enemy& enemy)
{
    ++g_WaveKilled;
    int energy = Balance::KILL_ENERGY_BIG;
    if (enemy.elite || enemy.type == EnemyType::Rival)
        energy = Balance::KILL_ENERGY_ELITE;
    else if (enemy.type == EnemyType::Scout)
        energy = Balance::KILL_ENERGY_SMALL;
    else if (enemy.type == EnemyType::Sine || enemy.type == EnemyType::Charger)
        energy = Balance::KILL_ENERGY_MID;
    energy = static_cast<int>(std::lround(static_cast<float>(energy) * Balance::STREAK_ENERGY[StreakTier()]));
    if (GameState::energy < EnergyMax()) g_StatKillEnergy += static_cast<float>(energy);
    const int motes = (std::min)(6, (std::max)(2, energy / 4));
    int left = energy;
    for (int n = 0; n < motes && left > 0; ++n)
    {
        for (Mote& m : g_Motes)
        {
            if (m.active) continue;
            m = Mote{};
            m.active = true;
            m.x = enemy.x;
            m.y = enemy.y;
            const float a = static_cast<float>(std::rand() % 628) / 100.0f;
            m.vx = std::cos(a) * 0.008f;
            m.vy = std::sin(a) * 0.008f / ASPECT_Y;
            m.delay = 6 + std::rand() % 8;
            m.polarity = enemy.polarity;
            m.value = n == motes - 1 ? left : energy / motes;
            left -= m.value;
            break;
        }
    }
}

void GainEnergy(int amount)
{
    const int maxEnergy = EnergyMax();
    const int previous = GameState::energy;
    GameState::energy += amount;
    if (GameState::energy > maxEnergy) GameState::energy = maxEnergy;
    if (previous < maxEnergy && GameState::energy >= maxEnergy)
    {
        GameAudio::Play(GameSound::EnergyFull);
    }
    GameState::energyFlashTimer = GameState::ENERGY_FLASH_DURATION;
}

// The energy of one absorbed bullet. The chain makes a wide net in a bullet field give less per bullet (Balance::ChainScale); the modules scale it.
float ScaleAbsorbEnergy(float base)
{
    float scale = Balance::ChainScale(g_ChainHeat);
    if (Has(Id::Momentum)) scale *= 1.0f - Balance::MOMENTUM_ENERGY_PENALTY * CostK(Id::Momentum);
    if (Has(Id::Backwater) && LowHp()) scale *= 1.0f + Balance::BACKWATER_ABSORB_BONUS * Gain(Id::Backwater);
    return base * scale;
}

// is this point inside the band of fire of EMBER?
static bool InEmber(float x, float y)
{
    return g_EmberTimer > 0 && x >= g_EmberStartX && x <= g_EmberEndX && std::fabs(y - g_EmberY) <= g_EmberHalf;
}

static void AddArc(float x1, float y1, float x2, float y2)
{
    for (EchoArc& arc : g_Arcs)
    {
        if (arc.timer > 0) continue;
        arc.x1 = x1;
        arc.y1 = y1;
        arc.x2 = x2;
        arc.y2 = y2;
        arc.polarity = GameState::player.polarity;
        arc.timer = 10;
        break;
    }
}

// An arc from (ox, oy) to the nearest enemy (the boss when there is none). ECHO's arc jumps on to the nearest enemies that it has not hit yet
// (Balance::ECHO_JUMP_FALLOFF of the damage each), and an enemy in the fire of EMBER takes one more hit (the synergy THUNDERSTORM).
void FireEchoArc(float ox, float oy, float hits, bool jumps)
{
    if (hits < 0.0f) hits = static_cast<float>(Balance::ECHO_DAMAGE_FIRST);
    const int targets = jumps ? EchoTargetsAt(Level(Id::Echo)) : 1;
    int hitSlots[4] = {-1, -1, -1, -1};
    float fx = ox, fy = oy;
    bool hitAny = false;
    for (int n = 0; n < targets && n < 4; ++n)
    {
        float bestDistance = n == 0 ? 1.0e9f : Balance::ECHO_JUMP_RANGE * Balance::ECHO_JUMP_RANGE;
        int bestIndex = -1;
        for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        {
            if (!GameState::enemies[i].active) continue;
            bool done = false;
            for (int k = 0; k < n; ++k) done |= (hitSlots[k] == i);
            if (done) continue;
            const float dx = GameState::enemies[i].x - fx;
            const float dy = (GameState::enemies[i].y - fy) * ASPECT_Y;
            const float distance = dx * dx + dy * dy;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = i;
            }
        }
        if (bestIndex < 0) break;
        Enemy& enemy = GameState::enemies[bestIndex];
        const float damage = hits * (n == 0 ? 1.0f : Balance::ECHO_JUMP_FALLOFF[n - 1]);
        const float ex = enemy.x, ey = enemy.y;
        const bool burning = ComboOn(Meta::ComboStorm) && InEmber(ex, ey);
        DamageEnemy(enemy, damage + (burning ? 1.0f : 0.0f)); // synergy: thunderstorm
        AddArc(fx, fy, ex, ey);
        GameEffect::SpawnHit(ex, ey, true);
        hitSlots[n] = bestIndex;
        fx = ex;
        fy = ey;
        hitAny = true;
    }
    if (!hitAny)
    {
        float tx = 0.0f, ty = 0.0f;
        if (!GameBoss::GetTargetPosition(tx, ty)) return;
        GameBoss::TakeDamage((std::max)(1, static_cast<int>(std::lround(hits)))); // (the score of the boss: UpdateBossScore)
        AddArc(ox, oy, tx, ty);
        GameEffect::SpawnHit(tx, ty, true);
    }
    GameAudio::Play(GameSound::EchoArc);
}

// Called once per absorbed bullet (after the caller played its own effect / sound).
void OnAbsorb(float baseEnergy)
{
    ++g_RunAbsorbs;
    g_IdleAbsorbFrames = 0;
    const bool wasFull = GameState::energy >= EnergyMax();

    // energy has fractions (the chain scales it); the bar is whole numbers. A full bar takes nothing more.
    if (!wasFull) g_StatAbsorbEnergy += ScaleAbsorbEnergy(baseEnergy);
    g_EnergyFrac += ScaleAbsorbEnergy(baseEnergy);
    g_ChainHeat += Balance::CHAIN_ADD;
    const int whole = static_cast<int>(g_EnergyFrac);
    if (whole > 0)
    {
        g_EnergyFrac -= static_cast<float>(whole);
        GainEnergy(whole);
    }
    if (GameState::energy >= EnergyMax()) g_EnergyFrac = 0.0f;

    if (Has(Id::Aegis) && g_AegisTimer > 0) --g_AegisTimer; // the shield comes back with the absorbed bullets
    if (Has(Id::Overflow) && wasFull && g_OverflowStore < Balance::OVERFLOW_STORE_MAX) ++g_OverflowStore; // a full bar stores the bullet for the next beam
    if (Has(Id::Echo) && ++g_EchoCounter >= Balance::ECHO_EVERY)
    {
        g_EchoCounter = 0;
        FireEchoArc(GameState::player.x, GameState::player.y);
    }
    if (Resonant(Verb::Absorb, 3) && ++g_ResArcCounter >= Balance::RES_ABSORB_ARC_EVERY) // resonance: three absorb modules
    {
        g_ResArcCounter = 0;
        FireEchoArc(GameState::player.x, GameState::player.y, static_cast<float>(Balance::ECHO_DAMAGE_FIRST), false);
    }
    if (Has(Id::Momentum)) // every absorbed bullet is a stack of fire rate
    {
        g_Momentum += 1.0f;
        if (g_Momentum > static_cast<float>(Balance::MOMENTUM_MAX_STACKS)) g_Momentum = static_cast<float>(Balance::MOMENTUM_MAX_STACKS);
        g_MomentumIdle = 0;
    }
}

// extra energy for a special bullet (a bolt of the boss that follows the ship): through the chain and the modules, but not another absorb
void OnAbsorbBonus(float baseEnergy)
{
    g_EnergyFrac += ScaleAbsorbEnergy(baseEnergy);
    const int whole = static_cast<int>(g_EnergyFrac);
    if (whole > 0)
    {
        g_EnergyFrac -= static_cast<float>(whole);
        GainEnergy(whole);
    }
    if (GameState::energy >= EnergyMax()) g_EnergyFrac = 0.0f;
}

// the bolts of the boss that follow the ship give more energy (Balance::ABSORB_MUL_HOMING): GameBoss counts them, whoever absorbed them
void FlushBossAbsorbBonus()
{
    const int extra = GameBoss::ConsumeAbsorbBonus();
    if (extra > 0) OnAbsorbBonus(GameState::ENERGY_PER_ABSORB_BOSS * (Balance::ABSORB_MUL_HOMING - 1.0f) * static_cast<float>(extra));
}

static void AbsorbEnemyBulletScaled(EnemyBullet& bullet, float energyScale)
{
    bullet.active = false;
    GameEffect::SpawnAbsorb(bullet.x, bullet.y, 0.06f, bullet.polarity);
    if (g_SoundCooldown <= 0)
    {
        GameAudio::PlayAbsorb(bullet.polarity == Polarity::Red);
        g_SoundCooldown = 3;
    }
    OnAbsorb(GameState::ENERGY_PER_ABSORB * bullet.energyMul * energyScale);
}

static void AbsorbBossBulletsScaled(int count, float x, float y, Polarity polarity, float energyScale)
{
    if (count <= 0) return;
    GameEffect::SpawnAbsorb(x, y, 0.07f, polarity);
    if (g_SoundCooldown <= 0)
    {
        GameAudio::PlayAbsorb(polarity == Polarity::Red);
        g_SoundCooldown = 3;
    }
    for (int i = 0; i < count; ++i)
        OnAbsorb(GameState::ENERGY_PER_ABSORB_BOSS * energyScale);
    FlushBossAbsorbBonus();
}

void AbsorbBossBullets(int count, float x, float y, Polarity polarity)
{
    AbsorbBossBulletsScaled(count, x, y, polarity, 1.0f);
}

// ---------------------------------------------------------------- enemy damage from modules
void KillEnemy(Enemy& enemy)
{
    float explosionSize = 0.10f;
    if (enemy.type == EnemyType::Heavy || enemy.type == EnemyType::Turret)
        explosionSize = 0.18f;
    else if (enemy.type == EnemyType::Charger)
        explosionSize = 0.13f;
    else if (enemy.type == EnemyType::Rival)
        explosionSize = 0.22f;

    GameEffect::SpawnExplosion(enemy.x, enemy.y, explosionSize);
    EnemyArt::OnDeath(enemy);
    const bool heavy = Combat::UsesHeavyEnemyAudio(enemy.type);
    GameAudio::Play(heavy ? GameSound::EliteExplosion : GameSound::EnemyExplosion);
    if (heavy) ScreenShake::Start(10, 0.010f);

    // the slot may be refilled by the wave in the same frame: forget it so the newcomer is seen as new
    const int slot = static_cast<int>(&enemy - GameState::enemies);
    if (slot >= 0 && slot < GameState::ENEMY_MAX) g_PrevEnemyActive[slot] = false;

    enemy.hp = 0;
    enemy.active = false;
    OnEnemyKilled(enemy);
    GameState::score += enemy.scoreValue * GameState::GetScoreMultiplier();
}

void DamageEnemy(Enemy& enemy, float hits)
{
    // 'hits' are shots: 1 = what one shot of the ship does (Balance::HP_UNIT points)
    const int damage = (std::max)(1, static_cast<int>(std::lround(hits * static_cast<float>(Balance::HP_UNIT) * DamageMul())));
    enemy.hp -= damage;
    NoteDamage(static_cast<float>(damage) / static_cast<float>(Balance::HP_UNIT));
    enemy.hitFlashTimer = 4;
    if (enemy.hp <= 0) KillEnemy(enemy);
}

void DamageBand(float startX, float endX, float centerY, float halfHeight, float damage)
{
    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
    {
        Enemy& enemy = GameState::enemies[i];
        if (!enemy.active) continue;
        const bool overlapsX = endX >= enemy.x - enemy.halfWidth && startX <= enemy.x + enemy.halfWidth;
        const bool overlapsY =
            centerY + halfHeight >= enemy.y - enemy.halfHeight && centerY - halfHeight <= enemy.y + enemy.halfHeight;
        if (overlapsX && overlapsY) DamageEnemy(enemy, damage);
    }
    GameBoss::CheckSpecialShotHit(startX, endX, centerY, halfHeight, (std::max)(1, static_cast<int>(std::lround(damage)))); // (the score of the boss: UpdateBossScore)
}

// ---------------------------------------------------------------- PULSE
void FirePulse()
{
    const float radius = ComboOn(Meta::ComboGravity) ? PULSE_RADIUS_GRAVITY : PULSE_RADIUS; // synergy: gravity well
    const float px = GameState::player.x;
    const float py = GameState::player.y;
    int refund = 0;

    for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
    {
        EnemyBullet& bullet = GameState::enemyBullets[i];
        if (!bullet.active) continue;
        const float dx = bullet.x - px;
        const float dy = (bullet.y - py) * ASPECT_Y;
        if (dx * dx + dy * dy > radius * radius) continue;
        bullet.active = false;
        GameEffect::SpawnAbsorb(bullet.x, bullet.y, 0.05f, bullet.polarity);
        refund += 2;
    }

    const int bossBullets = GameBoss::AbsorbSamePolarityInZone(px, py, radius, radius / ASPECT_Y, Polarity::Red)
                            + GameBoss::AbsorbSamePolarityInZone(px, py, radius, radius / ASPECT_Y, Polarity::Blue);
    refund += bossBullets * 2;
    GameBoss::ConsumeAbsorbBonus(); // (the shockwave gives its own refund, not the bonus of the homing bolts)
    const int refundCap = ComboOn(Meta::ComboGravity) ? PULSE_REFUND_CAP * 3 / 2 : PULSE_REFUND_CAP; // synergy: gravity well - the wider net may give half as much more
    if (refund > refundCap) refund = refundCap;
    if (refund > 0) GainEnergy(refund);
    g_PulseTimer = 24;
    g_PulseX = px;
    g_PulseY = py;
    g_PulseRadius = radius;
    // (no sound of its own: the ring is seen, and the energy it gives is heard when the bar is full)
}

int PulsePeriod()
{
    return static_cast<int>(std::lround(static_cast<float>(Balance::PULSE_PERIOD) / Gain(Id::Pulse)));
}

// Combat / PlayScene call this right after every point of damage taken (GameState::playerHp has already been reduced)
void NoteHit()
{
    ++g_RunHits;
    g_SinceHurt = 0;
    // the streak drops to the start of the tier below (a hit at tier 2 keeps tier 1; a hit at tier 1 or 0 is the end of it)
    g_Streak = StreakTier() >= 2 ? static_cast<float>(Balance::STREAK_TIER1) : 0.0f;
    GameState::combo = static_cast<int>(g_Streak);
    if (Has(Id::Pulse)) g_PulseCooldown = (std::max)(0, g_PulseCooldown - Balance::PULSE_HIT_CUT); // a hit brings the next shockwave sooner
}

// ---------------------------------------------------------------- the beam (the V key)
float g_BeamDurationScale = 1.0f;

// Starts the piercing beam: a full one with a full bar, a half one with half a bar (TWIN). OVERFLOW adds what it stored to a full beam (and to a half one
// with the synergy POISED). Sets what a tick of the beam does and how long it lasts; takes the energy. False: not enough energy.
bool BeginBeam(int& energy)
{
    const int full = EnergyMax();
    const int half = full * Balance::TWIN_COST_PERCENT / 100;
    const bool isFull = energy >= full;
    const bool isHalf = !isFull && Has(Id::Twin) && energy >= half;
    if (!isFull && !isHalf) return false;
    g_BeamIsHalf = isHalf;
    float damage = 1.0f;
    float length = ShotDurationScale();
    if (isHalf)
    {
        damage = (std::min)(1.0f, Balance::TWIN_DAMAGE * Gain(Id::Twin));
        length *= 0.5f;
        energy -= half;
    }
    else
    {
        energy = 0;
    }
    if (Has(Id::Overflow) && g_OverflowStore > 0 && (isFull || ComboOn(Meta::ComboPoised)))
    {
        const float boost = Balance::OVERFLOW_BOOST_PER_BULLET * Gain(Id::Overflow) * static_cast<float>(g_OverflowStore);
        damage *= 1.0f + boost;
        length *= 1.0f + boost;
        g_OverflowStore = isHalf ? g_OverflowStore / 2 : 0; // synergy: poised - a half beam spends only half of the store
    }
    g_BeamDamageScale = damage;
    g_BeamDurationScale = length;
    return true;
}

float BeamDurationScale()
{
    return g_BeamDurationScale;
}

// ---------------------------------------------------------------- tactics (the TACTIC key)
static bool TacticDown()
{
    return GameInput::ActionDown(GameSettings::Action::Tactic);
}

float MoveSpeedScale()
{
    return g_ChargeFrames > 0 ? Balance::CHARGE_MOVE : 1.0f;
}

void FireChargeBeam(float charge)
{
    const bool full = charge >= 1.0f;
    const float half = full ? CHARGE_HALF_FULL : CHARGE_HALF_MIN + CHARGE_HALF_PART * charge;
    const float hits = Balance::CHARGE_HITS_MIN + (Balance::CHARGE_HITS_MAX - Balance::CHARGE_HITS_MIN) * charge;
    const float startX = GameState::player.x + GameState::player.halfWidth;
    DamageBand(startX, 1.15f, GameState::player.y, half, hits); // (a full charge is high enough to break an overload of the boss)
    g_TacticFlash = 16;
    g_TacticBeamX = startX;
    g_TacticBeamY = GameState::player.y;
    g_TacticBeamHalf = half;
    GameAudio::Play(full ? GameSound::SpecialShot : GameSound::EliteHit);
    if (full) ScreenShake::Start(8, 0.008f);
}

void UpdateTactic()
{
    const bool down = TacticDown();
    const bool pressed = down && !g_PrevTacticDown;
    g_PrevTacticDown = down;
    if (g_TacticCooldown > 0) --g_TacticCooldown;
    if (g_TacticFlash > 0) --g_TacticFlash;
    if (g_Decoy.active && --g_Decoy.timer <= 0) g_Decoy.active = false;
    if (g_Tactic < 0 || GameState::playerHp <= 0) return;
    const Tactic tactic = static_cast<Tactic>(g_Tactic);

    if (tactic == Tactic::Decoy && pressed && g_TacticCooldown <= 0)
    {
        g_Decoy.x = GameState::player.x;
        g_Decoy.y = GameState::player.y;
        g_Decoy.polarity = GameState::player.polarity; // it takes the colour the ship has now: switch after dropping it and both colours are absorbed
        g_Decoy.timer = Balance::DECOY_FRAMES;
        g_Decoy.active = true;
        g_TacticCooldown = Balance::DECOY_COOLDOWN;
        GameEffect::SpawnAbsorb(g_Decoy.x, g_Decoy.y, 0.14f, g_Decoy.polarity);
        GameAudio::Play(GameSound::TacticDecoy);
        LogEvent("tactic", "decoy");
    }
    else if (tactic == Tactic::Dash)
    {
        if (pressed && g_TacticCooldown <= 0)
        {
            float mx = 0.0f, my = 0.0f;
            GamePlayer::GetMoveInput(mx, my);
            const float length = std::sqrt(mx * mx + my * my);
            if (length < 0.2f)
            {
                mx = 1.0f; // no input: forward
                my = 0.0f;
            }
            else
            {
                mx /= length;
                my /= length;
            }
            const float step = Balance::DASH_DISTANCE / static_cast<float>(Balance::DASH_FRAMES);
            g_DashX = mx * step;
            g_DashY = my * step / ASPECT_Y;
            g_DashTimer = Balance::DASH_FRAMES;
            g_TacticCooldown = Balance::DASH_COOLDOWN;
            if (GameState::playerInvincibleTimer < Balance::DASH_INVINCIBLE) GameState::playerInvincibleTimer = Balance::DASH_INVINCIBLE;
            GameAudio::Play(GameSound::TacticDash);
            LogEvent("tactic", "dash");
        }
        if (g_DashTimer > 0)
        {
            --g_DashTimer;
            GameState::player.x += g_DashX;
            GameState::player.y += g_DashY;
            GamePlayer::ClampToScreen(GameState::player);
        }
    }
    else if (tactic == Tactic::Charge)
    {
        if (down && g_TacticCooldown <= 0)
        {
            if (g_ChargeFrames < Balance::CHARGE_FULL) ++g_ChargeFrames;
            if (GameState::player.polarity != g_PrevPolarity) g_ChargeFrames /= 2; // a colour switch while it charges costs half of the charge
        }
        else if (g_ChargeFrames > 0)
        {
            if (g_ChargeFrames >= Balance::CHARGE_MIN)
            {
                FireChargeBeam(static_cast<float>(g_ChargeFrames) / static_cast<float>(Balance::CHARGE_FULL));
                g_TacticCooldown = Balance::CHARGE_COOLDOWN;
                LogEvent("tactic", "charge");
            }
            g_ChargeFrames = 0;
        }
    }
}

// ---------------------------------------------------------------- per-frame hooks
// right after GamePlayer::Update(): the tactic and the fire rate bonuses
void AfterPlayerUpdate()
{
    UpdateTactic();
    g_PrevPolarity = GameState::player.polarity;

    const float bonus = FireRateBonus();
    if (bonus > 0.0f)
    {
        g_RateAcc += bonus;
        while (g_RateAcc >= 1.0f)
        {
            g_RateAcc -= 1.0f;
            if (GameState::shotCoolTime > 0) --GameState::shotCoolTime;
        }
    }
}

// The score of the boss is paid while its bar goes down: every share of it that is shot away pays at once, so a fight that is lost is not worth 0, and the
// whole of it (a clear) is what the single 5000 used to be. Boss practice scores the same way (Balance::PRACTICE_SCORES).
void UpdateBossScore()
{
    if ((g_PracticePhase > 0 && !Balance::PRACTICE_SCORES) || !g_ReachedBoss || !GameBoss::IsActive()) return;
    const bool down = GameBoss::IsDefeated();
    int target = static_cast<int>(GameBoss::GetProgress() * static_cast<float>(Balance::BOSS_SCORE_DAMAGE));
    target = target / Balance::BOSS_SCORE_STEP * Balance::BOSS_SCORE_STEP;
    if (down) target = Balance::BOSS_SCORE_DAMAGE;
    if (target > g_BossScorePaid)
    {
        GameState::score += target - g_BossScorePaid;
        g_BossScorePaid = target;
    }
    if (down && !g_BossClearPaid)
    {
        GameState::score += Balance::BOSS_SCORE_CLEAR;
        g_BossClearPaid = true;
    }
}

bool MendActive()
{
    if (!Has(Id::Mend) || GameState::playerHp <= 0 || GameState::playerHp >= MaxHp()) return false;
    const bool unyielding = Has(Id::Backwater) && LowHp() && ComboOn(Meta::ComboUnyielding);
    return g_SinceHurt >= Balance::MEND_DELAY || unyielding;
}

float MendProgress()
{
    return g_MendAcc < 0.0f ? 0.0f : (g_MendAcc > 1.0f ? 1.0f : g_MendAcc);
}

// once per UpdateGame frame (not while the pick / route screen is open)
void Tick()
{
    ++g_RunFrames;
    ++g_IdleAbsorbFrames;
    ++g_SinceHurt;
    g_ChainHeat *= Balance::CHAIN_DECAY; // the chain of absorbed bullets cools down
    if (g_AegisFlash > 0) --g_AegisFlash;
    if (g_AfterglowTimer > 0) --g_AfterglowTimer;
    if (++g_FocusIdle > Balance::FOCUS_IDLE_FRAMES)
    {
        g_FocusStacks = 0;
        g_FocusTarget = -1;
    }
    if (Has(Id::Momentum))
    {
        ++g_MomentumIdle;
        const bool held = g_AfterglowTimer > 0 && ComboOn(Meta::ComboCombo); // synergy: combo - the stacks do not fade while the afterglow lasts
        if (!held && g_Momentum > 0.0f && g_MomentumIdle > Balance::MOMENTUM_HOLD_FRAMES && (g_RunFrames % 15) == 0) g_Momentum = (std::max)(0.0f, g_Momentum - 1.0f);
    }
    if (Has(Id::Pulse) && GameState::playerHp > 0 && --g_PulseCooldown <= 0)
    {
        FirePulse();
        g_PulseCooldown = PulsePeriod();
    }
    if (Has(Id::Mend) && GameState::playerHp > 0)
    {
        const bool unyielding = Has(Id::Backwater) && LowHp() && ComboOn(Meta::ComboUnyielding); // synergy: unyielding - no wait under half of the HP
        if (g_SinceHurt >= Balance::MEND_DELAY || unyielding)
        {
            g_MendAcc += Balance::MEND_PER_SECOND * Gain(Id::Mend) / 60.0f * (unyielding ? 1.5f : 1.0f); // synergy: unyielding - half as fast again
            if (g_MendAcc >= 1.0f)
            {
                const int whole = static_cast<int>(g_MendAcc);
                g_MendAcc -= static_cast<float>(whole);
                if (GameState::playerHp < MaxHp())
                {
                    GameState::playerHp = (std::min)(MaxHp(), GameState::playerHp + whole);
                    g_MendTickFlash = MEND_TICK_FRAMES; // (the ship shows every repaired point)
                }
            }
        }
        else
        {
            g_MendAcc = 0.0f;
        }
    }
    // the repair is seen on the ship, so the player does not have to look at the bar: a ring opens when it BEGINS (the rising edge), a ring of dashes fills towards every
    // repaired point while it goes on (ModulesDraw: DrawMendFx)
    const bool mendOn = MendActive();
    if (mendOn && !g_MendWasOn) g_MendStartTimer = MEND_START_FRAMES;
    g_MendWasOn = mendOn;
    if (g_MendStartTimer > 0) --g_MendStartTimer;
    if (g_MendTickFlash > 0) --g_MendTickFlash;
    if (g_SoundCooldown > 0) --g_SoundCooldown;
    if (g_ToastTimer > 0) --g_ToastTimer;
    if (g_PulseTimer > 0) --g_PulseTimer;
    for (EchoArc& arc : g_Arcs)
        if (arc.timer > 0) --arc.timer;
    if (GameBoss::IsActive())
    {
        g_ReachedBoss = true;
        Meta::MarkBossSeen();
    }
    if (GameBoss::IsBattleStarted() && GameBoss::GetPhase() > g_MaxBossPhase) g_MaxBossPhase = GameBoss::GetPhase();
    if (g_PracticePhase > 0 && !Balance::PRACTICE_SCORES)
    {
        GameState::score = 0; // (practice that does not score: it never touches the high score)
    }
    UpdateBossScore();
    if (g_GameOverLock > 0) --g_GameOverLock;
    if (IsPlaying()) ++g_PlayFrames;
    DirectorScaleNew();
    DirectorSpawnElites();
    DirectorSnapshot();
}

// the decoy of the tactic and the bigger absorb box (SIPHON, resonance, DUAL)
void UpdateZones()
{
    if (GameState::playerHp <= 0) return;
    if (g_Decoy.active)
    {
        const Zone& z = g_Decoy;
        for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
        {
            EnemyBullet& bullet = GameState::enemyBullets[i];
            if (!bullet.active || bullet.neutral || bullet.polarity != z.polarity) continue; // white bullets cannot be absorbed
            const float dx = z.x - bullet.x;
            const float dy = (z.y - bullet.y) * ASPECT_Y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length < Balance::DECOY_PULL_RADIUS && length > 0.001f) // the decoy pulls the bullets of its colour
            {
                bullet.x += dx / length * DECOY_PULL_STEP;
                bullet.y += dy / length * DECOY_PULL_STEP / ASPECT_Y;
            }
            if (Collision::CheckAABB(z.x, z.y, DECOY_HALF_W, DECOY_HALF_H, bullet.x, bullet.y, bullet.halfWidth, bullet.halfHeight))
                AbsorbEnemyBulletScaled(bullet, Balance::DECOY_ENERGY);
        }
        AbsorbBossBulletsScaled(GameBoss::AbsorbSamePolarityInZone(z.x, z.y, DECOY_HALF_W, DECOY_HALF_H, z.polarity), z.x, z.y, z.polarity, Balance::DECOY_ENERGY);
    }

    const float scale = SiphonScale();
    if (scale > 1.0f) // the bolts of the boss in the bigger absorb box (the enemy bullets: Combat)
    {
        AbsorbBossBullets(GameBoss::AbsorbSamePolarityInZone(GameState::player.x, GameState::player.y, GameState::player.halfWidth * scale,
                                                             GameState::player.halfHeight * scale, GameState::player.polarity),
                          GameState::player.x, GameState::player.y, GameState::player.polarity);
    }
}

// the special shot has just been fired
void OnSpecialActivated()
{
}

// after the special shot update / damage step: the band of EMBER, AFTERGLOW, and the burst of three break modules
void UpdateSpecial()
{
    const bool active = GameSpecialShot::IsActive();
    if (active)
    {
        g_BeamStartX = GameSpecialShot::GetStartX();
        g_BeamEndX = GameSpecialShot::GetEndX();
        g_BeamY = GameSpecialShot::GetCenterY();
        g_BeamHalf = GameSpecialShot::GetHalfHeight();
    }
    if (g_PrevSpecialActive && !active)
    {
        if (Has(Id::Afterglow)) g_AfterglowTimer = static_cast<int>(std::lround(static_cast<float>(Balance::AFTERGLOW_FRAMES) * Gain(Id::Afterglow)));
        if (Has(Id::Afterglow) && Has(Id::Momentum) && ComboOn(Meta::ComboCombo)) // synergy: combo - the end of the beam fills the stacks (they hold while the afterglow lasts)
        {
            g_Momentum = static_cast<float>(Balance::MOMENTUM_MAX_STACKS);
            g_MomentumIdle = 0;
        }
        if (Resonant(Verb::Break, 3) && !GameBoss::IsDefeated()) // resonance: three break modules - the beam bursts along its line when it ends
        {
            DamageBand(g_BeamStartX, g_BeamEndX, g_BeamY, g_BeamHalf * 1.6f, Balance::RES_BREAK_BLAST_HITS);
            for (int k = 0; k < 5; ++k)
            {
                const float x = g_BeamStartX + (g_BeamEndX - g_BeamStartX) * (0.1f + 0.2f * static_cast<float>(k));
                GameEffect::SpawnExplosion(x, g_BeamY, 0.10f);
            }
            ScreenShake::Start(10, 0.010f);
        }
        if (Has(Id::Ember))
        {
            g_EmberStartX = g_BeamStartX;
            g_EmberEndX = g_BeamEndX;
            g_EmberY = g_BeamY;
            g_EmberHalf = g_BeamHalf * 0.8f;
            g_EmberLife = static_cast<int>(std::lround(static_cast<float>(EMBER_FRAMES) * Gain(Id::Ember)));
            g_EmberTimer = g_EmberLife;
            g_EmberTick = 0;
        }
        g_BeamDamageScale = 1.0f;
        g_BeamIsHalf = false;
    }
    g_PrevSpecialActive = active;

    if (g_EmberTimer > 0)
    {
        --g_EmberTimer;
        if (++g_EmberTick % EMBER_TICK == 0 && !GameBoss::IsDefeated())
        {
            DamageBand(g_EmberStartX, g_EmberEndX, g_EmberY, g_EmberHalf, Gain(Id::Ember));
            // synergy: thunderstorm - the fire calls lightning, an arc from the middle of the band every second tick (0.5 s)
            if (Has(Id::Echo) && ComboOn(Meta::ComboStorm) && (g_EmberTick / EMBER_TICK) % 2 == 0)
                FireEchoArc((g_EmberStartX + g_EmberEndX) * 0.5f, g_EmberY);
        }
        // the fire burns the bullets that fly into it into energy (both colours; the white ones burn without energy)
        for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
        {
            EnemyBullet& bullet = GameState::enemyBullets[i];
            if (!bullet.active || !InEmber(bullet.x, bullet.y)) continue;
            if (bullet.neutral)
            {
                bullet.active = false;
                GameEffect::SpawnAbsorb(bullet.x, bullet.y, 0.04f, bullet.polarity);
                continue;
            }
            AbsorbEnemyBulletScaled(bullet, 0.5f);
        }
        const float cx = (g_EmberStartX + g_EmberEndX) * 0.5f;
        const float hw = (g_EmberEndX - g_EmberStartX) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            const Polarity pol = c == 0 ? Polarity::Red : Polarity::Blue;
            AbsorbBossBulletsScaled(GameBoss::AbsorbSamePolarityInZone(cx, g_EmberY, hw, g_EmberHalf, pol), cx, g_EmberY, pol, 0.5f);
        }
    }
}

// ---------------------------------------------------------------- backup core
bool TryBackupCore()
{
    if (!Has(Id::BackupCore) || g_BackupUsed) return false;
    g_BackupUsed = true;
    const float rate = (std::min)(1.0f, Balance::BACKUP_REVIVE_RATE * Gain(Id::BackupCore));
    GameState::playerHp = (std::max)(1, static_cast<int>(std::lround(static_cast<float>(MaxHp()) * rate)));
    GameState::playerDeathTimer = 0;
    GameState::playerInvincibleTimer = BACKUP_INVINCIBLE_FRAMES;
    const bool whiteToo = ComboOn(Meta::ComboRevenge);
    GameState::energy = whiteToo ? EnergyMax() / 2 : 0; // synergy: revenge - the revive keeps half a bar
    GameState::combo = 0;
    // the blast: every bullet of a colour becomes energy (the white ones are gone; with the synergy REVENGE they become energy too) ...
    for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
    {
        EnemyBullet& bullet = GameState::enemyBullets[i];
        if (!bullet.active || !bullet.neutral || whiteToo) continue;
        GameEffect::SpawnAbsorb(bullet.x, bullet.y, 0.05f, bullet.polarity);
        bullet.active = false;
        g_PrevBulletActive[i] = false;
    }
    ConvertBulletsToMotes();
    GameBoss::ConvertBulletsToEnergy();
    // ... every enemy is hit hard, and the boss loses a share of the phase it is in
    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
    {
        if (GameState::enemies[i].active) DamageEnemy(GameState::enemies[i], Balance::BACKUP_BLAST_HITS);
    }
    GameBoss::DamagePhaseShare(Balance::BACKUP_BOSS_RATE);
    GameEffect::SpawnExplosion(GameState::player.x, GameState::player.y, 0.30f);
    // the blast is drawn by ModulesDraw (DrawReviveBlast): light that gathers into the ship, a flash, three shock rings (red, white, blue) that cross the screen; the shards
    // of both colours are thrown out when the rings are born (UpdateUi, 17 steps from now: when the hit stop is over)
    g_ReviveTimer = REVIVE_FRAMES;
    g_ReviveX = GameState::player.x;
    g_ReviveY = GameState::player.y;
    GameAudio::Play(GameSound::BackupRevive);
    ScreenShake::Start(20, 0.022f);
    if (GameState::hitStop < 18) GameState::hitStop = 18;
    LogEvent("backup_core", "");
    return true;
}
}









