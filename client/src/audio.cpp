#include <cstring>
#include "audio.h"
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
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
    "fe_book_in", "fe_book_out", "fe_grenade", "fe_wormpot", "wormpot_spin", "wormpot_stop",
    "holy_boom", "holy_held",
    "bomb_whistle", "cow_fall", "power_rocket", "power_homing", "power_bow",
    "equip_air", "equip_bazooka", "equip_bubble", "equip_default", "equip_potion", "equip_scouser", "equip_shotgun", "equip_sniper", "equip_umbrella",
    "held_sheep", "held_sentry", "held_scouser", "held_old_woman", "lock_on",
    "ufo_appearing", "ufo_active", "ufo_beam", "ufo_engine", "ufo_takeoff", "bat_impact", "bubble_inflate", "bubble_wobble", "bubble_loop", "throw", "secret_launch",
};
static_assert(sizeof SFX_NAMES / sizeof *SFX_NAMES == (size_t)Sfx::Count, "one file per Sfx");
// W4M WormsX.fev, hand-kept from `tools/w4m-re/fev.py` (docs/w4m-map.md §12): the event of each file, its gain in dB
// (event + sound definition + category), loop, 3D linear rolloff min..max in m (20 units/m; 0 = 2D), max playbacks.
struct Def { const char *event; float db; bool loop; float min, max; int maxpb; float fade = 0; const int *w = nullptr; int mode = 1; };  // fade: FEV fade in/out, s; w: FEV wave weights, null = equal; mode: FEV sounddef play mode (see pick)
// every other multi-wave def has equal weights in the FEV (100 each, 20 on OldWomanMutter)
const int W_SCOUSER_HELD[] = {100, 300, 100};
const Def DEFS[] = {
    {"global/ExplosionRegular", -3, false, 0.5f, 50, 4, 0, nullptr, 2},
    {"weapons/ExplosionLarge", -12, false, 0.5f, 40, 1},  // its 2nd variant is ExplosionBoxed1 (W4M -2 dB, 2D)
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
    {"weapons/Teleport", -8, false, 0.5f, 25, 1, 0, nullptr, 2},
    {"weapons/BaseballBarSwing", 0, false, 0, 0, 1},
    {"weapons/FirePunch", -6, false, 0.5f, 25, 1},
    {"weapons/Prod", -4, false, 0, 0, 1},
    {"weapons/SniperRifleFire", -3, false, 0, 0, 1},
    {"weapons/BowRelease", 0, false, 0.5f, 60, 1},
    {"weapons/MissileLoop", 0, false, 5, 75, 1},  // loops, but its Time envelope cuts it at 5 s: one pass of the clip
    {"weapons/OldWomenLaunch", 0, false, 0.5f, 60, 1},
    {"weapons/ScouserLaunch", 0, false, 0.5f, 60, 1},
    {"weapons/SentryGunHeld", -8, true, 0.5f, 20, 1},
    {"weapons/SentryGun", -3, true, 0.5f, 100, 1},
    {"weapons/FuseLoop", -4, true, 0.5f, 25, 1},
    {"weapons/GasLoop", -16, false, 0.5f, 15, 1},
    {"weapons/AlienUfoBeamStart", 0, false, 0.5f, 100, 1},
    {"weapons/RainLoop", -6, true, 0, 0, 1},
    {"weapons/ParachuteLoop", -2, false, 0.5f, 25, 1},  // the Open layer, a oneshot
    {"weapons/MineArmLoop", 0, true, 5, 25, 1},
    {"weapons/CrateSpawn", -4, false, 0.5f, 60, 1},
    {"weapons/PickupWeapon", -11, false, 0.5f, 25, 1},  // PickupUtil -11, PickupHealthCrate -6
    {"weapons/WingFlap", -3, false, 0.5f, 25, 1, 0, nullptr, 2},
    {"weapons/OldWomenFootsteps", -6, false, 0.5f, 60, 1, 0, nullptr, 2},
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
    {"frontendsfx/WormPotLoop", 0, true, 0, 0, 1},
    {"frontendsfx/WormPotStop", 0, false, 0, 0, 1},
    {"weapons/HolyGrenadeExplosion", -1, false, 0, 0, 1},
    {"weapons/HolyGrenadeHeld", -10, true, 0.5f, 25, 1},
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
    {"weapons/SheepHeld", -9.2f, false, 0.5f, 25, 1},  // 3D log 10..500 units
    {"weapons/SentryGunHeld", -8, true, 0.5f, 20, 1, 0.35f},
    {"weapons/ScouserHeld", 0, false, 0.5f, 60, 1, 0, W_SCOUSER_HELD, 0},
    {"weapons/OldWomanHeld", -7, false, 0.5f, 60, 1, 0, nullptr, 0},
    {"weapons/LockOn", 0, false, 0, 0, 1},  // sample TargetAquired, 2D
    {"weapons/AlienUfoAppearing", 0, false, 0.5f, 100, 1},  // the UFO events: 3D linear 10..2000 units; TakeOff 2D
    {"weapons/AlienUfoActive", 0, false, 0.5f, 100, 1},
    {"weapons/AlienUfoBeamLoop", 0, true, 0.5f, 100, 1, 0.5f},
    {"weapons/AlienUfoEngineLoop", 0, true, 0.5f, 100, 1, 0.5f},
    {"weapons/AlienUFOTakeOff", 0, false, 0, 0, 1},
    {"weapons/BaseballBatImpact", 0, false, 0.5f, 25, 1},  // 3D linear 10..500 units
    {"weapons/BubbleMachineInflate", -2, false, 0.5f, 25, 1},  // sample BubbleMachinePlace; both 3D linear 10..500 units
    {"weapons/BubbleMachineWobble", -2, false, 0.5f, 25, 1},
    {"weapons/BubbleMachineLoop", -22, false, 0.5f, 20, 1, 0, nullptr, 2},  // 3D linear 10..400 units; one of Bubble1-6 per 500 ms spawn
    {"weapons/Throw", 0, false, 0.5f, 25, 1},  // 3D linear 10..500 units
    {"weapons/SecretWeapLaunch", 0, false, 0, 0, 1},  // 2D
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
Music theme;
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

// FMOD 3D linear rolloff (full volume inside min, silent past max) and pan from the listener's right
void place(Sound s, const Def &d, float gain, const Vector3 *at) {
    float pan = 0;
    if (at && d.max > 0) {
        Vector3 to = Vector3Subtract(*at, ear);
        float dist = Vector3Length(to);
        gain *= Clamp((d.max - dist) / (d.max - d.min), 0, 1);
        if (dist > 1e-3f) pan = Vector3DotProduct(to, earRight) / dist;
    }
    SetSoundVolume(s, gain), SetSoundPan(s, pan);
}

// FMOD sounddef play mode (fmod_event.dll selector 0x10038670): 0 and 3 sequential per event instance (from wave 1 / wave 0),
// 1 weighted random, 2 random without repeating the last wave, 4 per-instance shuffle, 6 global shuffle (7 global sequential: unused).
int pick(int n, const Def &d) {
    struct State { int last = 0, cur = 0; std::vector<int> perm; };  // last: previous wave + 1, 0 = none
    static std::map<const Def *, State> state;  // keyed by Def: SPEECH defs live outside DEFS
    if (n < 2) return 0;
    State &st = state[&d];
    int &last = st.last, &cur = st.cur;
    std::vector<int> &perm = st.perm;
    auto wt = [&](int i) { return d.w ? d.w[i] : 1; };  // d.w has one weight per variant
    if (d.mode == 0) return 1 % n;  // a fresh instance starts at index 0 and steps once
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
void playRandom(Variants &v, const Def &d, float volume, const Vector3 *at) {
    if (!v.n) return;
    int k = pick(v.n, d);
    int busy = 0;
    Slot *oldest = nullptr, *free = nullptr;
    for (Slot &x : v.slot)
        if (IsSoundPlaying(x.s)) busy++, oldest = !oldest || x.born < oldest->born ? &x : oldest;
        else if (x.variant == k && !free) free = &x;
    if (busy >= d.maxpb && oldest) StopSound(oldest->s), free = free ? free : oldest;
    if (!free) return;
    place(free->s, d, volume * powf(10, d.db / 20), at);
    free->born = ++plays;
    PlaySound(free->s);
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

bool openMusic(const char *name) {
    for (const char *root : {ASSET_ROOT, ROMFS_ROOT}) {
        std::string s = std::string(root) + "music/" + name + ".ogg";
        const char *p = s.c_str();
        if (!FileExists(p)) continue;
        Music m = LoadMusicStream(p);
        if (!IsMusicValid(m)) continue;
        if (musicLoaded) UnloadMusicStream(theme);
        theme = m;
        theme.looping = std::string(name) != "victory";  // jingle: once, then silence
        musicLoaded = true;
        track = name;
        return true;
    }
    return false;
}

}  // namespace

void init() {
    InitAudioDevice();
    for (int i = 0; i < (int)Sfx::Count; i++) {
        sfx[i] = loadVariants(std::string(ASSET_ROOT "sfx/") + SFX_NAMES[i], DEFS[i].maxpb);
        if (!sfx[i].n) sfx[i] = loadVariants(std::string(ROMFS_ROOT "sfx/") + SFX_NAMES[i], DEFS[i].maxpb);
    }
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
    musicLoaded = false;
    CloseAudioDevice();
}

void update() {
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
    playRandom(*v, DEFS[(int)id], volume, at);
}
void play(Sfx id, float volume) { play(id, volume, nullptr); }
void play(Sfx id, Vector3 at) { play(id, 1, &at); }

void equip(const char *w, Vector3 at) {
    struct E { const char *name; Sfx id; };  // WEAPTWK EquipSfx by weapon; Sheep, Starburst, Fire Punch, Prod, Armour, Binoculars, Teleport, Change Worm, Skip Go have none
    static const E T[] = {
        {"Bazooka", Sfx::EquipBazooka}, {"Homing Missile", Sfx::EquipBazooka}, {"Sentry Gun", Sfx::EquipBazooka},
        {"Airstrike", Sfx::EquipAir}, {"Super Airstrike", Sfx::EquipAir}, {"Fatkins Strike", Sfx::EquipAir}, {"Concrete Donkey", Sfx::EquipAir}, {"Alien Abduction", Sfx::EquipAir},
        {"Flood", Sfx::EquipUmbrella}, {"Inflatable Scouser", Sfx::EquipScouser}, {"Shotgun", Sfx::EquipShotgun}, {"Sniper Rifle", Sfx::EquipSniper},
        {"Bubble Trouble", Sfx::EquipBubble}, {"Icarus Potion", Sfx::EquipPotion},
        {"Grenade", Sfx::EquipDefault}, {"Cluster Grenade", Sfx::EquipDefault}, {"Banana Bomb", Sfx::EquipDefault}, {"Holy Hand Grenade", Sfx::EquipDefault},
        {"Poison Arrow", Sfx::EquipDefault}, {"Dynamite", Sfx::EquipDefault}, {"Gas Canister", Sfx::EquipDefault}, {"Landmine", Sfx::EquipDefault},
        {"Old Woman", Sfx::EquipDefault}, {"Baseball Bat", Sfx::EquipDefault}, {"Tail Nail", Sfx::EquipDefault}, {"Ninja Rope", Sfx::EquipDefault},
        {"Jetpack", Sfx::EquipDefault}, {"Parachute", Sfx::EquipDefault}, {"Girder", Sfx::EquipDefault}, {"Surrender", Sfx::EquipDefault},
    };
    for (const E &e : T) if (!strcmp(w, e.name)) return play(e.id, at);
}

void hold(Sfx id, bool on, const Vector3 *at) {
    static bool was[(int)Sfx::Count];
    const Def &d = DEFS[(int)id];
    if (on && !was[(int)id]) play(id, 1, at);
    for (Slot &k : sfx[(int)id].slot) {
        if (!on) StopSound(k.s);
        else if (IsSoundPlaying(k.s)) place(k.s, d, powf(10, d.db / 20), at);
    }
    was[(int)id] = on;
}

void loop(Sfx id, bool on, const Vector3 *at) {
    Variants &v = sfx[(int)id];
    const Def &d = DEFS[(int)id];
    static float level[(int)Sfx::Count];
    static Vector3 last[(int)Sfx::Count];  // where a fading-out loop was last placed
    float &g = level[(int)id];
    if (at) last[(int)id] = *at;
    g = d.fade > 0 ? Clamp(g + (on ? 1 : -1) * GetFrameTime() / d.fade, 0, 1) : on;
    if (g <= 0) { for (Slot &k : v.slot) StopSound(k.s); return; }
    for (Slot &k : v.slot) if (IsSoundPlaying(k.s)) return place(k.s, d, g * powf(10, d.db / 20), &last[(int)id]);
    if (on) playRandom(v, d, g, &last[(int)id]);
}

// banks load lazily (~30 decoded upfront would cost ~300 MB); preloadVoices() moves the hitch to match start
static Bank &bankOf(int team) {
    int k = team < (int)teamBank.size() && teamBank[team] >= 0 ? teamBank[team] : team;
    Bank &b = banks[k % banks.size()];
    if (!b.loaded) {
        for (int i = 0; i < (int)Voice::Count; i++) b.lines[i] = loadVariants(b.dir + "/" + VOICE_NAMES[i], 1);
        b.loaded = true;
    }
    return b;
}

static void voice(int team, Voice id, const Vector3 *at) {
    if (banks.empty()) return;
    Bank &b = bankOf(team);
    const Def &d = id == Voice::SadSigh || id == Voice::Yawn ? SPEECH_SOFT : SPEECH;
    for (int hop = 0; hop < 4 && !b.lines[(int)id].n; hop++) id = FALLBACK[(int)id];
    playRandom(b.lines[(int)id], d, 1, at);
}
void voice(int team, Voice id) { voice(team, id, nullptr); }
void voice(int team, Voice id, Vector3 at) { voice(team, id, &at); }

void preloadVoices(int teams) {
    for (int t = 0; t < teams && !banks.empty(); t++) bankOf(t);
}

int voiceBanks() { return (int)banks.size(); }

const char *voiceBankName(int bank) { return bank >= 0 && bank < (int)banks.size() ? GetFileName(banks[bank].dir.c_str()) : ""; }

void setTeamVoice(int team, int bank) {
    if (team >= (int)teamBank.size()) teamBank.resize(team + 1, -1);
    teamBank[team] = bank;
}

void music(bool on, const char *name) {
    if (name && track != name) loop(Sfx::Cheer, false);  // the crowd leaves with the jingle
    if (name && track != name && !openMusic(name) && track != "theme") openMusic("theme");
    musicOn = on;
    if (!musicLoaded) return;
    if (on && !IsMusicStreamPlaying(theme))
        fade = track == "victory" ? 1 : 0, SetMusicVolume(theme, fade * powf(10, trackDb(track) / 20)), PlayMusicStream(theme);
    else if (!on) StopMusicStream(theme);
}

}  // namespace Audio
