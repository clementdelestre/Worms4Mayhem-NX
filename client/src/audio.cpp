#include <cstring>
#include "audio.h"
#include "loading.h"
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef __SWITCH__
#define ASSET_ROOT "sdmc:/switch/worms4nx/assets/"
#define ROMFS_ROOT "romfs:/"
#else
#define ASSET_ROOT "./assets/"
#define ROMFS_ROOT "./romfs/"
#endif

namespace Audio {
namespace {

// importer drops extracted W4M sounds under ASSET_ROOT; bundled CC0 defaults live in ROMFS_ROOT
const char *SFX_NAMES[] = {
    "explosion", "big_explosion", "fire", "bounce", "splash", "jump", "sheep", "holy", "turn_start", "tick",
    "shotgun",   "airstrike",     "donkey", "rope", "teleport",
    "bat_swing", "fire_punch", "prod", "sniper", "bow", "homing", "old_woman", "scouser", "sentry_place", "sentry_fire",
    "dynamite", "gas", "abduction", "flood", "parachute", "mine_beep", "crate_land", "pickup", "super_sheep",
    "step", "land", "hp_tick",
    "crate_impact_health", "crate_impact_weapon", "crate_impact_util", "cheer",
    "fe_highlight", "fe_change", "fe_click", "fe_cancel", "fe_error", "fe_type", "fe_page", "fe_popup_in", "fe_popup_out", "fe_next_in", "fe_next_out",
    "fe_prev_in", "fe_prev_out", "fe_bounce", "fe_slide", "fe_net", "fe_custom", "fe_soundvid", "fe_controller", "fe_factory",
    "fe_book_in", "fe_book_out", "fe_grenade", "fe_wormpot", "fe_speech", "wormpot_spin", "wormpot_stop",
    "holy_boom", "holy_held",
    "bomb_whistle", "cow_fall", "power_rocket", "power_homing", "power_bow",
    "equip_air", "equip_bazooka", "equip_bubble", "equip_default", "equip_potion", "equip_scouser", "equip_shotgun", "equip_sniper", "equip_umbrella",
    "held_sheep", "held_sentry", "held_scouser", "held_old_woman", "lock_on",
    "ufo_appearing", "ufo_active", "ufo_beam", "ufo_engine", "ufo_takeoff", "bat_impact", "bubble_inflate", "bubble_wobble", "bubble_loop", "throw", "secret_launch",
    "tick_slow", "bow_impact", "explosion_boxed", "donkey_impact", "fireworks", "buffalo", "debris", "jetpack", "jetpack_end",
    "fire_loop", "steam_loop", "flies_loop", "elec_arc", "electric_arcing", "storm_cloud", "hose_into_water",
    "flood_rain", "flood_thunder", "fatkins_bounce", "banana_bounce", "mine_machine",
};
static_assert(sizeof SFX_NAMES / sizeof *SFX_NAMES == (size_t)Sfx::Count, "one file per Sfx");
// W4M WormsX.fev via tools/w4m-re/fev.py (docs/w4m/audio.md §12): event, dB (event + sounddef + category), loop, 3D rolloff min..max m (0 = 2D), max playbacks,
// fade s, wave weights (null: equal), sounddef mode (pick), trigger delay / respawn ms (+64/+66, +4/+8), log rolloff, pitch spread (+08, x 4 octaves)
// env: emitter() gain over time (Time-param envelope x event fade-in), (s, gain) pairs ending at s < 0; layer: oneshot layer started with it
struct Def { const char *event; float db; bool loop; float min, max; int maxpb; float fade = 0; const int *w = nullptr; int mode = 1; int delay[2] = {}, spawn[2] = {}; bool log = false; float pitchRand = 0;
             const float *env = nullptr; int layer = -1; };
// FloodRainLoop: Time 0..15 at 1 unit/s, volume 0.5 to 0.531 x 15 s then 0 at 0.7997 x 15 s, x the 2 s event fade-in (y read as linear gain)
const float FLOOD_ENV[] = {0, 0, 2, 0.5f, 7.965f, 0.5f, 11.995f, 0, -1};
// every other multi-wave def has equal weights in the FEV (100 each, 20 on OldWomanMutter)
const int W_SCOUSER_HELD[] = {100, 300, 100};
const Def DEFS[] = {
    {"global/ExplosionRegular", -3, false, 0.5f, 50, 4, 0, nullptr, 2},
    {"weapons/ExplosionLarge", -12, false, 0.5f, 40, 1},
    {"weapons/RocketRelease", -6, false, 0, 0, 1},
    {"weapons/GrenadeBounce", -2, false, 0.5f, 60, 1, 0, nullptr, 6},
    {"weapons/SplashHeavy", 0, false, 0.5f, 70, 2, 0, nullptr, 2},
    {"(none: CC0 jump)", 0, false, 0, 0, 1},
    {"weapons/SheepBaa", -3, false, 0.5f, 25, 1},
    {"weapons/Hallelujah", 0, false, 0, 0, 1},
    {"weapons/HudAlert", -10, false, 0, 0, 1},
    {"weapons/ClockFast", -2, true, 0, 0, 1},
    {"weapons/ShotgunFire", -5, false, 0, 0, 1, 0, nullptr, 2},
    {"weapons/Bomber", -9, true, 0.5f, 60, 1, 0.5f},
    {"weapons/ConcreteDonkeyRelease", -1, false, 0.5f, 100, 1},
    {"weapons/NinjaRopeFire", 0, false, 0.5f, 25, 1},
    {"weapons/Teleport", -8, false, 0.5f, 25, 1, 0.35f, nullptr, 2},
    {"weapons/BaseballBarSwing", 0, false, 0, 0, 1},
    {"weapons/FirePunch", -6, false, 0.5f, 25, 1},
    {"weapons/Prod", -4, false, 0, 0, 1},
    {"weapons/SniperRifleFire", -3, false, 0, 0, 1},
    {"weapons/BowRelease", 0, false, 0.5f, 60, 1},
    {"weapons/MissileLoop", 0, false, 5, 75, 1, 0, nullptr, 1, {1000, 1000}},  // FEV loop; the 5.03 s Time envelope is applied by the caller (Bazooka / Homing shot), one pass of the 5.85 s clip
    {"weapons/OldWomenLaunch", 0, false, 0.5f, 60, 1},
    {"weapons/ScouserLaunch", 0, false, 0.5f, 60, 1},
    {"weapons/SentryGunHeld", -8, true, 0.5f, 20, 1},
    {"weapons/SentryGun", -3, true, 0.5f, 100, 1},
    {"weapons/FuseLoop", -4, true, 0.5f, 25, 1},
    {"weapons/GasLoop", -16, false, 0.5f, 15, 1},
    {"weapons/AlienUfoBeamStart", 0, false, 0.5f, 100, 1},
    {"weapons/RainLoop", -6, true, 0, 0, 1, 2},
    {"weapons/ParachuteLoop", -2, false, 0.5f, 25, 1},  // the Open layer, a oneshot
    {"weapons/MineArmLoop", 0, true, 5, 25, 1},
    {"weapons/CrateSpawn", -4, false, 0.5f, 60, 1},
    {"weapons/PickupWeapon", -11, false, 0.5f, 25, 1},  // PickupUtil -11, PickupHealthCrate -6
    {"weapons/WingFlap", -3, false, 0.5f, 25, 1, 0, nullptr, 2},
    {"weapons/OldWomenFootsteps", -6, false, 0.5f, 60, 1, 0, nullptr, 2, {120, 120}, {0, 1}},
    {"weapons/Thud", 0, false, 0.5f, 25, 2, 0, nullptr, 2},
    {"global/click3", 0, false, 0, 0, 1},
    {"weapons/CrateImpactHealth", -2, false, 0.5f, 60, 1},
    {"weapons/CrateImpactWeapon", -2, false, 0.5f, 60, 1},
    {"weapons/CrateImpactUtil", -3, false, 0.5f, 60, 1},
    {"cheer/cheer", -12.9f, true, 0, 0, 1},
    {"global/Highlight", 0, false, 0, 0, 1},
    {"global/click2", 0, false, 0, 0, 1},
    {"frontendsfx/click", 0, false, 0, 0, 1},  // priority 64, the only menu sound below 128: irrelevant without a voice limit
    {"frontendsfx/Cancel", 0, false, 0, 0, 1},
    {"global/FEError", 0, false, 0, 0, 1},
    {"global/Typewriter", 0, false, 0, 0, 1},
    {"frontendsfx/PageTurn", 0, false, 0, 0, 1},
    {"global/In_ScaleY", 0, false, 0, 0, 1},
    {"frontendsfx/Out_ScaleY", 0, false, 0, 0, 1},
    {"frontendsfx/In_Next", 0, false, 0, 0, 1},
    {"frontendsfx/Out_Next", 0, false, 0, 0, 1},
    {"frontendsfx/In_Prev", 0, false, 0, 0, 1},
    {"frontendsfx/Out_Prev", 0, false, 0, 0, 1},
    {"frontendsfx/In_BigBounce", 0, false, 0, 0, 1},
    {"frontendsfx/In_SlideX", 0, false, 0, 0, 1},
    {"frontendsfx/In_Net", 0, false, 0, 0, 1},
    {"frontendsfx/In_Custom", 0, false, 0, 0, 1},
    {"frontendsfx/In_SoundVid", 0, false, 0, 0, 1},
    {"global/In_Controller", 0, false, 0, 0, 1},
    {"frontendsfx/In_WeaponFactory", 0, false, 0, 0, 1},
    {"frontendsfx/In_Book", 0, false, 0, 0, 1},
    {"frontendsfx/Out_Book", -INFINITY, false, 0, 0, 1},  // event volume 0: silent in W4M
    {"frontendsfx/grenade", 0, false, 0, 0, 1},
    {"frontendsfx/In_Wormpot", 0, false, 0, 0, 1},
    {"frontendsfx/In_Speech", 0, false, 0, 0, 1},
    {"frontendsfx/WormPotLoop", 0, true, 0, 0, 1},
    {"frontendsfx/WormPotStop", 0, false, 0, 0, 1},
    {"weapons/HolyGrenadeExplosion", -1, false, 0, 0, 1},
    {"weapons/HolyGrenadeHeld", -10, true, 0.5f, 25, 1, 0.35f},
    {"weapons/BombWhistle", -14, false, 0.5f, 40, 6},
    {"weapons/CowFall", -6, false, 0.5f, 25, 2, 0, nullptr, 2},
    {"weapons/RocketPowerUp", -6, false, 0.5f, 60, 1},  // 3D linear 10..1200 units
    {"weapons/HomingMissilePowerUp", -6, false, 0.5f, 25, 1},
    {"weapons/BowCreak", -2, false, 0.5f, 25, 1},
    {"weapons/AirEquip", -12, false, 0.5f, 500, 1},  // the Equip events: 3D linear 1..10000 units, sound definition -12 dB
    {"weapons/BazookaEquip", -12, false, 0.5f, 500, 1},
    {"weapons/BubbleEquip", -12, false, 0.5f, 500, 1},
    {"weapons/DefaultEquip", -12, false, 0.5f, 500, 1},
    {"weapons/PotionEquip", -11, false, 0, 0, 1},  // event -9 dB, 2D
    {"weapons/ScouserArm", -7, false, 0.5f, 25, 1},
    {"weapons/ShotgunEquip", -12, false, 0.5f, 500, 1},
    {"weapons/SniperEquip", -12, false, 0.5f, 500, 1},
    {"weapons/UmbrellaOpen", -12, false, 0, 0, 1},  // 2D
    {"weapons/SheepHeld", -9.2f, false, 0.5f, 25, 1, 0, nullptr, 3, {1000, 3000}, {0, 1}, true},
    {"weapons/SentryGunHeld", -8, true, 0.5f, 20, 1, 0.35f},
    {"weapons/ScouserHeld", 0, false, 0.5f, 60, 1, 0, W_SCOUSER_HELD, 0, {200, 1200}, {0, 1}},
    {"weapons/OldWomanHeld", -7, false, 0.5f, 60, 1, 0, nullptr, 0, {200, 1600}, {0, 1}},
    {"weapons/LockOn", 0, false, 0, 0, 1},  // sample TargetAquired, 2D
    {"weapons/AlienUfoAppearing", 0, false, 0.5f, 100, 1},  // the UFO events: 3D linear 10..2000 units; TakeOff 2D
    {"weapons/AlienUfoActive", 0, false, 0.5f, 100, 1},
    {"weapons/AlienUfoBeamLoop", 0, true, 0.5f, 100, 1, 0.5f},
    {"weapons/AlienUfoEngineLoop", 0, true, 0.5f, 100, 1, 0.5f},
    {"weapons/AlienUFOTakeOff", 0, false, 0, 0, 1},
    {"weapons/BaseballBatImpact", 0, false, 0.5f, 25, 1},  // 3D linear 10..500 units
    {"weapons/BubbleMachineInflate", -2, false, 0.5f, 25, 1},  // sample BubbleMachinePlace; both 3D linear 10..500 units
    {"weapons/BubbleMachineWobble", -2, false, 0.5f, 25, 1},
    {"weapons/BubbleMachineLoop", -22, false, 0.5f, 20, 1, 0, nullptr, 2, {}, {500, 500}},  // 3D linear 10..400 units; one of Bubble1-6 per 500 ms spawn
    {"weapons/Throw", 0, false, 0.5f, 25, 1},  // 3D linear 10..500 units
    {"weapons/SecretWeapLaunch", 0, false, 0, 0, 1},  // 2D
    {"weapons/ClockSlow", -2, true, 0, 0, 1},
    {"weapons/BowImpact", 0, false, 0.5f, 100, 1},  // 3D linear 10..2000 units
    {"weapons/ExplosionBoxed", -2, false, 0, 0, 4},  // 2D
    {"weapons/ConcreteDonkeyImpact", 0, false, 0, 0, 1, 0, nullptr, 2},  // 2D, 3 waves
    {"global/FireWorksExplosion", 0, false, 0.5f, 25, 1, 0, nullptr, 2, {}, {}, true},  // EmitterSoundFX of the WXPF_ / Starburst bangs
    {"weapons/BuffaloOfLies", -2, false, 0.5f, 25, 1},
    {"weapons/Debris", -12, false, 0.5f, 100, 1, 0, nullptr, 2, {}, {}, false, 0.025f},
    {"weapons/JetPack", -3, true, 0.5f, 60, 1},  // 3D linear 10..1200 units
    {"weapons/JetPackEnd", -3, false, 0.5f, 60, 1},
    {"weapons/FireLoop", -10, true, 0.5f, 25, 4, 0.5f},  // 3D linear 10..500 units, fade out 500 ms
    {"weapons/SteamLoop", -15, true, 0.5f, 25, 4, 0.5f},
    {"weapons/FliesLoop", -12, true, 0.05f, 10, 1},  // 1..200 units
    {"weapons/ElecArc", -6, true, 0.5f, 15, 1},  // 10..300 units
    {"weapons/ElectricArching", 0, true, 0.5f, 25, 1},
    {"weapons/StormCloud", 0, false, 0.5f, 80, 4, 0.5f, nullptr, 2},  // ThunderClaps x 5, 10..1600 units
    {"weapons/HoseIntoWater", 0, true, 0.05f, 35, 1},  // TapIntoWater, 1..700 units
    {"weapons/FloodRainLoop", -2, true, 0, 0, 1, 0, nullptr, 3, {}, {}, false, 0, FLOOD_ENV, (int)Sfx::FloodThunder},  // 2D, layer RainLoop -2 dB
    {"weapons/FloodRainLoop (Thunder layer)", 0, false, 0, 0, 1, 0, nullptr, 3, {1500, 1500}},  // oneshot, sounddef delay 1500 ms
    {"weapons/FatkinsBounce", -3, false, 0, 0, 1, 0, nullptr, 2},  // 2D, FatkinsBounce1-2
    {"weapons/BananaBombImpact", -6, false, 0.5f, 60, 1},  // 3D linear 10..1200 units
    {"weapons/MineMachineOperate", -10, true, 0, 0, 1, 0.35f},  // 2D loop, fades 350 ms
};
static_assert(sizeof DEFS / sizeof *DEFS == (size_t)Sfx::Count, "one W4M event per Sfx");
// Speech/<voice>/*: 0 dB, 3D 0.5..50 m, one playback per event; SadSigh and Yawn -2.5 dB, 0.5..22.5 m
constexpr Def SPEECH = {"Speech/*", 0, false, 0.5f, 50, 1}, SPEECH_SOFT = {"Speech/*/SadSigh|Yawn", -2.5f, false, 0.5f, 22.5f, 1};
// music/<Theme>: sound definition + category music (-6 dB); every track loops but Victory
struct Track { const char *name; float db; };
const Track TRACKS[] = {{"theme", -6}, {"victory", -6}, {"arabian", -9}, {"wildwest", -9}, {"suddendeath", -9}};  // others -12
float trackDb(const std::string &t) {
    for (const Track &k : TRACKS) if (t == k.name) return k.db;
    return -12;
}
// Fallback when a bank lacks a line (the CC0 romfs banks hold only the first six): the closest category, followed until found
const Voice FALLBACK[(int)Voice::Count] = {
    Voice::Fire, Voice::Hurt, Voice::Death, Voice::Victory, Voice::Jump, Voice::Idle,
    Voice::Hurt, Voice::Startled, Voice::Hurt, Voice::Startled, Voice::Fire, Voice::Victory, Voice::Hurt, Voice::Startled,
    Voice::Disbelief, Voice::Disbelief, Voice::ShakeFist, Voice::Taunt,
    Voice::Damage, Voice::Titter, Voice::Hurt, Voice::Idle, Voice::Hurt, Voice::Death, Voice::Death, Voice::Hurt, Voice::Fire,
    Voice::Idle, Voice::Idle, Voice::Idle, Voice::Victory, Voice::Idle, Voice::Death,
    Voice::Taunt, Voice::Fire, Voice::Damage, Voice::Missed, Voice::Missed, Voice::Victory, Voice::Titter};
const char *VOICE_NAMES[(int)Voice::Count] = {"fire", "hurt", "death", "victory", "jump", "idle",
    "startled", "grenade", "shriek", "gasp", "shakefist", "titter", "disbelief", "incoming", "missed", "mistake", "traitor", "damage",
    "firstblood", "enemydeath", "sadsigh", "yawn", "sneeze", "clutchchest", "nooo", "bounce", "taunt", "waiting", "shortontime", "skipgo",
    "collect", "cratedrop", "drown", "revenge", "punch", "damageb", "nodamagea", "nodamageb", "maxdamage", "pointandlaugh"};
constexpr int MAX_VARIANTS = 12;

// Each variant has maxpb voices (aliases past the first), so up to maxpb plays overlap like FMOD's max playbacks.
struct Slot { Sound s; int variant; unsigned born; };
struct Variants {
    std::vector<Slot> slot;
    int n = 0;
};

struct Bank {
    std::string dir;
    bool loaded = false;
    Variants lines[(int)Voice::Count];
};

Variants sfx[(int)Sfx::Count];
std::vector<Bank> banks;
std::vector<int> teamBank;  // team -> bank, -1 = default
Music theme, outgoing;
float outGain = 0;  // femusic fade-out (FEV 2000 ms) of the track being replaced or stopped
bool stopping = false;
std::string track;
bool musicLoaded = false, musicOn = false;
// W4M Music.FadeIn (FrontEndService 0x7290b4): +0.01 per frame up to Audio.Vol.Music 0.6 (DEFSAVE), 1 s at 60 fps
float fade = 1;
Vector3 ear{}, earRight{1, 0, 0};
unsigned plays = 0;

// base.ogg, base_2.ogg, ... until the first gap. No TextFormat: init and preloadVoices run on loading threads.
Variants loadVariants(const std::string &base, int maxpb) {
    Variants v;
    for (; v.n < MAX_VARIANTS; v.n++) {
        std::string p = v.n ? base + "_" + std::to_string(v.n + 1) + ".ogg" : base + ".ogg";
        if (!FileExists(p.c_str())) break;
        Sound s = LoadSound(p.c_str());
        for (int k = 0; k < maxpb; k++) v.slot.push_back({k ? LoadSoundAlias(s) : s, v.n, 0});
    }
    return v;
}

void unload(Variants &v) {
    for (size_t i = 0; i < v.slot.size(); i++)
        if (i && v.slot[i].variant == v.slot[i - 1].variant) UnloadSoundAlias(v.slot[i].s);
        else UnloadSound(v.slot[i].s);
    v.slot.clear(), v.n = 0;
}

// FMOD 3D rolloff: linear (full inside min, silent past max) or log (min / distance, constant past max); pan from the listener's right
float place(Sound s, const Def &d, float gain, const Vector3 *at) {
    float pan = 0;
    if (at && d.max > 0) {
        Vector3 to = Vector3Subtract(*at, ear);
        float dist = Vector3Length(to);
        gain *= d.log ? d.min / Clamp(dist, d.min, d.max) : Clamp((d.max - dist) / (d.max - d.min), 0, 1);
        if (dist > 1e-3f) pan = Vector3DotProduct(to, earRight) / dist;
    }
    SetSoundVolume(s, gain), SetSoundPan(s, pan);
    return gain;
}
// FEV event fade-in on a fire-and-forget start: the volume ramps linearly over the event's fade time
struct Ramp { Sound s; double t0; float gain, len; };
std::vector<Ramp> ramps;

// FMOD sounddef play mode (fmod_event.dll selector 0x10038670): 0 and 3 sequential per event instance (from wave 1 / wave 0),
// 1 weighted random, 2 random without repeating the last wave, 4 per-instance shuffle, 6 global shuffle (7 global sequential: unused).
struct State { int last = 0, cur = 0; std::vector<int> perm; };  // last: previous wave + 1, 0 = none
std::map<const Def *, State> state;  // keyed by Def: SPEECH defs live outside DEFS
void restart(const Def &d) { state[&d].cur = 0; }
int pick(int n, const Def &d) {
    if (n < 2) return 0;
    State &st = state[&d];
    int &last = st.last, &cur = st.cur;
    std::vector<int> &perm = st.perm;
    auto wt = [&](int i) { return d.w ? d.w[i] : 1; };  // d.w has one weight per variant
    if (d.mode == 0) return cur = (cur + 1) % n;  // per event instance (reset by restart), from wave 1: state 0 steps before playing
    if (d.mode == 3) return 0;
    if (d.mode == 6) {  // exe shuffle 0x10038870: reshuffle when spent, never starting with the last wave played
        if (perm.size() != (size_t)n || cur + 1 >= n) {
            int prev = perm.size() == (size_t)n ? perm[n - 1] : -1;
            perm.resize(n);
            for (int i = 0; i < n; i++) perm[i] = i;
            for (int i = 0; i < n; i++) std::swap(perm[i], perm[i + GetRandomValue(0, n - 1 - i)]);
            if (perm[0] == prev) std::swap(perm[0], perm[1 + GetRandomValue(0, n - 2)]);
            cur = 0;
        } else cur++;
        return perm[cur];
    }
    int k = 0, tot = 0;
    for (int i = 0; i < n; i++) tot += wt(i);
    for (int r = GetRandomValue(0, tot - 1); k < n - 1 && (r -= wt(k)) >= 0; k++) {}
    if (d.mode == 2 && k + 1 == last) k = (k + 1) % n;
    last = k + 1;
    return k;
}

// past maxpb playing voices the oldest is cut (FMOD max playbacks behaviour 1, steal oldest)
void playRandom(Variants &v, const Def &d, float volume, const Vector3 *at, bool ramp = false) {
    if (!v.n) return;
    int k = pick(v.n, d);
    int busy = 0;
    Slot *oldest = nullptr, *free = nullptr;
    for (Slot &x : v.slot)
        if (IsSoundPlaying(x.s)) busy++, oldest = !oldest || x.born < oldest->born ? &x : oldest;
        else if (x.variant == k && !free) free = &x;
    if (busy >= d.maxpb && oldest) StopSound(oldest->s), free = free ? free : oldest;
    if (!free) return;
    float g = place(free->s, d, volume * powf(10, d.db / 20), at);
    free->born = ++plays;
    if (d.pitchRand > 0) SetSoundPitch(free->s, exp2f(4 * d.pitchRand * (GetRandomValue(0, 32767) / 16383.5f - 1)));
    if (ramp && d.fade > 0) SetSoundVolume(free->s, 0), ramps.push_back({free->s, GetTime(), g, d.fade});
    PlaySound(free->s);
}

// fmod_event 0x10038400: a trigger delay of min + rand() % (max - min) ms (min when equal) before each sound starts (Channel delay)
struct Pending { Variants *v; const Def *d; float vol; bool has; Vector3 at; double due; int id; };
std::vector<Pending> pending;
int between(const int *r) { return r[0] == r[1] ? r[0] : r[0] + GetRandomValue(0, r[1] - r[0] - 1); }
void trigger(Variants &v, const Def &d, float vol, const Vector3 *at, int id = -1) {
    if (int ms = between(d.delay)) return pending.push_back({&v, &d, vol, at != nullptr, at ? *at : Vector3{}, GetTime() + ms / 1000.0, id});
    playRandom(v, d, vol, at, true);
}

std::vector<std::string> bankDirs(const char *root) {
    std::vector<std::string> dirs;
    std::string voices = std::string(root) + "voices";
    if (!DirectoryExists(voices.c_str())) return dirs;
    FilePathList l = LoadDirectoryFilesEx(voices.c_str(), "DIRS*", false);
    for (unsigned i = 0; i < l.count; i++) dirs.push_back(l.paths[i]);
    UnloadDirectoryFiles(l);
    std::sort(dirs.begin(), dirs.end());  // stable team -> bank mapping across runs
    return dirs;
}

// frontendmusic/femusic ("theme") has a 2000 ms event fade-out; the level tracks have none
void leave() {
    if (outGain > 0) UnloadMusicStream(outgoing), outGain = 0;
    if (track == "theme" && IsMusicStreamPlaying(theme)) outgoing = theme, outGain = fade * powf(10, trackDb(track) / 20);
    else UnloadMusicStream(theme);
}

std::vector<std::pair<std::string, Music>> ready;  // preloadMusic(): opened streams waiting for their first play

Music loadMusic(const char *name) {
    for (size_t i = 0; i < ready.size(); i++)
        if (ready[i].first == name) {
            Music m = ready[i].second;
            ready.erase(ready.begin() + i);
            return m;
        }
    for (const char *root : {ASSET_ROOT, ROMFS_ROOT}) {
        std::string s = std::string(root) + "music/" + name + ".ogg";
        if (!FileExists(s.c_str())) continue;
        Music m = LoadMusicStream(s.c_str());
        if (IsMusicValid(m)) return m;
    }
    return Music{};
}

bool openMusic(const char *name) {
    Music m = loadMusic(name);
    if (!IsMusicValid(m)) return false;
    if (musicLoaded) leave();
    theme = m;
    theme.looping = std::string(name) != "victory";  // jingle: once, then silence
    musicLoaded = true;
    track = name;
    return true;
}

}  // namespace

void init() {
    InitAudioDevice();
    auto load = [](int first) {  // two decoders: the OGG decode is most of the boot after the models
        for (int i = first; i < (int)Sfx::Count; i += 2) {
            sfx[i] = loadVariants(std::string(ASSET_ROOT "sfx/") + SFX_NAMES[i], DEFS[i].maxpb);
            if (!sfx[i].n) sfx[i] = loadVariants(std::string(ROMFS_ROOT "sfx/") + SFX_NAMES[i], DEFS[i].maxpb);
        }
    };
    std::thread odd(load, 1);
    load(0), odd.join();
    std::vector<std::string> dirs = bankDirs(ASSET_ROOT);
    if (dirs.empty()) dirs = bankDirs(ROMFS_ROOT);
    for (auto &d : dirs) banks.push_back(Bank{d, false, {}});
    SetAudioStreamBufferSizeDefault(16384);  // ~370 ms per half: rides out long frames
    openMusic("theme");
}

void shutdown() {
    for (auto &v : sfx) unload(v);
    for (auto &b : banks) for (auto &v : b.lines) unload(v);
    banks.clear();
    if (musicLoaded) UnloadMusicStream(theme);
    if (outGain > 0) UnloadMusicStream(outgoing), outGain = 0;
    for (auto &r : ready) UnloadMusicStream(r.second);
    ready.clear();
    musicLoaded = false;
    CloseAudioDevice();
}

void stopSfx() {
    for (auto &v : sfx) for (Slot &k : v.slot) StopSound(k.s);
    for (auto &b : banks) for (auto &v : b.lines) for (Slot &k : v.slot) StopSound(k.s);
    pending.clear(), ramps.clear();
}

struct EmitterVoice { Sfx id; Slot *slot; unsigned born, frame; bool stolen; double t0; };
std::map<int, EmitterVoice> emitters;
unsigned emitterFrame = 0;

void update() {
    for (auto it = emitters.begin(); it != emitters.end();)  // emitters that stopped calling emitter()
        if (it->second.frame + 1 < emitterFrame) {
            if (!it->second.stolen) StopSound(it->second.slot->s);
            if (int l = DEFS[(int)it->second.id].layer; l >= 0) {  // the event's layers stop with it
                for (Slot &k : sfx[l].slot) StopSound(k.s);
                pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Pending &p) { return p.id == l; }), pending.end());
            }
            it = emitters.erase(it);
        }
        else ++it;
    emitterFrame++;
    for (size_t i = 0; i < pending.size();)
        if (Pending p = pending[i]; GetTime() >= p.due) pending.erase(pending.begin() + i), playRandom(*p.v, *p.d, p.vol, p.has ? &p.at : nullptr, true);
        else i++;
    if (outGain > 0) {
        outGain -= GetFrameTime() / 2 * powf(10, trackDb("theme") / 20);
        if (outGain <= 0) UnloadMusicStream(outgoing), outGain = 0;
        else SetMusicVolume(outgoing, outGain), UpdateMusicStream(outgoing);
    }
    for (size_t i = 0; i < ramps.size();) {
        Ramp &r = ramps[i];
        float k = (float)((GetTime() - r.t0) / r.len);
        if (k >= 1 || !IsSoundPlaying(r.s)) { if (IsSoundPlaying(r.s)) SetSoundVolume(r.s, r.gain); ramps.erase(ramps.begin() + i); }
        else SetSoundVolume(r.s, r.gain * k), i++;
    }
    if (stopping && musicLoaded) {
        if ((fade -= GetFrameTime() / 2) <= 0) fade = 0, stopping = false, StopMusicStream(theme);
        else SetMusicVolume(theme, fade * powf(10, trackDb(track) / 20)), UpdateMusicStream(theme);
        return;
    }
    if (!musicLoaded || !musicOn) return;
    if (fade < 1) fade = fminf(fade + GetFrameTime(), 1), SetMusicVolume(theme, fade * powf(10, trackDb(track) / 20));
    UpdateMusicStream(theme);
}

void listen(const Camera3D &cam) {
    ear = cam.position;
    earRight = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(cam.target, cam.position), cam.up));
}

static void play(Sfx id, float volume, const Vector3 *at) {
    Variants *v = &sfx[(int)id];
    if (!v->n && id > Sfx::Tick && id <= Sfx::SuperSheepFire) v = &sfx[(int)Sfx::Fire];
    trigger(*v, DEFS[(int)id], volume, at, (int)id);
}
void play(Sfx id, float volume) { play(id, volume, nullptr); }
void play(Sfx id, Vector3 at) { play(id, 1, &at); }

void equip(const char *w, Vector3 at) {
    struct E { const char *name; Sfx id; };  // WEAPTWK EquipSfx, played by the WAE_* classes only (0x5950c0): none without one (Fatkins, Girder, Jetpack, Parachute)
    static const E T[] = {
        {"Bazooka", Sfx::EquipBazooka}, {"Homing Missile", Sfx::EquipBazooka}, {"Sentry Gun", Sfx::EquipBazooka},
        {"Airstrike", Sfx::EquipAir}, {"Super Airstrike", Sfx::EquipAir}, {"Concrete Donkey", Sfx::EquipAir}, {"Alien Abduction", Sfx::EquipAir},
        {"Flood", Sfx::EquipUmbrella}, {"Inflatable Scouser", Sfx::EquipScouser}, {"Shotgun", Sfx::EquipShotgun}, {"Sniper Rifle", Sfx::EquipSniper},
        {"Bubble Trouble", Sfx::EquipBubble}, {"Icarus Potion", Sfx::EquipPotion},
        {"Grenade", Sfx::EquipDefault}, {"Cluster Grenade", Sfx::EquipDefault}, {"Banana Bomb", Sfx::EquipDefault}, {"Holy Hand Grenade", Sfx::EquipDefault},
        {"Poison Arrow", Sfx::EquipDefault}, {"Dynamite", Sfx::EquipDefault}, {"Gas Canister", Sfx::EquipDefault}, {"Landmine", Sfx::EquipDefault},
        {"Old Woman", Sfx::EquipDefault}, {"Baseball Bat", Sfx::EquipDefault}, {"Tail Nail", Sfx::EquipDefault}, {"Ninja Rope", Sfx::EquipDefault},
        {"Surrender", Sfx::EquipDefault},
    };
    for (const E &e : T) if (!strcmp(w, e.name)) return play(e.id, at);
}

// One event instance while on. fmod_event 0x10019c40: a oneshot instance with spawn max > 0 respawns once fewer than max spawned
// sounds (1) play, delayed ones included, and its countdown (always running) is out; it then adds min + rand() % (max - min) ms
void hold(Sfx id, bool on, const Vector3 *at) {
    static bool was[(int)Sfx::Count];
    static float wait[(int)Sfx::Count];
    const Def &d = DEFS[(int)id];
    bool busy = false;
    for (Pending &p : pending)
        if (p.id == (int)id) busy = true, p.has = at != nullptr, p.at = at ? *at : Vector3{};
    if (!on) pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Pending &p) { return p.id == (int)id; }), pending.end());
    for (Slot &k : sfx[(int)id].slot) {
        if (!on) StopSound(k.s);
        else if (IsSoundPlaying(k.s)) busy = true, place(k.s, d, powf(10, d.db / 20), at);
    }
    float &w = wait[(int)id];
    w = fmaxf(0, w - GetFrameTime() * 1000);
    if (on && !was[(int)id]) restart(d), w = 0;
    if (on && (!was[(int)id] || (d.spawn[1] > 0 && !busy && w <= 0))) play(id, 1, at), w += between(d.spawn);
    was[(int)id] = on;
}

void emitter(int key, Sfx id, Vector3 at) {
    Variants &v = sfx[(int)id];
    const Def &d = DEFS[(int)id];
    auto it = emitters.find(key);
    if (it == emitters.end()) {
        if (!v.n) return;
        std::vector<EmitterVoice *> mine;
        for (auto &[k, e] : emitters) if (e.id == id && !e.stolen) mine.push_back(&e);
        if ((int)mine.size() >= d.maxpb) {
            EmitterVoice *old = *std::min_element(mine.begin(), mine.end(), [](auto *a, auto *b) { return a->born < b->born; });
            StopSound(old->slot->s), old->stolen = true;
        }
        Slot *free = nullptr;
        int k = pick(v.n, d);
        for (Slot &x : v.slot) {
            bool used = std::any_of(emitters.begin(), emitters.end(), [&](auto &e) { return !e.second.stolen && e.second.slot == &x; });
            if (!used && (!free || x.variant == k)) free = &x;
        }
        if (!free) return;
        it = emitters.emplace(key, EmitterVoice{id, free, ++plays, emitterFrame, false, GetTime()}).first;
        if (d.layer >= 0) trigger(sfx[d.layer], DEFS[d.layer], 1, &at, d.layer);
    }
    EmitterVoice &e = it->second;
    e.frame = emitterFrame;
    if (e.stolen) return;
    float g = d.env ? d.env[1] : 1, t = (float)(GetTime() - e.t0);
    for (const float *p = d.env; p && p[2] >= 0; p += 2)  // piecewise linear, held at both ends
        if (t >= p[0]) g = t < p[2] ? p[1] + (p[3] - p[1]) * (t - p[0]) / (p[2] - p[0]) : p[3];
    place(e.slot->s, d, g * powf(10, d.db / 20), &at);
    if (!IsSoundPlaying(e.slot->s)) PlaySound(e.slot->s);
}

void loop(Sfx id, bool on, const Vector3 *at, float volume) {
    Variants &v = sfx[(int)id];
    const Def &d = DEFS[(int)id];
    static float level[(int)Sfx::Count];
    static Vector3 last[(int)Sfx::Count];  // where a fading-out loop was last placed
    float &g = level[(int)id];
    if (at) last[(int)id] = *at;
    g = d.fade > 0 ? Clamp(g + (on ? 1 : -1) * GetFrameTime() / d.fade, 0, 1) : on;
    if (g <= 0) { for (Slot &k : v.slot) StopSound(k.s); return; }
    for (Slot &k : v.slot) if (IsSoundPlaying(k.s)) return void(place(k.s, d, g * volume * powf(10, d.db / 20), &last[(int)id]));
    if (on) playRandom(v, d, g * volume, &last[(int)id]);
}

// banks load lazily (~30 decoded upfront would cost ~300 MB); preloadVoices() moves the hitch to match start
static void load(Bank &b) {
    if (b.loaded) return;
    for (int i = 0; i < (int)Voice::Count; i++) b.lines[i] = loadVariants(b.dir + "/" + VOICE_NAMES[i], 1);
    b.loaded = true;
}
static Bank &bankOf(int team, bool loaded = true) {
    int k = team < (int)teamBank.size() && teamBank[team] >= 0 ? teamBank[team] : team;
    Bank &b = banks[k % banks.size()];
    if (loaded) load(b);
    return b;
}

static void voice(int team, Voice id, const Vector3 *at) {
    if (banks.empty()) return;
    Bank &b = bankOf(team);
    const Def &d = id == Voice::SadSigh || id == Voice::Yawn ? SPEECH_SOFT : SPEECH;
    for (Variants &v : b.lines)  // a new line is dropped while the bank's voice is still speaking (no queue, no gap)
        for (Slot &x : v.slot) if (IsSoundPlaying(x.s)) return;
    for (int hop = 0; hop < 4 && !b.lines[(int)id].n; hop++) id = FALLBACK[(int)id];
    playRandom(b.lines[(int)id], d, 1, at);
}
void voice(int team, Voice id) { voice(team, id, nullptr); }
void voice(int team, Voice id, Vector3 at) { voice(team, id, &at); }

void preloadVoices(int teams) {  // one decoder per bank
    std::vector<Bank *> todo;
    for (int t = 0; t < teams && !banks.empty(); t++)
        if (Bank *b = &bankOf(t, false); !b->loaded && std::find(todo.begin(), todo.end(), b) == todo.end()) todo.push_back(b);
    std::vector<std::thread> th;
    for (size_t i = 1; i < todo.size(); i++) th.emplace_back([b = todo[i]] { Loading::pinCore(2), load(*b); });  // the caller is on core 1
    if (!todo.empty()) load(*todo[0]);
    for (std::thread &x : th) x.join();
}

int voiceBanks() { return (int)banks.size(); }
unsigned started() { return plays; }

const char *voiceBankName(int bank) { return bank >= 0 && bank < (int)banks.size() ? GetFileName(banks[bank].dir.c_str()) : ""; }

void setTeamVoice(int team, int bank) {
    if (team >= (int)teamBank.size()) teamBank.resize(team + 1, -1);
    teamBank[team] = bank;
}

void preloadMusic(const char *name) {
    for (auto &r : ready) if (r.first == name) return;
    if (Music m = loadMusic(name); IsMusicValid(m)) ready.emplace_back(name, m);
}

void music(bool on, const char *name) {
    if (name && track != name) loop(Sfx::Cheer, false);  // the crowd leaves with the jingle
    if (name && track != name && !openMusic(name) && track != "theme") openMusic("theme");
    musicOn = on;
    if (!musicLoaded) return;
    if (on && !IsMusicStreamPlaying(theme))
        fade = track == "victory" ? 1 : 0, SetMusicVolume(theme, fade * powf(10, trackDb(track) / 20)), PlayMusicStream(theme);
    else if (!on && track == "theme" && IsMusicStreamPlaying(theme)) stopping = true;
    else if (!on) StopMusicStream(theme);
    if (on) stopping = false;
}

}  // namespace Audio
