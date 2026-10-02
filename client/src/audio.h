#pragma once
#include "raylib.h"

// Sound effects and voice lines. Missing asset files are silently skipped, never fatal.
// Each sound may have variants (name.ogg, name_2.ogg, ...); one is picked at random per play.
namespace Audio {

// Shotgun..SuperSheepFire are per-weapon fire sounds; they fall back to Fire when not imported.
enum class Sfx { Explosion, BigExplosion, Fire, Bounce, Splash, Jump, Sheep, Holy, TurnStart, Tick,
                 Shotgun, Airstrike, Donkey, Rope, Teleport,
                 BatSwing, FirePunch, Prod, Sniper, Bow, Homing, OldWomanFire, ScouserFire, SentryPlace, SentryFire,
                 Dynamite, Gas, Abduction, Flood, Parachute, MineBeep, CrateLand, Pickup, SuperSheepFire,
                 Step, Land, HpTick,  // Step, Land: worm foley; HpTick: HUD hp count; silent when not imported
                 CrateImpactHealth, CrateImpactWeapon, CrateImpactUtil,  // parachuted crate touching down, by content
                 Cheer,  // W4M GameOverLogicEntity crowd, over the victory jingle
                 // W4M frontend (kAUDIO_*): cursor, left/right change, A, B, typing, page, popup / screen in-out, per-menu intros
                 FeHighlight, FeChange, FeClick, FeCancel, FeError, FeType, FePage, FePopupIn, FePopupOut, FeNextIn, FeNextOut, FePrevIn, FePrevOut,
                 FeBounce, FeSlide, FeNet, FeCustom, FeSoundVid, FeController, FeFactory, FeBookIn, FeBookOut, FeGrenade, FeWormpot,
                 WormpotSpin, WormpotStop,
                 HolyBoom, HolyHeld,  // W4M HolyGrenadeEx / HolyGrenadeHeld
                 BombWhistle, CowFall,  // W4M LaunchSfx of the air strike bombs and the Bovine Blitz cows
                 LockOn,  // W4M weapons/LockOn: the homing target is taken (0x560420)
                 Count };
// Startled..Drown: W4M acting-scene lines (docs/worm-reactions.md), voices/<bank>/<name>.ogg
enum class Voice { Fire, Hurt, Death, Victory, Jump, Idle,
                   Startled, Grenade, Shriek, Gasp, ShakeFist, Titter, Disbelief, Incoming, Missed, Mistake, Traitor, Damage,
                   FirstBlood, EnemyDeath, SadSigh, Yawn, Sneeze, ClutchChest, Nooo, Bounce, Taunt, Waiting, ShortOnTime, SkipGo,
                   Collect, CrateDrop, Drown, Revenge, Punch, DamageB, NoDamageA, NoDamageB, MaxDamage, PointAndLaugh, Count };

void init();
void shutdown();
void update();  // call once per frame: streams music
void listen(const Camera3D &cam);  // 3D events fade with the distance to it (W4M FEV min/max) and pan
// Gain, 3D range and max playbacks come from the W4M event table in audio.cpp; volume scales it.
void play(Sfx id, float volume = 1.0f);
void play(Sfx id, Vector3 at);  // 3D events attenuated at `at`, 2D ones as play(id)
void loop(Sfx id, bool on, const Vector3 *at = nullptr);  // call every frame: keeps one variant replaying while on
void voice(int team, Voice id);  // team i speaks with its bank (setTeamVoice), default i % bank count (banks = dirs under voices/)
void voice(int team, Voice id, Vector3 at);  // W4M speech is 3D
int voiceBanks();
const char *voiceBankName(int bank);  // folder name, "" if out of range
void setTeamVoice(int team, int bank);
void preloadVoices(int teams);  // load the banks of teams 0..teams-1 now instead of on their first line
// track = music/<track>.ogg (e.g. the map theme); null keeps the current track, unknown falls back to theme.ogg
void music(bool on, const char *track = nullptr);

}  // namespace Audio
