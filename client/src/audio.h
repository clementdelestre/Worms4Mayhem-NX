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
                 FeBounce, FeSlide, FeNet, FeCustom, FeSoundVid, FeController, FeFactory, FeBookIn, FeBookOut, FeGrenade, FeWormpot, FeSpeech,
                 WormpotSpin, WormpotStop,
                 HolyBoom, HolyHeld,  // W4M HolyGrenadeEx / HolyGrenadeHeld
                 BombWhistle, CowFall,  // W4M LaunchSfx of the air strike bombs and the Bovine Blitz cows
                 PowerRocket, PowerHoming, PowerBow,  // W4M PowerbarMeterEntity 0x5f5c70: the charge sound, by weapon type
                 EquipAir, EquipBazooka, EquipBubble, EquipDefault, EquipPotion, EquipScouser, EquipShotgun, EquipSniper, EquipUmbrella,  // W4M EquipSfx
                 HeldSheep, HeldSentry, HeldScouser, HeldOldWoman,  // W4M HoldLoopSfx: SheepHeld (oneshot), SentryGunHeld (loop), ScouserHeld, OldWomanHeld
                 LockOn,  // W4M weapons/LockOn: the homing target is taken (0x560420)
                 UfoAppearing, UfoActive, UfoBeamLoop, UfoEngine, UfoTakeOff,  // W4M AlienAbductionGraphicEntity's weapons/AlienUfo* (BeamStart is Abduction)
                 BatImpact,  // weapons/BaseballBatImpact: WXP_AbdTelep_Central's EmitterSoundFX (an abductee's Zap)
                 BubbleInflate, BubbleWobble, BubbleLoop,  // W4M BubbleTroubleGraphicEntity: the bubble appears (0x54e920) / is hit (0x54e480); its machine runs
                 Throw, SecretLaunch,  // W4M weapons/Throw, weapons/SecretWeapLaunch: LaunchSfx of a Factory thrown / launched weapon (0x598fc0, 0x599167)
                 TickSlow,  // W4M weapons/ClockSlow (HudClockEntity 0x5efd80); Tick is ClockFast
                 BowImpact, ExplosionBoxed, DonkeyImpact,  // W4M weapons/ConcreteDonkeyImpact: WXP_DonkeyStrikeBounce's EmitterSoundFX; W4M Poison Arrow: ArmSfxLoop on impact; WXP_ExploArrow_RingDk's EmitterSoundFX (its DetonationSfx is empty)
                 Fireworks,  // W4M global/FireWorksExplosion (Firework1-3)
                 Buffalo,  // W4M weapons/BuffaloOfLies: the mystery crate reveal (BuffaloOfLiesGraphicEntity 0x551020)
                 Debris,  // W4M weapons/Debris: an explosion changed the land (0x4736c7)
                 Jetpack, JetpackEnd,  // W4M weapons/Jetpack (the jet loop, JetpackUtilityLogicEntity +0xf8) and weapons/JetpackEnd (0x562530)
                 FireLoop, SteamLoop, FliesLoop, ElecArc, ElectricArching, StormCloud, HoseIntoWater,  // PARTTWK EmitterSoundFX of map emitters
                 FloodRain, FloodThunder,  // weapons/FloodRainLoop (WXP_StormClouds): its rain loop and its delayed Thunder layer
                 FatkinsBounce, BananaBounce,  // WEAPTWK BounceSfx of kWeaponFatkins / kWeaponBananaBomb
                 Count };
// Startled..Drown: W4M acting-scene lines (docs/worm-reactions.md), voices/<bank>/<name>.ogg
enum class Voice { Fire, Hurt, Death, Victory, Jump, Idle,
                   Startled, Grenade, Shriek, Gasp, ShakeFist, Titter, Disbelief, Incoming, Missed, Mistake, Traitor, Damage,
                   FirstBlood, EnemyDeath, SadSigh, Yawn, Sneeze, ClutchChest, Nooo, Bounce, Taunt, Waiting, ShortOnTime, SkipGo,
                   Collect, CrateDrop, Drown, Revenge, Punch, DamageB, NoDamageA, NoDamageB, MaxDamage, PointAndLaugh, Count };

void init();
void shutdown();
void stopSfx();  // cut every sound effect and voice line (replay skipped, match left); music untouched
void update();  // call once per frame: streams music
void listen(const Camera3D &cam);  // 3D events fade with the distance to it (W4M FEV min/max) and pan
// Gain, 3D range and max playbacks come from the W4M event table in audio.cpp; volume scales it.
void play(Sfx id, float volume = 1.0f);
void play(Sfx id, Vector3 at);  // 3D events attenuated at `at`, 2D ones as play(id)
void equip(const char *weapon, Vector3 at);  // W4M WeaponAccessoryEntity 0x5950c0: the weapon's EquipSfx, none for most animals and melee
void hold(Sfx id, bool on, const Vector3 *at = nullptr);  // call every frame: one pass from the rising edge, cut at the falling edge
void loop(Sfx id, bool on, const Vector3 *at = nullptr, float volume = 1);  // call every frame: keeps one variant replaying while on
// a particle emitter's EmitterSoundFX loop (one voice per key), call every frame while it runs: past the event's max playbacks the
// oldest is stolen for good (FMOD steal oldest); a key not refreshed for a frame stops
void emitter(int key, Sfx id, Vector3 at);
void voice(int team, Voice id);  // team i speaks with its bank (setTeamVoice), default i % bank count (banks = dirs under voices/)
void voice(int team, Voice id, Vector3 at);  // W4M speech is 3D
int voiceBanks();
unsigned started();  // sounds started so far (hitch log)
const char *voiceBankName(int bank);  // folder name, "" if out of range
void setTeamVoice(int team, int bank);
void preloadVoices(int teams);  // load the banks of teams 0..teams-1 now instead of on their first line
// track = music/<track>.ogg (e.g. the map theme); null keeps the current track, unknown falls back to theme.ogg
void music(bool on, const char *track = nullptr);
void preloadMusic(const char *track);  // open the stream now (an SD read of ~40 ms on Switch); its next music() call takes it

}  // namespace Audio
