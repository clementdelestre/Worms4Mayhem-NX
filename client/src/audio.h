#pragma once

// Sound effects and voice lines. Missing asset files are silently skipped, never fatal.
// Each sound may have variants (name.ogg, name_2.ogg, ...); one is picked at random per play.
namespace Audio {

// Shotgun..SuperSheepFire are per-weapon fire sounds; they fall back to Fire when not imported.
enum class Sfx { Explosion, BigExplosion, Fire, Bounce, Splash, Jump, Sheep, Holy, TurnStart, Tick,
                 Shotgun, Airstrike, Donkey, Rope, Teleport,
                 BatSwing, FirePunch, Prod, Sniper, Bow, Homing, OldWomanFire, ScouserFire, SentryPlace, SentryFire,
                 Dynamite, Gas, Abduction, Flood, Parachute, MineBeep, CrateLand, Pickup, SuperSheepFire, Count };
enum class Voice { Fire, Hurt, Death, Victory, Jump, Idle, Count };

void init();
void shutdown();
void update();  // call once per frame: streams music
void play(Sfx id, float volume = 1.0f);
void voice(int team, Voice id);  // team i speaks with its bank (setTeamVoice), default i % bank count (banks = dirs under voices/)
int voiceBanks();
const char *voiceBankName(int bank);  // folder name, "" if out of range
void setTeamVoice(int team, int bank);
void preloadVoices(int teams);  // load the banks of teams 0..teams-1 now instead of on their first line
// track = music/<track>.ogg (e.g. the map theme); null keeps the current track, unknown falls back to theme.ogg
void music(bool on, const char *track = nullptr);

}  // namespace Audio
