#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "acting.h"
#include "ai.h"
#include "audio.h"
#include "controls.h"
#include "frontbg.h"
#include "fx.h"
#include "lanhost.h"
#include "lit.h"
#include "loading.h"
#include "mission.h"
#include "models.h"
#include "net.h"
#include "replay.h"
#include "sim.h"
#include "ui.h"
#include "xray.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#define ROMFS_DIR "romfs:/"
#else
#define DATA_DIR "./"
#define ROMFS_DIR "./romfs/"
#endif

extern "C" void glFinish(void);  // perf overlay only; rlgl does not wrap it
extern "C" void glClear(unsigned int mask);
#ifndef __SWITCH__  // bench only: draw calls and framebuffer binds, through the desktop rlgl's glad pointers
extern "C" void (*glad_glDrawElements)(unsigned, int, unsigned, const void *), (*glad_glDrawArrays)(unsigned, int, int), (*glad_glBindFramebuffer)(unsigned, unsigned);
static long glDraws, glBinds;
static void (*realDE)(unsigned, int, unsigned, const void *);
static void (*realDA)(unsigned, int, int);
static void (*realBF)(unsigned, unsigned);
static void countGl() {
    realDE = glad_glDrawElements, realDA = glad_glDrawArrays, realBF = glad_glBindFramebuffer;
    glad_glDrawElements = [](unsigned m, int c, unsigned t, const void *i) { glDraws++, realDE(m, c, t, i); };
    glad_glDrawArrays = [](unsigned m, int f, int c) { glDraws++, realDA(m, f, c); };
    glad_glBindFramebuffer = [](unsigned t, unsigned f) { glBinds++, realBF(t, f); };
}
#else
static long glDraws, glBinds;
static void countGl() {}
#endif

static const Color TEAM_COLORS[] = {{220, 50, 50, 255}, {50, 110, 230, 255}, {60, 190, 70, 255}, {240, 200, 40, 255}};

static bool pressedAny(int pad, std::initializer_list<int> buttons, std::initializer_list<int> keys) { return Ui::pressed(pad, buttons, keys); }

// raylib's log, timestamped, to log.txt (no console on Switch; desktop echoes stdout). Counts GPU / file loads for the hitch log.
enum { L_TEX, L_SHADER, L_MODEL, L_SOUND, L_IMAGE, L_FBO, L_COUNT };
static unsigned loads[L_COUNT];
// A Switch SD write blocks 15-25 ms (newlib flushes every 1 KB): after boot (logAsync) lines queue in memory and a thread
// writes them each second; boot lines and warnings are written at once, so a crash leaves them on the card
struct LogQueue { std::mutex mu, fileMu; std::string text; bool async = false; FILE *f = nullptr; std::thread writer; std::atomic<bool> stop{false}; };
static LogQueue &logq = *new LogQueue;  // never destroyed: the writer is joined in an atexit handler
static void logDrain() {
    std::string out;
    std::lock_guard<std::mutex> fl(logq.fileMu);
    { std::lock_guard<std::mutex> l(logq.mu); out.swap(logq.text); }
    if (logq.f && !out.empty()) fwrite(out.data(), 1, out.size(), logq.f), fflush(logq.f);
}
static void logAsync() {  // joined at exit: libnx has no pthread_detach (std::thread::detach aborts)
    if (!logq.f || logq.async) return;
    logq.async = true;
    logq.writer = std::thread([] {
        for (int i = 1; !logq.stop; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (i % 20 == 0) logDrain();
        }
    });
    atexit([] { logq.stop = true, logq.writer.join(), logDrain(); });
}
static void logLine(int level, const char *fmt, va_list ap) {
    static const char *const KIND[L_COUNT] = {"TEXTURE:", "SHADER:", "MODEL:", "WAVE:", "IMAGE:", "FBO:"};
    if (strstr(fmt, "loaded successfully") || strstr(fmt, "created successfully"))
        for (int k = 0; k < L_COUNT; k++) if (!strncmp(fmt, KIND[k], strlen(KIND[k]))) loads[k]++;
    if (strstr(fmt, "Mesh uploaded") || strstr(fmt, "Unloaded vertex array")) return;  // one per remeshed chunk
#ifndef __SWITCH__
    va_list cp;
    va_copy(cp, ap);
    printf("%s: ", level >= LOG_ERROR ? "ERROR" : level == LOG_WARNING ? "WARNING" : "INFO"), vprintf(fmt, cp), putchar('\n'), va_end(cp);
#endif
    static const auto t0 = std::chrono::steady_clock::now();  // not GetTime(): GLFW logs an error through here once terminated
    static bool opened = (logq.f = fopen(DATA_DIR "log.txt", "w")) != nullptr;
    if (!opened) return;
    char b[1024];
    int k = snprintf(b, sizeof b, "%.3f ", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    vsnprintf(b + k, sizeof b - k, fmt, ap);
    bool now;
    {
        std::lock_guard<std::mutex> l(logq.mu);
        logq.text.append(b) += '\n';
        now = !logq.async || level >= LOG_WARNING;
    }
    if (now) logDrain();
}

// Scripted input for shot mode: select weapon, aim up, charge, release.
static Input scriptInput(int frame, int weapon, bool fire) {
    Input in;
    if (frame < 2 * weapon && frame % 2) in.buttons = Input::NEXT_WEAPON;
    if (frame > 10 && frame < (fire ? 30 : 14)) in.aim = 127;
    if (fire && frame > 40 && frame < 100) in.buttons = Input::FIRE;
    return in;
}

static void animEvent(const Game &g, const GameEvent &e);

// W4M GameOverLogicEntity +0x2c == 2 (0x4fd5c5: Win, Mission / Challenge / Tutorial Success): victory music, crowd and fireworks; not on a draw or a failure
static bool won(const Game &g) { return g.cfg.mission ? g.run.result > 0 : g.winner >= 0; }
static bool gunBlast(const GameEvent &e) { return e.weapon >= 0 && WEAPONS[e.weapon].kind == Kind::Shotgun; }  // EmitterSoundFX weapons/ShotgunFire of WXP_ShotgunBlastHit, no bang
static bool donkeyBlast(const GameEvent &e) { return e.weapon >= 0 && WEAPONS[e.weapon].kind == Kind::Donkey && WEAPONS[e.weapon].clusters == 0; }  // its DetonationFx and sound, not a bang
static const char *objectModel(const Object &o) {
    if (o.type != Object::Crate) return o.type == Object::Mine ? "mine" : o.type == Object::Barrel ? "barrel" : o.type == Object::Sentry ? "sentry" : "target";
    if (o.mystery >= 0) return Models::has("crate_mystery") ? "crate_mystery" : "crate_weapon";
    return o.weapon < 0 ? "crate_health" : utility(WEAPONS[o.weapon].kind) && Models::has("crate_utility") ? "crate_utility" : "crate_weapon";
}
static bool holyNext = false;  // the blast after the Hallelujah is the holy grenade's own
static void onEvent(const Game &g, const GameEvent &e) {
    animEvent(g, e);
    Acting::event(g, e);
    Ui::hudEvent(g, e);
    using Audio::Sfx;
    using Audio::Voice;
    int team = e.worm >= 0 ? g.worms[e.worm].team : 0;
    if (e.kind == GameEvent::GameOver && won(g)) Fx::fireworks(g.landCenter(), Terrain::NX * Terrain::VOX / 2, g.landTop());  // radius as the orbit camera
    Fx::event(e, g.terrain.side);
    switch (e.kind) {
    case GameEvent::Boom: Audio::play(gunBlast(e) ? Sfx::Shotgun : donkeyBlast(e) ? Sfx::DonkeyImpact : e.weapon >= 0 && WEAPONS[e.weapon].stick > 0 ? Sfx::ExplosionBoxed : Sfx::Explosion, e.pos); if (g.phase != Phase::Aim) Controls::impact(e.pos); break;
    case GameEvent::BigBoom: if (donkeyBlast(e)) { Audio::play(Sfx::DonkeyImpact, e.pos); if (g.phase != Phase::Aim) Controls::impact(e.pos); break; }
        Audio::play(holyNext ? Sfx::HolyBoom : Sfx::BigExplosion, e.pos), holyNext = false; if (g.phase != Phase::Aim) Controls::impact(e.pos); break;
    case GameEvent::Hallelujah: Audio::play(Sfx::Holy, e.pos), holyNext = true; break;
    case GameEvent::Fire: {
        const WeaponDef &d = WEAPONS[e.weapon];
        Kind k = d.kind;
        static const Sfx FIRE_SFX[] = {Sfx::Fire, Sfx::Sheep, Sfx::Airstrike, Sfx::Donkey, Sfx::Shotgun, Sfx::Rope, Sfx::Fire, Sfx::Teleport,
                                       Sfx::SuperSheepFire, Sfx::OldWomanFire, Sfx::Bounce, Sfx::Fire, Sfx::Tick, Sfx::ScouserFire, Sfx::SentryFire,
                                       Sfx::Abduction, Sfx::Flood, Sfx::Parachute, Sfx::TurnStart, Sfx::TurnStart, Sfx::TurnStart};  // by Kind
        Sfx s = (size_t)k < sizeof FIRE_SFX / sizeof *FIRE_SFX ? FIRE_SFX[(int)k] : Sfx::TurnStart;  // later Kinds: none of their own yet
        // a few Kinds cover several named weapons with distinct W4M sounds; pick by name like heldModel() does
        if (k == Kind::Shell) s = d.name == "Poison Arrow" ? Sfx::Bow : d.name == "Dynamite" ? Sfx::Dynamite : d.name == "Gas Canister" ? Sfx::Gas : s;
        else if (k == Kind::Melee) s = d.name == "Baseball Bat" ? Sfx::BatSwing : d.name == "Prod" ? Sfx::Prod : Sfx::FirePunch;
        else if (k == Kind::Shotgun && d.name == "Sniper Rifle") s = Sfx::Sniper;
        if (k == Kind::Shell && customWeapon(e.weapon)) s = d.fuse > 0 ? Sfx::Throw : Sfx::SecretLaunch;  // Factory LaunchSfx by launch type
        else if (k == Kind::Sentry && e.worm >= 0) s = Sfx::SentryPlace;  // placing vs. the turret's own shots (worm -1)
        if (k != Kind::Airstrike && k != Kind::Abduction && k != Kind::Jetpack) Audio::play(s, e.pos);  // chopper / UFO: drawBomber, drawUfo; jetpack: its loop (jetAudio)
        bool tool = utility(k) || k == Kind::SkipGo;
        if (e.worm >= 0 && !tool) Audio::voice(team, WEAPONS[e.weapon].name == "Fire Punch" ? Voice::Punch : Voice::Fire, e.pos);  // worm -1: sentry gun; MeleeWeaponLogicEntity 0x568270: kMeleeFirepunch says Punch
        break;
    }
    case GameEvent::Bounce: Audio::play(Sfx::Bounce, e.pos); break;
    case GameEvent::Arm: Audio::play(Sfx::BowImpact, e.pos); break;
    case GameEvent::Launch:  // WEAPTWK LaunchSfx; the bomb's ArielFx
        if (WEAPONS[e.weapon].kind == Kind::Donkey) {  // ArielFx WXP_CrateSpawnLARGE at its locator Donkey_L (0, -93, 0) units x Scale 1.2 [scale on the locator assumed]; EmitterSoundFX weapons/Cratespawn
            Audio::play(Sfx::CrateLand, e.pos), Fx::donkeyAriel(Vector3Add(e.pos, {0, -93 * 1.2f / 20, 0}));
            break;
        }
        Audio::play(WEAPONS[e.weapon].fuse > 0 ? Sfx::CowFall : Sfx::BombWhistle, e.pos);
        if (WEAPONS[e.weapon].fuse <= 0) Fx::ariel(e.pos);
        break;
    case GameEvent::Splash: Audio::play(Sfx::Splash, e.pos); if (e.worm < 0) Controls::impact(e.pos); break;  // a shot sinking
    case GameEvent::Death: Audio::voice(team, Voice::Death, e.pos); break;
    case GameEvent::Hurt: Audio::voice(team, Voice::Hurt, e.pos); break;
    case GameEvent::Jump: Audio::play(Sfx::Jump, e.pos); Audio::voice(team, Voice::Jump, e.pos); break;
    case GameEvent::TurnStart: Audio::play(Sfx::TurnStart); Audio::voice(team, Voice::Idle, e.pos); break;
    case GameEvent::CrateDrop: Audio::play(Sfx::CrateLand, e.pos); break;
    case GameEvent::CrateLand:
        Audio::play(e.weapon == -1 ? Sfx::CrateImpactHealth : e.weapon >= 0 && utility(WEAPONS[e.weapon].kind) ? Sfx::CrateImpactUtil : Sfx::CrateImpactWeapon, e.pos);  // mystery (-2): Weapon (0x5ca143)
        break;
    case GameEvent::Collect: Audio::play(Sfx::Pickup, e.pos); break;
    case GameEvent::Mystery: Audio::play(Sfx::Buffalo, e.pos); break;
    case GameEvent::Debris: Audio::play(Sfx::Debris, e.pos); break;
    case GameEvent::JetStart: Fx::jetStart({e.pos.x, e.pos.y - Game::R + 7 / 20.f, e.pos.z}); break;  // worm Position + 7 units (0x58ce7f)
    case GameEvent::MineArm: break;  // MineArmLoop: looped below while a mine ticks
    case GameEvent::Zap: Audio::play(Sfx::BatImpact, e.pos); break;  // WXP_AbdTelep_Central's EmitterSoundFX
    case GameEvent::Poof: case GameEvent::AbdDamage: case GameEvent::Abducted: break;  // Fx::event; no sound in their PARTTWK emitters
    case GameEvent::BubbleNew: Audio::play(Sfx::BubbleInflate, e.pos); break;
    case GameEvent::BubbleHit: Audio::play(Sfx::BubbleWobble, e.pos); break;
    case GameEvent::BubblePop: break;  // Fx::event; WXP_BubbleMachineExpire has no EmitterSoundFX
    case GameEvent::GameOver:
        if (won(g)) Audio::music(true, "victory");  // the crowd (cheer/cheer) loops below while the match is over
        if (g.winner >= 0) Audio::voice(g.winner, Voice::Victory);
        break;
    }
}

// Mesh held at the worm's WeaponLocator (WEAPTWK WeaponGraphicsResourceID), *clip its Holding clip, *aim its Aim clip or
// nullptr (absent from the worm bundle, or the Holding clip itself, whose later 0x594d40 call wins on the shared handle)
static const char *heldModel(const WeaponDef &d, const char **clip, const char **aim = nullptr) {
    const std::string &n = d.name;
    const char *a = nullptr, *m = nullptr;
    switch (d.kind) {
    case Kind::Shell:
        if (n == "Poison Arrow") *clip = "HoldBow", a = "AimBow", m = "hold_bow";
        else if (n == "Dynamite") *clip = "HoldDynamite", m = "hold_dynamite";
        else if (d.fuse > 0) *clip = "HoldThrown", a = "AimGrenade", m = n == "Cluster Grenade" ? "hold_cluster" : n == "Banana Bomb" ? "hold_banana"
             : n == "Holy Hand Grenade" ? "hold_holy" : n == "Gas Canister" ? "hold_gas" : "hold_grenade";  // WAE_Thrown hardcodes HoldThrown
        else *clip = "HoldBazooka", a = "AimBazooka", m = "hold_bazooka";
        break;
    case Kind::Shotgun: *clip = n == "Sniper Rifle" ? "HoldSniper" : "HoldShotgun", a = n == "Sniper Rifle" ? "AimSniper" : "AimShotgun", m = n == "Sniper Rifle" ? "hold_sniper" : "hold_shotgun"; break;
    case Kind::Homing: *clip = "HoldHomingMissile", a = "AimHomingMissile", m = "hold_homing"; break;
    case Kind::Sheep: *clip = "HoldSheep", m = "hold_sheep"; break;
    case Kind::SuperSheep:  // Super Sheep's WeaponGraphicsResourceID is Sheep too
        if (n == "Starburst") *clip = "HoldStarburst", a = "AimBazooka", m = "hold_starburst";
        else *clip = "HoldSheep", m = "hold_sheep";
        break;
    case Kind::OldWoman: *clip = "HoldOldWoman", m = "hold_oldwoman"; break;  // Aim Struggle: not in the worm bundle
    case Kind::Scouser: *clip = "HoldScouser", m = "hold_scouser"; break;
    case Kind::Melee: *clip = n == "Baseball Bat" ? "HoldBat" : n == "Prod" ? "HoldProd" : n == "Tail Nail" ? "HoldNMN" : "HoldFirepunch";
        m = n == "Baseball Bat" ? "hold_bat" : n == "Tail Nail" ? "hold_hammer" : "";  // "": hands only; AimBat is missing
        break;
    case Kind::Mine: *clip = "HoldLandmine", m = "hold_landmine"; break;
    case Kind::Sentry: *clip = "HoldSentrygun", m = "hold_sentry"; break;
    case Kind::Surrender: *clip = "HoldSurrender", m = "hold_flag"; break;
    case Kind::SkipGo: *clip = "HoldSkipGo", m = "hold_skipgo"; break;
    case Kind::Airstrike: case Kind::Donkey: case Kind::Abduction:  // Fatkins (id 23) has no WAE in table 0x95f6d0: nothing in hand
        if (n == "Fatkins Strike") return nullptr;
        *clip = "HoldAirstrike", m = "hold_radio";
        break;
    case Kind::Flood: *clip = "HoldRainDance", m = "hold_flood"; break;
    case Kind::Rope: *clip = "HoldNinjarope", a = "AimBazooka", m = "hold_rope"; break;
    case Kind::Bubble: *clip = "HoldBT", m = "bubble_machine"; break;
    case Kind::Icarus: *clip = "HoldRedbull", m = "hold_redbull"; break;
    default: return nullptr;
    }
    if (aim) *aim = a;
    return m;
}

// WEAPTWK WXAnimDraw (WAE state 1, Accessory.Init); WAE_Thrown 0x595ed0 hardcodes DrawThrown for the five grenades
static const char *drawClip(const WeaponDef &d) {
    const std::string &n = d.name;
    switch (d.kind) {
    case Kind::Shell: return n == "Poison Arrow" ? "DrawBow" : n == "Dynamite" ? "DrawDynamite" : d.fuse > 0 ? "DrawThrown" : "DrawBazooka";
    case Kind::Shotgun: return n == "Sniper Rifle" ? "DrawSniper" : "DrawShotgun";
    case Kind::Homing: return "DrawHomingMissile";
    case Kind::Sheep: case Kind::SuperSheep: return n == "Starburst" ? "DrawStarburst" : "DrawSheep";
    case Kind::OldWoman: return "DrawOldWoman";
    case Kind::Scouser: return "DrawScouser";
    case Kind::Melee: return n == "Baseball Bat" ? "DrawBat" : n == "Prod" ? "DrawProd" : n == "Tail Nail" ? "DrawNMN" : "DrawFirepunch";
    case Kind::Mine: return "DrawLandmine";
    case Kind::Sentry: return "DrawSentrygun";
    case Kind::Surrender: return "DrawSurrender";
    case Kind::SkipGo: return "DrawSkipGo";
    case Kind::Airstrike: case Kind::Donkey: case Kind::Abduction: return "DrawAirstrike";
    case Kind::Flood: return "DrawRainDance";
    case Kind::Rope: return "DrawNinjarope";
    case Kind::Bubble: return "DrawBT";
    case Kind::Icarus: return "DrawRedbull";
    default: return nullptr;
    }
}

// Clip of the powered windup (PoweringUpStart / PlayWindupAnim on FIRE press): WAE_Thrown WindupThrown, WAE_Mechanical the
// WEAPTWK Fire clip (WindupBow); the other WAE classes ignore both messages
static const char *windupClip(const WeaponDef &d) {
    return d.kind != Kind::Shell || d.name == "Dynamite" ? nullptr : d.name == "Poison Arrow" ? "WindupBow" : d.fuse > 0 ? "WindupThrown" : nullptr;
}

// W4M's use clip for a weapon (Weapon.PlayFireAnim); *keep: the held item stays in hand instead of leaving it (throws, animals,
// placed items, the dropped flag 0x5908d7). wind: s of windup; WAE_Thrown throws FireThrown past 0.6 s, else LobThrown (0x59663e).
static const char *fireClip(const WeaponDef &d, bool *keep, float wind = 0) {
    const std::string &n = d.name;
    *keep = true;
    switch (d.kind) {
    case Kind::Shell:
        if (n == "Poison Arrow") return "WindupBow";  // WAE_Mechanical loads WEAPTWK Fire in its fire slot too (0x58d932); EndFire FireBow is never read
        *keep = d.fuse <= 0;
        return n == "Dynamite" ? "FireDynamite" : d.fuse > 0 ? (wind > 0.6f ? "FireThrown" : "LobThrown") : "FireBazooka";
    case Kind::Shotgun: return n == "Sniper Rifle" ? "FireSniper" : "FireShotgun";
    case Kind::Homing: return "FireHomingMissile";
    case Kind::Melee: return n == "Baseball Bat" ? "Fire2Bat" : n == "Prod" ? "FireProd" : n == "Tail Nail" ? "FireNMN" : "Fire2Firepunch";
    case Kind::Flood: return "FireRainDance";
    case Kind::Bubble: return "FireBT";
    case Kind::Icarus: return "FireRedbull";
    default: break;
    }
    *keep = false;
    switch (d.kind) {
    case Kind::Surrender: return "Tantrum";
    case Kind::Sheep: case Kind::SuperSheep: return n == "Starburst" ? nullptr : "FireSheep";
    case Kind::OldWoman: return "FireOldWoman";
    case Kind::Scouser: return "FireScouser";
    case Kind::Mine: return "FireLandmine";
    case Kind::Sentry: return "FireSentrygun";
    default: return nullptr;
    }
}

// WEAPTWK WXAnimTaunt by weapon (TauntRainDance is not in the worm bundle; Starburst has none in the bundle)
static const char *tauntClip(const WeaponDef &d) {
    static const std::pair<const char *, const char *> T[] = {
        {"Bazooka", "TauntBazooka"}, {"Grenade", "TauntThrown"}, {"Cluster Grenade", "TauntThrown"}, {"Banana Bomb", "TauntThrown"},
        {"Holy Hand Grenade", "TauntThrown"}, {"Gas Canister", "TauntThrown"}, {"Landmine", "TauntThrown"}, {"Dynamite", "TauntDynamite"},
        {"Airstrike", "TauntAirstrike"}, {"Super Airstrike", "TauntAirstrike"}, {"Concrete Donkey", "TauntAirstrike"},
        {"Alien Abduction", "TauntAirstrike"}, {"Homing Missile", "TauntHomingMissile"},
        {"Shotgun", "TauntShotgun"}, {"Sniper Rifle", "TauntSniper"}, {"Baseball Bat", "TauntBat"}, {"Fire Punch", "TauntFirepunch"},
        {"Prod", "TauntProd"}, {"Tail Nail", "TauntNMN"}, {"Poison Arrow", "TauntBow"}, {"Old Woman", "TauntOldWoman"},
        {"Inflatable Scouser", "TauntScouser"}, {"Sheep", "TauntSheep"}, {"Super Sheep", "TauntSheep"}, {"Starburst", "TauntStarburst"},
        {"Sentry Gun", "TauntSentrygun"}, {"Ninja Rope", "TauntNinjarope"}, {"Surrender", "TauntSurrender"},
        {"Bubble Trouble", "TauntBT"}, {"Icarus Potion", "TauntRedbull"}};
    for (const auto &e : T) if (d.name == e.first) return e.second;
    return nullptr;
}

// Render-only gait state: the sim walks worms by moving pos (vel stays 0), so the cycle follows position deltas.
// act: one-shot clip started by a sim event (weapon use, flinch, drowning), held: item kept at the WeaponLocator meanwhile.
struct WormAnim {
    Vector3 pos{}; float yaw = 0, walk = 0, still = 1, air = 0, fallV = 0, land = 9; bool init = false, moving = false, nailed = false;
    const char *flip = nullptr;  // kWE 4 Backflip / 5 Fwdflip clip, until it lands
    bool vaulting = false; float vaultT = 9, chute = -1, lr = 0, drowned = -1, spin = -1;  // spin: kWE 22 tumble angle, -1 off  // drowned: s since it sank  // vaultT: s since kWE 9; chute: s open, lr: WXWorm.ParachuteLR
    struct Act { const char *clip = nullptr; float t = 0; const char *held = nullptr, *aim = nullptr, *from = nullptr; float fromT = 0; } act;  // aim: Aim* clip layered on the arms; from: windup frame faded out
    // WAE state 1 / 4 (current worm): drawT s since Accessory.Init, drawn: weapon in hand, wind: windup clock (s since FIRE press)
    float drawT = 0, wind = 0; int drawW = -1; bool drawn = false, charging = false;
    float lockT = -1, jlr = 0;  // lockT: s since HUD.Target.Selected (homing), jlr: WXWorm.JetpackLR
};
static std::vector<WormAnim> wormAnims;

static void animEvent(const Game &g, const GameEvent &e) {
    if (e.worm < 0 || e.worm >= (int)g.worms.size()) return;
    wormAnims.resize(g.worms.size());
    if (e.kind == GameEvent::Jump && e.weapon >= 0) wormAnims[e.worm].flip = e.weapon == 4 ? "Backflip" : e.weapon == 5 ? "Fwdflip" : nullptr;
    WormAnim::Act &a = wormAnims[e.worm].act;
    const char *c = nullptr, *h = nullptr;
    bool keep;
    if (e.kind == GameEvent::Fire && WEAPONS[e.weapon].kind == Kind::Teleport) a = {"TelepadAppear", 1.5f};  // its first half is invisible
    else if (e.kind == GameEvent::Fire) {
        const WeaponDef &d = WEAPONS[e.weapon];
        const char *wc = windupClip(d);
        float wind = wormAnims[e.worm].wind;
        const char *ac = nullptr;
        h = heldModel(d, &c, &ac), a = {fireClip(d, &keep, wind), 0, keep ? h : nullptr, ac};
        if (wc && !strcmp(wc, "WindupThrown")) a.from = wc, a.fromT = fminf(wind, Models::clipLength("worm", wc));  // 0x595a3b: Windup at 1 - v
    }
    else if (e.kind == GameEvent::Hurt && !a.clip) a = {g.worms[e.worm].nailed ? "NailedHitFront" : "Hit_Front"};  // W4M 0x5a9100: Nailed* variant
    else if (e.kind == GameEvent::Splash) a = {"FallDrown"};
    if (a.clip && !strcmp(a.clip, "FireNMN")) a.t = 0.7f;  // the sim hits at once: start at the hammer's downswing
}

// Once per frame, for every worm: walk phase advances with distance walked, footsteps on the cycle beat, landing thud.
// W4M blast flight, mode 0 (0x5a0617): the body pitches to the velocity, asin(vy / |v|)
static float blastPitch(const Worm &w) {
    float s = Vector3Length(w.vel), h = sqrtf(w.vel.x * w.vel.x + w.vel.z * w.vel.z);
    return h > 4 && s > 1e-3f ? asinf(Clamp(w.vel.y / s, -1, 1)) : 0;
}

static void animateWorms(const Game &g, float dt, const Camera3D &cam) {
    wormAnims.resize(g.worms.size());
    const float L = fmaxf(Models::clipLength("worm", "Walk"), 0.1f);
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &w = g.worms[i];
        WormAnim &a = wormAnims[i];
        Vector3 d = Vector3Subtract(w.pos, a.pos);
        float dist = sqrtf(d.x * d.x + d.z * d.z), turn = fabsf(wrapPi(w.yaw - a.yaw));
        if (!a.init || dist > 2) { WormAnim::Act act = a.act; a = WormAnim{}, a.act = act, a.init = true, dist = turn = 0; }  // spawn, teleport, replay rewind
        // WXWorm.JetpackLR (0x561e40, per 20 ms): +-1 while the jetpack turns the worm's yaw up / down, else 0, at
        // v += clamp((to - v) / 2, +-0.03) (0x47a1a0 k 1)
        float yawStep = wrapPi(w.yaw - a.yaw), jto = (int)i == g.current && g.jetting && fabsf(yawStep) > 1e-5f ? (yawStep > 0 ? 1.0f : -1.0f) : 0;
        a.jlr += Clamp((jto - a.jlr) * (1 - powf(0.5f, dt / 0.02f)), -0.03f * dt / 0.02f, 0.03f * dt / 0.02f);
        a.pos = w.pos, a.yaw = w.yaw;
        a.drowned = w.alive ? -1 : fmaxf(a.drowned, 0) + dt;
        bool vaulting = (int)i == g.current && g.vault.t > 0;  // kWE 9: Walking -> Vaulting starts the Vault clip
        a.vaultT = vaulting && !a.vaulting ? 0 : a.vaultT + dt, a.vaulting = vaulting;
        // WXWorm.ParachuteLR (ParachuteLogicEntity 0x578a40, every 20 ms): lr = (3 lr +- 0.75) / 4 by the stick's turn, (3 lr) / 4 without a stick
        bool chute = (int)i == g.current && g.chute && !w.grounded;
        a.chute = chute ? fmaxf(a.chute, 0) + dt : -1;
        if (int s = Game::chuteSteer(g.steerIn, w.yaw); s || Vector3LengthSqr(g.steerIn) == 0) a.lr += (0.75f * s - a.lr) * (1 - powf(0.75f, dt / 0.02f));
        if (w.nailed && !a.nailed) a.act = {"Nailed"};  // W4M DirtBallLogicEntity 0x5ce320 queues it on the victim
        a.nailed = w.nailed;
        if (WormAnim::Act &c = a.act; c.clip) {
            bool air = !w.grounded && fabsf(w.vel.y) > 1, leap = !strcmp(c.clip, "Fire2Firepunch"), drown = !strcmp(c.clip, "FallDrown");
            c.t += dt;
            bool done = c.t >= Models::clipLength("worm", c.clip) || (leap ? w.grounded && c.t > 0.3f : !drown && (strncmp(c.clip, "Recover", 7) ? air || a.moving : a.air > 0.2f));  // state 5: a recovery plays to its end (0x5a2c90) unless it really falls (kWE 7)
            if (c.held && strncmp(c.clip, "Taunt", 5) && (int)i == g.current && g.phase == Phase::Aim && c.t > 0.4f) done = true;  // shotgun: aiming the next shot
            if (done) c = {};
        }
        if (!w.grounded && (fabsf(w.vel.y) > 1 || a.air > 0)) {  // ignore slope-contact flicker; the apex (|vy| < 1) stays airborne
            a.air += dt, a.fallV = fminf(a.fallV, w.vel.y), a.walk = 0, a.moving = false;
            // kWE 22 (0x5a3a60): falling past 0.3 units/ms (15 m/s), Skid and a 2 pi rad/s tumble (0x5a0593)
            // the angle (+0x158) carries on: 0 from a jump or fall (0x5a01a0 zeroes it), the mode-0 flight pitch asin(vy/|v|) from a blast (0x5a0617)
            if (w.vel.y < -15 && a.spin < 0 && w.alive && !((int)i == g.current && (g.roped || g.jetting || g.chute)))
                a.spin = blastPitch(w) ? fmodf(blastPitch(w) + 2 * PI, 2 * PI) : 0;
            if (a.spin >= 0) a.spin = fmodf(a.spin + 2 * PI * dt, 2 * PI);
            continue;
        }
        // kWE 15 hard landing (vn <= -0.3 units/ms) after it: recover clip by the tumble angle (0x5a3dc1)
        if (a.spin >= 0 && a.fallV <= -15 && w.alive) {
            float s = a.spin;
            a.act = {s < 0.75f * PI || s > 1.75f * PI ? "RecoverFront1" : s < 1.25f * PI ? "RecoverBurried1" : (i + g.clock) & 1 ? "RecoverBack1" : "RecoverBack2"};
        }
        a.spin = -1;
        if (a.air > 0.2f && a.fallV < -4 && w.alive) a.land = 0, Audio::play(Audio::Sfx::Land, w.pos);
        a.air = a.fallV = 0, a.land += dt, a.flip = nullptr;
        float step = dist + turn * 0.6f;  // turning in place shuffles at half the walk pace
        a.still = step > 1e-4f ? 0 : a.still + dt;
        a.moving = w.alive && a.still < 0.1f;  // bridges render frames that ran no sim tick
        float before = a.walk, B = 0.4f * L;   // one body surge per cycle; B: where it lands
        if (a.moving) a.walk += step / 3;      // in-place clip, 1x at the 3 u/s walk speed
        else if (a.walk > 0) a.walk = a.walk < L / 2 ? fmaxf(a.walk - dt, 0) : a.walk + dt >= L ? 0 : a.walk + dt;  // ease to the upright pose
        bool beat = floorf((a.walk - B) / L) > floorf((before - B) / L);
        a.walk = fmodf(a.walk, L);
        bool self = (int)i == g.current && (g.phase == Phase::Aim || g.retreating()) && Vector3Length(w.vel) < 0.3f;  // not sliding
        if (a.moving && beat && self) Audio::play(Audio::Sfx::Step, w.pos);
    }
    // ActivateAccessory -> Accessory.Init (Draw from 0): turn start, weapon change, back on its feet after walking or a jump
    // (UpdateWalking 0x5b1bed; kWE 1 / 2 hid it); windup clock = charge x Tweaks.MaxPowerUpTime 1.5 s
    for (size_t i = 0; i < g.worms.size(); i++) {
        WormAnim &a = wormAnims[i];
        const char *pc;
        bool drawn = (int)i == g.current && g.phase == Phase::Aim && !g.roped && !g.jetting && g.worms[i].alive && !a.moving && a.air == 0 &&
                     heldModel(WEAPONS[g.weapon], &pc);
        if (drawn && (!a.drawn || a.drawW != g.weapon)) a.drawT = 0, a.drawW = g.weapon, a.wind = 0;
        else a.drawT += dt;
        a.drawn = drawn, a.charging = drawn && g.power > 0;
        if (a.charging) a.wind = g.power * 1.5f;
        a.lockT = drawn && g.locked ? (a.lockT < 0 ? 0 : a.lockT + dt) : -1;  // WAE_Standard 0x590ae0: TargetSelected once on the mesh
    }
    static std::vector<uint8_t> busy;
    busy.assign(g.worms.size(), 0);
    for (size_t i = 0; i < g.worms.size(); i++) busy[i] = wormAnims[i].moving || wormAnims[i].air > 0 || wormAnims[i].act.clip;
    Acting::update(g, dt, busy, cam);
}

// W4M jetpack/parachute meshes sit on the worm's root in raw units; WORM_K: worm.glb's export scale (tools/w4m-models).
static const float WORM_K = 0.0496f;
static Matrix rootMatrix(const Worm &w, Vector3 raw) {
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(raw.x, raw.y, raw.z), MatrixScale(WORM_K, WORM_K, WORM_K)),
                          MatrixMultiply(MatrixRotateY(w.yaw), MatrixTranslate(w.pos.x, w.pos.y - Game::R, w.pos.z)));
}
static const float CHUTE_K = 0.045f;  // crate_chute.glb is in raw W4M units: 20 units = the 0.9 m crate
static const Vector3 JETPACK_AT = {0, 12, 3};  // pack origin on the worm's back, its sticks in the JetpackFly hands

// W4M JetpackUtilityLogicEntity 0x562530, each 20 ms in flight: the weapons/Jetpack loop's volume eases up x 1.14 while thrusting (from 0.3 at
// least) to 1, down x 0.96 to 0.05; under 0.3 after a full burn, weapons/JetpackEnd (3 s apart); landing or running dry stops the loop (0x5624f0)
static void jetAudio(const Game &g) {
    static float vol = 0.05f, lastFuel = 0;
    static int lastClock = 0, nextEnd = 0;
    static bool armed = false;  // +0x100: set at full volume
    if (g.clock < lastClock) nextEnd = 0, armed = false;  // a new match
    int n = g.clock - lastClock;
    lastClock = g.clock;
    if (!g.jetting || g.current >= (int)g.worms.size()) return void((vol = 0.05f, lastFuel = INFINITY, Audio::loop(Audio::Sfx::Jetpack, false), Fx::jetStop()));
    const Vector3 p = g.worms[g.current].pos, at = {p.x, p.y - Game::R, p.z};
    if (n > 0) {
        bool burn = g.fuel < lastFuel;  // WXWorm.JetpackThrust > 0.5
        float k = n * Game::DT / 0.02f;
        lastFuel = g.fuel;
        if (!burn) Fx::jetStop();
        vol = burn ? fmaxf(vol, 0.3f) * powf(1.14f, k) : vol * powf(0.96f, k);
        if (vol > 1) vol = 1, armed = true;
        else vol = fmaxf(vol, 0.05f);
        int now = (int)(g.clock * Game::DT * 1000);
        if (vol < 0.3f && armed && now > nextEnd) Audio::play(Audio::Sfx::JetpackEnd, at), armed = false, nextEnd = now + 3000;
    }
    Audio::loop(Audio::Sfx::Jetpack, true, &at, vol);
}

// Once per frame: jetpack exhaust (full jets while fuel burns) and the Fire Punch's flaming fist.
static void toolFx(const Game &g, float dt) {
    static float thrust = 0, lastFuel = 0, acc = 0;
    thrust = g.jetting && g.fuel < lastFuel ? 0.1f : fmaxf(thrust - dt, 0);  // bridges frames without a sim tick
    lastFuel = g.fuel;
    if (g.jetting && g.current < (int)g.worms.size()) {
        const Worm &w = g.worms[g.current];
        Matrix m = MatrixMultiply(MatrixTranslate(JETPACK_AT.x, JETPACK_AT.y, JETPACK_AT.z), rootMatrix(w, {}));
        for (acc += dt * (thrust > 0 ? 50 : 12); acc >= 1; acc--)
            for (float x : {5.0f, -5.0f}) {  // JetLeft/JetRight nozzles
                Vector3 n = Vector3Transform({x, -10, -6}, m), v = {w.vel.x, w.vel.y - (thrust > 0 ? 6 : 2), w.vel.z};
                Fx::flame(n, v, thrust > 0 ? 0.3f : 0.12f, thrust > 0 ? 0.7f : 0.25f, 0.15f, true);
                if (thrust > 0 && acc < 1.5f) Fx::puff(Vector3Add(n, {0, -0.5f, 0}), {0, -1.5f, 0}, 0.9f, 0.3f, 1.1f, {200, 200, 200, 120});
            }
    }
    for (size_t i = 0; i < g.worms.size() && i < wormAnims.size(); i++) {
        const WormAnim::Act &c = wormAnims[i].act;
        Matrix h;
        if (!c.clip || strcmp(c.clip, "Fire2Firepunch") || !Models::joint("worm", "WeaponLocator", c.clip, c.t, false, &h)) continue;
        const Worm &w = g.worms[i];
        Vector3 f = Vector3Transform({h.m12, h.m13, h.m14}, MatrixMultiply(MatrixRotateY(w.yaw), MatrixTranslate(w.pos.x, w.pos.y - Game::R, w.pos.z)));
        for (int k = 0; k < 2; k++) Fx::flame(f, {0, 1.5f, 0}, 0.35f, 0.5f, 0.15f, false);
    }
}

static const float VM_RIGHT = 0.4f, VM_UP = -0.32f, VM_FWD = 0.7f, VM_SCALE = 0.4f, VM_CONV = 3;  // first-person view-model offset from the eye

// W4M worm: team-tinted, animation picked from the sim state (aim clips map pitch to their timeline).
static bool drawWorm(const Game &g, const Worm &w, float clock, const Camera3D *fp = nullptr) {  // fp: held weapon only, as a view-model
    int i = int(&w - g.worms.data()), st = -1;
    for (const Game::Abductee &b : g.abductees) if (b.worm == i) st = b.st;
    if (st == 2 && !fp) return true;  // aboard the UFO: hidden (AlienAbductionLogicEntity 0x547874)
    WormAnim a = i < (int)wormAnims.size() ? wormAnims[i] : WormAnim{};
    float speed = sqrtf(w.vel.x * w.vel.x + w.vel.z * w.vel.z), t = clock;  // shared timeline: idle worms reuse one skinned pose
    const char *clip = "Base", *held = nullptr, *aim = nullptr;
    bool loop = true, actLoop = true;
    bool tool = i == g.current && (g.roped || g.jetting || (g.chute && a.air > 0));
    Models::Layers lay;  // W4M pose layers: emote face, LookAt head, GestureAt arms
    float actT = 0;
    const char *acted = fp ? nullptr : Acting::clip(g, i, clock, &actT, &actLoop, &lay);
    Models::Layers dying;  // dying: its Death scene gestures only, no face, head, arm or eye layer
    for (int k = 0; k < 2; k++) dying.act[k] = lay.act[k], dying.actT[k] = lay.actT[k], dying.actW[k] = lay.actW[k];
    const Models::Layers *ly = fp || !w.alive ? nullptr : w.hp <= 0 ? &dying : &lay;  // drowned: FallDrown alone
    const bool aimNow = i == g.current && g.phase == Phase::Aim && !g.roped && !g.jetting;
    const float aimAt = w.pitch / (PI / 2) + 1;  // WAE Aim clip time, s: WeaponAngle / (pi / 2) + 1 (0x58f63d)
    float aimT = aimAt;
    Models::Layers wl;  // WAE layers over the body clip, on top of ly's
    auto aimPose = [&] {  // the weapon in hand: Draw, Windup or Hold, with the Aim clip added
        const char *ac = nullptr, *wc = windupClip(WEAPONS[g.weapon]), *dc = drawClip(WEAPONS[g.weapon]);
        if (!(held = heldModel(WEAPONS[g.weapon], &clip, &ac))) return false;
        float dl = Models::clipLength("worm", dc);
        t = fmaxf(a.drawT - dl, 0), loop = true, aim = ac;  // state 1: Hold loops on its own clock, run from the Draw's end (0x58f8a3)
        if (fp || !ly) return true;
        if (a.charging && wc) clip = wc, t = a.wind, loop = false;  // state 4: Windup and Aim at 1, Hold 0
        else if (dl > 0 && a.drawT < dl) {  // state 1: Draw at 1, Aim at drawT / length, Hold 0 (0x58f731)
            wl = *ly, wl.aimW = a.drawT / dl, ly = &wl;
            clip = dc, t = a.drawT, loop = false;
        }
        return true;
    };
    float drown = Models::clipLength("worm", "FallDrown");  // dead and still drawn: drowned, afloat until Settle pops it
    const Projectile *rk = nullptr;  // the Starburst the shooter rides
    if (!fp && i == g.current && w.alive) for (const Projectile &q : g.shots) if (!q.child && WEAPONS[q.weapon].name == "Starburst") rk = &q;
    // W4M kWPS_DrownFloat 0x5a06b0: FallDrown scrubbed by clamp(vy x 10, -1, 1), vy in units/ms (0.02 per m/s); afloat = mid-clip
    if (!w.alive) clip = "FallDrown", t = drown * (1 + Clamp(w.vel.y * 0.2f, -1, 1)) / 2, loop = false;  // the sim's DrownFloat velocity
    else if (st == 1) clip = "BeamUpLoop";  // rising in the beam, weapon hidden (HeldAccessory.Hide)
    else if (rk) {  // riding it: FireStarburst while the fuse burns 3.5 s (WAE state 5), then FlyStarburst (Starburst.Launched, state 7)
        float el = WEAPONS[rk->weapon].fuse - rk->fuse;
        clip = el < 3.5f ? "FireStarburst" : "FlyStarburst", t = el < 3.5f ? el : el - 3.5f;
    } else if (a.act.clip && !fp) {
        clip = a.act.clip, t = a.act.t, loop = false, held = a.act.held, aim = a.act.aim;
        if (a.act.from && ly) {  // WAE_Thrown state 5: Windup at 1 - v, v += clamp((1 - v) / 2, 0.2) per 20 ms (0x595a3b, 0x47a1a0)
            float v = 0;
            for (int n = (int)(t / 0.02f); n > 0 && v < 1; n--) v += fminf((1 - v) / 2, 0.2f);
            wl = *ly, wl.act[1] = a.act.from, wl.actT[1] = a.act.fromT, wl.actW[1] = 1 - v, ly = &wl;
        }
    }
    else if (fp && aimNow && aimPose()) {  // view-model: the aim pose whatever the body does (turning in place plays Walk)
    } else if (tool) {
        clip = g.roped ? "SwingNinjarope" : g.jetting ? "JetpackFly" : "ParachuteLR", held = g.roped ? "hold_rope" : nullptr;
        if (!g.roped && !g.jetting) t = 1 - a.lr, loop = false;  // WAE_Parachute 0x58f2b4: ParachuteLR at 1 - lr, on worm and canopy
        if (g.jetting) aim = "JetpackRotLR", aimT = a.jlr + 1;  // WAE_Jetpack 0x58cd74: over JetpackFly, on worm and pack
    } else if (a.spin >= 0 && a.air > 0) clip = "Skid", t = a.air;  // tumbling down
    else if (bool air = a.air > 0.15f || w.vel.y > 2; air && speed > 4) clip = "Blastflight2";  // knocked flying (short drops keep the pose)
    else if (air) {
        clip = a.flip ? a.flip : w.vel.y > 0 ? "Jump" : "Fall";
        if (w.vel.y > 0 || a.flip) t = a.air, loop = false;
    } else if (a.vaultT < Models::clipLength("worm", "Vault")) clip = "Vault", t = a.vaultT, loop = false;  // kWE 9: Walking -> Vaulting
    else if (a.moving || a.walk > 0) clip = "Walk", t = a.walk;
    else if (a.land < Models::clipLength("worm", "Land") && w.hp > 0 && !(i == g.current && g.phase == Phase::Aim)) clip = "Land", t = a.land, loop = false;
    else if (acted) clip = acted, t = actT, loop = actLoop;  // W4M acting: gestures, emotes
    else if (w.hp <= 0) clip = "Wave";  // bye-bye until Settle blows it up
    else if (g.phase == Phase::GameOver && w.team == g.winner) clip = "Victorious_Grin";
    else if (aimNow && aimPose()) {
    } else if (w.hp < 25) clip = "Wounded";
    if (i == g.current && g.icarus == 2 && w.alive) clip = "FlyRedBull", t = a.air, loop = true;  // RedBullWings wield 0x595437: the worm's clip too
    if (Vector3 bm; ly && Models::blend("worm", clip, t, loop, ly, &bm)) Acting::headMode(i, bm.z);  // read by the next Acting::update
    Color tint = Acting::tint(i, ColorLerp(WHITE, TEAM_COLORS[w.team], 0.5f));
    Vector3 p = {w.pos.x, w.pos.y - Game::R, w.pos.z};
    // W4M turns the model about its own root (0x5a26d3), the raw mesh origin: worm.glb's main_bone rest translation
    // rider (0x588fd0, once launched): pitch = the rocket's elevation; during the fuse the worm stands, not attached (0x5891e0)
    float roll = !w.alive ? 0 : rk ? (strcmp(clip, "FlyStarburst") ? 0 : asinf(Clamp(rk->aim.y, -1, 1))) : a.spin >= 0 && a.air > 0 ? a.spin : !strcmp(clip, "Blastflight2") ? blastPitch(w) : 0;
    const Vector3 ROOT = {0, 0.299f, 0.352f};
    if (roll) p = Vector3Add(p, Vector3Subtract(Vector3Transform(ROOT, MatrixRotateY(w.yaw)), Vector3Transform(ROOT, MatrixMultiply(MatrixRotateX(-roll), MatrixRotateY(w.yaw)))));
    Matrix root = MatrixMultiply(MatrixMultiply(MatrixRotateX(-roll), MatrixRotateY(w.yaw)), MatrixTranslate(p.x, p.y, p.z));
    if (fp ? !Models::has("worm") : !Models::draw("worm", p, w.yaw, roll, tint, clip, t, loop, aim, aimT, ly)) return false;
    Matrix m;
    if (held && Models::joint("worm", "WeaponLocator", clip, t, loop, &m, aim, aimT, ly)) {
        m = MatrixMultiply(m, root);
        if (fp) {  // the aim clip's hand orientation, moved to the bottom right of the view
            Vector3 f = Vector3Normalize(Vector3Subtract(fp->target, fp->position)), r = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), u = Vector3CrossProduct(r, f);
            float q = tanf(30 * DEG2RAD) / tanf(fp->fovy * 0.5f * DEG2RAD);  // pushed back as the FOV narrows: same screen spot and size
            Vector3 at = Vector3Add(fp->position, Vector3Add(Vector3Scale(r, VM_RIGHT), Vector3Add(Vector3Scale(u, VM_UP), Vector3Scale(f, VM_FWD * q))));
            Matrix in = QuaternionToMatrix(QuaternionFromVector3ToVector3(f, Vector3Normalize(Vector3Subtract(Vector3Add(fp->position, Vector3Scale(f, VM_CONV * q)), at))));
            m = MatrixMultiply(MatrixMultiply(MatrixScale(VM_SCALE, VM_SCALE, VM_SCALE), m), in);  // toed in toward the centre
            m.m12 = at.x, m.m13 = at.y, m.m14 = at.z;
        }
        // the mesh plays the worm's clip of the same name at its time and weight (0x594d40); WAE_Mechanical plays DrawFlood,
        // Windup and FireBow itself (0x58d89f, 0x58dc1a, 0x58dcd1), WAE_Standard TargetSelected (0x590ae0); one-shots hold their end (assumed)
        const char *mc = clip;
        float mt = t;
        bool once = !loop, bow = !strcmp(held, "hold_bow");
        if (!strcmp(held, "hold_flood")) mc = "DrawFlood", mt = a.drawT, once = true;
        else if (bow && !strcmp(clip, "WindupBow")) mc = a.act.clip ? "FireBow" : "Windup", mt = a.act.clip ? a.act.t : a.wind;
        else if (!strcmp(held, "hold_homing") && a.lockT >= 0 && !a.act.clip) mc = "AimLockHomingMissile", mt = a.lockT, once = true;
        float ml = Models::clipLength(held, mc);
        Models::draw(held, !strcmp(held, "hold_starburst") ? MatrixMultiply(MatrixRotateX(-PI / 2), m) : m, WHITE, ml > 0 ? mc : "Rest", once ? fminf(mt, ml - 1e-3f) : mt);
    }
    bool opening = a.chute >= 0 && a.chute < Models::clipLength("hold_chute", "FireParachute");  // PackAccessory.Wield 0x58f3d4
    if (tool && !fp && !g.roped) Models::draw(g.jetting ? "hold_jetpack" : "hold_chute", rootMatrix(w, g.jetting ? JETPACK_AT : Vector3{}), WHITE,
                                              g.jetting ? "JetpackRotLR" : opening ? "FireParachute" : clip, g.jetting ? fminf(aimT, 2 - 1e-3f) : opening ? a.chute : t);
    if (rk && !fp && Models::joint("worm", "Pack_Locator", clip, t, loop, &m, aim, aimT, ly)) Models::draw("hold_starburst", MatrixMultiply(m, root), WHITE, "FireStarburst", rk->fuse < WEAPONS[rk->weapon].fuse - 3.5f ? 3.5f : WEAPONS[rk->weapon].fuse - rk->fuse);  // WAE_Starburst 0x5917d0
    if (i == g.current && g.icarus == 2 && !fp && Models::joint("worm", "Pack_Locator", clip, t, loop, &m, aim, aimT, ly))  // WAE 0x5953d1
        Models::draw("wings", MatrixMultiply(m, root), WHITE, "FlyRedBull", t);
    int hat = !fp && w.team < (int)g.cfg.teamSetup.size() ? g.cfg.teamSetup[w.team].hat : 0;  // cosmetic only: index resolved against this client's own sorted hat list
    const char *hatN = tool && !fp && g.jetting ? "jetpack" : hat ? Models::hatName(hat - 1) : nullptr;  // W4M's jetpack helmet
    if (hatN && Models::joint("worm", "HatLocator", clip, t, loop, &m, aim, aimT, ly))
        Models::draw(hatN, MatrixMultiply(m, root));
    return true;
}

// W4M Crate.Chute: Open on the drop then Fall while descending, Close on landing (render-only state, by object index).
static void crateChute(const Object &o, size_t i, float dt) {
    static std::vector<std::pair<float, float>> st;  // {time since the drop, time since landing or -1}
    if (st.size() <= i) st.resize(i + 1, {0, -1});
    auto &[fall, shut] = st[i];
    if (o.falling) fall += dt, shut = 0;
    else if (shut >= 0) shut += dt;
    if (o.falling || (shut >= 0 && shut < Models::clipLength("crate_chute", "Close"))) {
        float open = Models::clipLength("crate_chute", "Open");
        bool opening = o.falling && fall < open;
        Models::draw("crate_chute", MatrixMultiply(MatrixScale(CHUTE_K, CHUTE_K, CHUTE_K), MatrixTranslate(o.pos.x, o.pos.y, o.pos.z)), WHITE,
                     !o.falling ? "Close" : opening ? "Open" : "Fall", !o.falling ? shut : opening ? fall : fall - open);
    } else if (!o.falling && shut >= 0) fall = 0, shut = -1;
}

// W4M mine ExpiryFx: a dud mine with none within 1 m last frame has just fizzled (render-only, no sim event).
static void dudFx(const std::vector<Object> &objs) {
    static std::vector<Vector3> was;
    std::vector<Vector3> now;
    for (const Object &o : objs) {
        if (o.type != Object::Mine || !o.dud) continue;
        bool old = false;
        for (Vector3 p : was) old |= Vector3Distance(p, o.pos) < 1;
        if (!old) Fx::dud(o.pos);
        now.push_back(o.pos);
    }
    was.swap(now);
}

// Dead worms leave a random W4M gravestone, dropped onto whatever terrain is left below (render only).
// W4M GirderKitGraphicEntity 0x5586c0: the Girder mesh x (3, 3.5, 3) units, opaque, tinted red where it can't go or after 45
static void drawUtilities(const Game &g) {
    if (g.girderOn && g.phase == Phase::Aim) {
        bool bad = g.girderFits(g.girder) || g.girders >= Game::GIRDER_MAX;
        const Color red = {255, 0, 0, 255};
        static Vector3 from{}, to{}, at{};  // 0x558fa0: a step slides over the 20 ms update at 6 units / 20 ms
        static double t0 = 0;
        if (!Vector3Equals(to, g.girder)) from = Vector3Distance(at, g.girder) < 1 ? at : g.girder, to = g.girder, t0 = GetTime();
        at = Vector3Lerp(from, to, fminf((float)(GetTime() - t0) / 0.02f, 1));
        Matrix m = MatrixMultiply(MatrixScale(0.15f, 0.175f, 0.15f), MatrixTranslate(at.x, at.y, at.z));
        if (!Models::draw("girder", m, bad ? red : WHITE)) {  // no glb: GirderSmall.xom's boxes
            rlPushMatrix();
            rlTranslatef(at.x, at.y, at.z);
            DrawCube({0, 0.5f, 0}, 4, 1, 4, bad ? red : LIGHTGRAY);
            for (float sz : {-1.5f, 1.5f}) DrawCube({0, -0.5f, sz}, 4, 1, 1, bad ? red : LIGHTGRAY);
            rlPopMatrix();
        }
    }
}

// W4M BubbleTrouble.Bubble (Bundl09 shaders Front/Rear): its texture by its own UVs, unlit, SrcAlpha / InvSrcAlpha, no z write, no culling
static const char *BUBBLE_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
uniform mat4 mvp;
varying vec2 uv;
void main() { uv = vertexTexCoord; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
static const char *BUBBLE_FS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
varying vec2 uv;
void main() { gl_FragColor = texture2D(texture0, uv) * colDiffuse; }
)";
// BubbleTroubleGraphicEntity: the machine at the base, WXP_Bubbles_Small at its "bubble" node (17.66 units up); the bubble
// plays WXM_Create (1.5 s), then WXM_Bobbing looped, WXM_HitBounce once per hit (0x54e920, 0x54e410, 0x54e480).
static void drawBubbles(const Game &g, float dt) {
    static Shader sh = Lit::shader(BUBBLE_VS, BUBBLE_FS, false);
    for (const Game::Bubble &b : g.bubbles) {
        Models::draw("bubble_machine", MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixRotateY(b.yaw)), MatrixTranslate(b.pos.x, b.pos.y, b.pos.z)));
        if (GetRandomValue(0, 999) < dt * 1000 / 0.32f) Fx::soap(Vector3Add(b.pos, {0, 0.93f, 0}));
    }
    static float spawn = 0;  // BubbleMachineLoop (WXP_Bubbles_Small's EmitterSoundFX, 0x5bd960): its sound definition respawns every 500 ms
    if (g.bubbles.empty()) spawn = 0;
    else if ((spawn += dt) >= 0.5f) spawn -= 0.5f, Audio::play(Audio::Sfx::BubbleLoop, Vector3Add(g.bubbles[0].pos, {0, 0.93f, 0}));
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask(), rlDisableBackfaceCulling();
    Models::shade(sh);
    for (const Game::Bubble &b : g.bubbles) {
        float t = b.age * Game::DT, ht = (b.age - b.hit) * Game::DT, hit = Models::clipLength("bubble", "WXM_HitBounce");
        const char *clip = b.hit >= 0 && ht < hit ? "WXM_HitBounce" : t < 1.5f ? "WXM_Create" : "WXM_Bobbing";
        float ct = clip[4] == 'H' ? ht : clip[4] == 'C' ? t : b.hit >= 0 ? ht - hit : t - 1.5f;
        Models::draw("bubble", Vector3Add(b.pos, {0, Game::BUBBLE_UP, 0}), 0, 0, WHITE, clip, ct, clip[4] == 'B');
    }
    Models::shade({});
    EndBlendMode();
    rlEnableDepthMask(), rlEnableBackfaceCulling();
}

static void drawGrave(const Game &g, const Worm &w) {
    if (w.drowned) return;
    Vector3 p = {w.pos.x, w.pos.y - Game::R, w.pos.z};
    for (int k = 0; k < 100 && p.y > g.water && !g.terrain.solid(p); k++) p.y -= 0.1f;
    uint32_t h = (g.cfg.seed + uint32_t(&w - g.worms.data()) * 40503u) * 2654435761u;  // random per worm, same every view
    Models::draw(TextFormat("grave%u", (h >> 16) % 4), p, w.yaw);
}

// Bovine Blitz, W4M SuperBomberGraphicEntity (the SuperAirstrike chopper, banked into its turns) and
// ParachutePayloadGraphicEntity (Cow.Payload "Hang" with Crate.Chute "Fall" at its Parachute node, both in raw units).
static bool drawBovine(const Projectile &s, float clock) {
    const float K = 0.05f;  // 20 W4M units per metre
    float yaw = atan2f(s.vel.x, s.vel.z);
    Matrix cow = MatrixMultiply(MatrixMultiply(MatrixScale(K, K, K), MatrixRotateY(yaw)), MatrixTranslate(s.pos.x, s.pos.y, s.pos.z)), j;
    if (!Models::draw("cow", cow, WHITE, "Hang", clock)) return false;
    if (Models::joint("cow", "Parachute", "Hang", clock, true, &j)) Models::draw("crate_chute", MatrixMultiply(j, cow), WHITE, "Fall", clock);
    return true;
}

// W4M BomberGraphicEntity / SuperBomberGraphicEntity: the chopper's clip runs on the sim plane's clock, then plays its
// exit (bombrun_end*) on after the plane has left the sim. Wing trails at trail1/2, weapons/Bomber looped at the chopper.
static struct { const char *model = nullptr, *clip = nullptr; Matrix at; float t = 0, bank = 0, last = 0, puffs = 0; int begin = 0, end = 0; bool live = false, fat = false; } bomber;
static const char *const BOMB_START[] = {"bombrun_start", "bombrun_start2", "bombrun_start3"}, *const BOMB_END[] = {"bombrun_end", "bombrun_end2", "bombrun_end3", "bombrun_end4", "bombrun_end5"};  // 0x91f38c, 0x91f39c

static void updateBomber(const Game &g, float dt) {
    auto &f = bomber;
    const Projectile *p = nullptr;
    for (const Projectile &s : g.shots)
        if (!s.child && (WEAPONS[s.weapon].kind == Kind::Airstrike || (WEAPONS[s.weapon].name == "Fatkins Strike" && s.stage > 0))) p = &s;
    if (p) {
        const WeaponDef &d = WEAPONS[p->weapon];
        float yaw = atan2f(p->vel.x, p->vel.z);
        if (!f.live && d.kind != Kind::Airstrike) f.fat = true, f.begin = (f.begin + 1) % 3, f.end = 4;  // Fatkins: always bombrun_end5 (0x54da95)
        else if (!f.live && d.fuse <= 0) f.fat = false, f.begin = (f.begin + 1) % 3, f.end = (f.end + 1) % 5;  // Bomber.Begin/End.AnimIndex, +1 per strike
        if (!f.live) f.last = yaw, f.bank = 0;
        if (d.kind == Kind::Airstrike && d.fuse > 0) {  // Bovine Blitz: OpenDoorsSource from the start of the steered run (0x58b5ea)
            float rate = wrapPi(yaw - f.last) / fmaxf(dt, 1e-3f);
            if (dt > 0) f.last = yaw, f.bank = Lerp(f.bank, Clamp(rate * 0.8f, -0.6f, 0.6f), 0.1f);
            bool lead = !p->prey && p->stage > 0;  // bombrun_start while the sim holds it (Game::STRIKE_LEAD)
            f.model = "superbomber", f.clip = lead ? "bombrun_start" : "OpenDoorsSource";
            f.t = fminf(lead ? (Game::STRIKE_LEAD - p->stage) * Game::DT : d.fuse - p->fuse, Models::clipLength(f.model, f.clip) - 1e-3f);
            f.at = MatrixMultiply(MatrixMultiply(MatrixRotateZ(-f.bank), MatrixRotateY(yaw)), MatrixTranslate(p->pos.x, p->pos.y, p->pos.z));
        } else {  // bombrun_start while the sim holds it (Game::STRIKE_LEAD), then the end clip from the first DropBomb (0x54db75)
            bool lead = d.kind != Kind::Airstrike || !p->prey;
            float e = lead ? (Game::STRIKE_LEAD - p->stage) * Game::DT : (p->prey * Game::strikeTicks(d) - 1 - p->stage) * Game::DT;
            Vector3 o = lead ? p->pos : Vector3Subtract(p->pos, Vector3Scale(p->vel, e + Game::DT));
            f.model = "bomber", f.clip = lead ? BOMB_START[f.begin] : BOMB_END[f.end], f.t = lead ? fminf(e, Models::clipLength(f.model, f.clip) - 1e-3f) : e;
            f.at = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, 0.68f, -0.455f), MatrixRotateY(yaw)), MatrixTranslate(o.x, o.y, o.z));  // W4M entity origin in the importer's centred model
        }
        f.live = true;
    } else if (f.live) {
        f.live = false;
        if (!strcmp(f.model, "superbomber")) f.clip = "bombrun_end6", f.t = 0;  // 0x58b250
        else if (f.fat) f.clip = BOMB_END[f.end], f.t = 0;  // Fatkins dropped: the end clip queued after the start one
    } else if (f.model) f.t += dt;
    if (f.model && f.t >= Models::clipLength(f.model, f.clip)) f.model = nullptr;
}

// perspShape lens (Bundl09 XSceneCamera: FocalLength 25.0217 mm, Aperture 1.26 x 0.94488 in), as XCamera 0x6e1f46 frames it
static const float SCENE_FOVY = 2 * atanf(0.94488f * 25.4f * 0.5f / 25.0217f) * RAD2DEG;

// W4M Camera.FollowSceneCam: the bomber's "persp" node (a Maya camera, looking down -z) until Bomber.AnimsComplete;
// a payload with a CameraId (Fatkins) takes over at the drop (0x54dfa4).
static bool bomberCam(Camera3D *c) {
    auto &f = bomber;
    Matrix j;
    bool sb = f.model && !strcmp(f.model, "superbomber");  // SuperBomber: followed from 0x58ace6 through the flight (OpenDoorsSource), stopped at 0x58b0e2
    if (!f.model || (sb ? !f.live :strcmp(f.model, "bomber") || (f.fat && !f.live)) || !Models::joint(f.model, "persp", f.clip, f.t, false, &j)) return false;
    Matrix m = MatrixMultiply(j, f.at);
    Vector3 p = Vector3Transform({0, 0, 0}, m);
    c->position = p, c->target = Vector3Add(p, Vector3Normalize(Vector3Subtract(Vector3Transform({0, 0, -1}, m), p)));
    c->up = Vector3Normalize(Vector3Subtract(Vector3Transform({0, 1, 0}, m), p)), c->fovy = SCENE_FOVY;
    return true;
}

static void drawBomber(float dt) {
    auto &f = bomber;
    if (!f.model || !Models::draw(f.model, f.at, WHITE, f.clip, f.t)) {
        f.puffs = 0;
        Audio::loop(Audio::Sfx::Airstrike, false);
        return;
    }
    Vector3 tr[2] = {};
    for (int k = 0; k < 2; k++) {
        Matrix j;
        if (!Models::joint(f.model, k ? "trail2" : "trail1", f.clip, f.t, false, &j)) j = MatrixIdentity();
        tr[k] = Vector3Transform({0, 0, 0}, MatrixMultiply(j, f.at));
    }
    Vector3 c = Vector3Lerp(tr[0], tr[1], 0.5f);
    Audio::loop(Audio::Sfx::Airstrike, dt > 0, &c);  // two instances at the rotors in W4M, FEV max playbacks 1
    for (f.puffs += dt * 50; f.puffs >= 1; f.puffs--) Fx::wingTrail(tr[0]), Fx::wingTrail(tr[1]);  // EmitterNumSpawn 1 per update (0x5bf800, which returns 20 ms)
}

// W4M AlienAbductionGraphicEntity (0x545d20) on the sim UFO's clock: AbductStart (it flies in through its warp gate; nozzle particles 4.975 s, the beam and
// its sounds 7.791 s), AbductLoop while it lifts, AbductCloseBeam > AbductViolate (AlienUfoActive) > AbductOpenDoors, AbductLoop2 while it spits, AbductEnd.
// Raw units, 20 per metre, origin on the beam axis. e: s into the sequence (beam cues), u: s into the stage.
static struct { bool live = false, scene = false; Matrix at; Vector3 ground{}; const char *clip = ""; float t = 0, e = -1, u = 0; int stage = -1; } ufo;
static const float UFO_BEAM = 7.791f, UFO_CLOSE = 2.29f, UFO_VIOLATE = 4.666f, UFO_LOOP = 2.332f;

static void updateUfo(const Game &g, float dt) {
    const Projectile *p = g.ufo();
    auto &f = ufo;
    if (!p) {
        f.live = false, f.e = -1, f.stage = -1;
        Audio::loop(Audio::Sfx::UfoEngine, false), Audio::loop(Audio::Sfx::UfoBeamLoop, false);
        return;
    }
    float was = f.e, wasU = f.stage == p->stage ? f.u : -1;
    f.u = f.stage == p->stage ? f.u + dt : 0;  // the looped stages have no fixed length
    f.live = true, f.stage = p->stage, f.ground = p->aim;
    f.at = MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixTranslate(p->pos.x, p->pos.y, p->pos.z));
    switch (p->stage) {
    case Game::ABD_ARRIVING: f.clip = "AbductStart", f.u = f.t = f.e = Game::ABD_ARRIVE - p->fuse; break;
    case Game::ABD_FAILING: f.clip = "AbductFail", f.u = f.t = f.e = Game::ABD_FAIL - p->fuse; break;
    case Game::ABD_LIFTING: f.clip = "AbductLoop", f.u = p->fuse, f.t = fmodf(f.u, UFO_LOOP), f.e = Game::ABD_ARRIVE + f.u; break;
    case Game::ABD_HOLDING:  // 0x5461e0, then 0x545d20 chains the clips
        f.u = Game::ABD_HOLD - p->fuse, f.e = 99;
        f.clip = f.u < UFO_CLOSE ? "AbductCloseBeam" : f.u < UFO_CLOSE + UFO_VIOLATE ? "AbductViolate" : "AbductOpenDoors";
        f.t = f.u < UFO_CLOSE ? f.u : f.u < UFO_CLOSE + UFO_VIOLATE ? f.u - UFO_CLOSE : f.u - UFO_CLOSE - UFO_VIOLATE;
        break;
    case Game::ABD_SPITTING: f.clip = "AbductLoop2", f.t = fmodf(f.u, UFO_LOOP), f.e = 99; break;
    default: f.clip = "AbductEnd", f.u = f.t = Game::ABD_LEAVE - p->fuse, f.e = 99;
    }
    // Camera.FollowSceneCam on its "persp" but while it lifts and once the camera worm is spat out (StopFollowingSceneCam 0x548698, 0x547de9)
    f.scene = p->stage != Game::ABD_LIFTING && !(p->stage == Game::ABD_SPITTING && !g.aboard(g.abdCam));
    auto cross = [&](float at) { return was < at && f.e >= at; };
    Vector3 at = p->pos;
    if (was < 0) Audio::play(Audio::Sfx::UfoAppearing, at);
    if (cross(UFO_BEAM) && p->stage == Game::ABD_ARRIVING) Audio::play(Audio::Sfx::Abduction, at);  // AlienUfoBeamStart
    if (p->stage == Game::ABD_HOLDING && wasU < UFO_CLOSE && f.u >= UFO_CLOSE) Audio::play(Audio::Sfx::UfoActive, at);
    if (p->stage == Game::ABD_LEAVING && wasU < 0) Audio::play(Audio::Sfx::UfoTakeOff);  // 0x546170
    Audio::loop(Audio::Sfx::UfoEngine, dt > 0, &at);
    Audio::loop(Audio::Sfx::UfoBeamLoop, dt > 0 && f.e >= UFO_BEAM && f.stage <= Game::ABD_LIFTING, &at);
}

static Vector3 ufoJoint(const char *node) {
    Matrix j;
    return Models::joint("ufo", node, ufo.clip, ufo.t, false, &j) ? Vector3Transform({0, 0, 0}, MatrixMultiply(j, ufo.at)) : Vector3Transform({0, 0, 0}, ufo.at);
}

// the saucer's scene camera (updateUfo: ufo.scene)
static bool ufoCam(Camera3D *c) {
    Matrix j;
    if (!ufo.live || !ufo.scene || !Models::joint("ufo", "persp", ufo.clip, ufo.t, false, &j)) return false;
    Matrix m = MatrixMultiply(j, ufo.at);
    Vector3 p = Vector3Transform({0, 0, 0}, m);
    c->position = p, c->target = Vector3Add(p, Vector3Normalize(Vector3Subtract(Vector3Transform({0, 0, -1}, m), p)));
    c->up = Vector3Normalize(Vector3Subtract(Vector3Transform({0, 1, 0}, m), p)), c->fovy = SCENE_FOVY;
    return true;
}

static void drawUfo(float dt) {
    auto &f = ufo;
    if (!f.live) return;
    Models::draw("ufo", f.at, WHITE, f.clip, f.t);
    float gate = f.stage == Game::ABD_ARRIVING || f.stage == Game::ABD_FAILING ? f.u : f.stage == Game::ABD_LEAVING ? f.u - 0.25f : -1;  // WXM_DefSource at the CreatePoint on arrival, and 250 ms into AbductEnd (0x546170)
    Vector3 gp = ufoJoint("CreatePoint");
    if (gate >= 0 && gate < 4.58f) Models::draw("warpgate", MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixTranslate(gp.x, gp.y, gp.z)), WHITE, "WXM_DefSource", gate);
    Fx::ufo(Vector3Transform({0, 0, 0}, f.at), ufoJoint("beam"), gp, f.ground, f.e, gate, f.stage, dt);
}

// Shells point along their velocity; fused throwables (grenades) tumble instead.
// The map's particle emitters and weather (render only), after the match's Fx::theme
static void levelFx(const Game &g) {
    const Terrain &t = g.terrain;
    Fx::level(t.emitters, t.scale / 20, t.origin, t.theme, t.time, g.water, [&t](Vector3 from, float len, Vector3 *hit, Vector3 *n) {
        if (!t.raycast({from, {0, -1, 0}}, len, hit)) return false;
        return *n = t.normal(*hit), true;
    }, g.cfg.mission && g.cfg.mission->rainProb >= 0 ? g.cfg.mission->rainProb : t.rainProb);
}

static bool drawShot(const Projectile &s, float clock, const Terrain &t) {
    const WeaponDef &d = WEAPONS[s.weapon];
    const std::string &n = d.name;
    if (s.child && d.kind == Kind::SuperSheep) return true;  // starburst stars: particles only (Fx::trail)
    if (n == "Starburst") return true;  // the rider mounts it on its Pack_Locator (drawWorm)
    if (!s.child && d.kind == Kind::Airstrike) return true;  // the plane: drawBomber()
    if (d.kind == Kind::Abduction) return true;  // the saucer: drawUfo()
    if (n == "Fatkins Strike" && s.stage > 0) return true;  // still in the bomber
    if (d.kind == Kind::Airstrike && s.child && d.fuse <= 0)
        return Models::draw("airstrike", s.pos, atan2f(s.vel.x, s.vel.z), atan2f(s.vel.y, sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z)), WHITE, "Spin", clock);  // AnimTravel
    if (d.kind == Kind::Airstrike && d.fuse > 0 && drawBovine(s, clock)) return true;
    const char *m = n == "Fatkins Strike" ? "fatkins" : n == "Poison Arrow" ? "arrow" : n == "Dynamite" ? "dynamite"
                  : n == "Gas Canister" ? "gas" : d.kind == Kind::SuperSheep ? "supersheep" : d.kind == Kind::OldWoman ? "oldwoman"
                  : d.kind == Kind::Homing ? "homing" : d.kind == Kind::Scouser ? "scouser"
                  : d.kind == Kind::Sheep ? "sheep" : d.kind == Kind::Donkey ? "donkey" : d.kind == Kind::Airstrike ? "airstrike"
                  : n == "Cluster Grenade" ? (s.child ? "clusterlet" : "cluster") : n == "Banana Bomb" ? "banana"  // W4M bananettes reuse the BananaBomb mesh
                  : n == "Holy Hand Grenade" ? "holy" : d.fuse > 0 ? "grenade" : "bazooka";
    if (!d.model.empty()) m = d.model.c_str();  // Weapon Factory
    Vector3 v = d.stick > 0 && s.stage ? s.aim : s.vel;  // a stuck arrow keeps the heading it hit with
    float h = sqrtf(v.x * v.x + v.z * v.z), yaw = atan2f(v.x, v.z);
    if (d.kind == Kind::Sheep || d.kind == Kind::Donkey) return Models::draw(m, s.pos, yaw, 0, WHITE, "Run", clock);
    if (d.kind == Kind::OldWoman)  // AnimIntermediate "Steal" while she stands after a theft (Payload.PlayIntermediateAnim 0x5933c2)
        return Models::draw(m, {s.pos.x, s.pos.y - 0.3f, s.pos.z}, yaw, 0, WHITE, s.stage > 0 ? "Steal" : "Walk",
                            s.stage > 0 ? (msTicks(800) - s.stage) * Game::DT : clock, s.stage <= 0);
    if (d.kind == Kind::SuperSheep && n != "Starburst") return Models::draw(m, s.pos, yaw, atan2f(s.vel.y, h), WHITE, "Fly", clock);
    if (d.kind == Kind::Scouser) return Models::draw(m, s.pos, clock * 0.7f);
    if (dropped(d)) return Models::draw(m, restOn(t, s.pos, Models::bottom(m)), 0);  // set down standing, it doesn't tumble
    if (d.fuse > 0 && d.kind == Kind::Shell) return Models::draw(m, Vector3Length(s.vel) < 1 ? restOn(t, s.pos, Models::bottom(m)) : s.pos, clock * 6, clock * 4);
    if (d.stick > 0 && s.stage) return Models::draw(m, s.pos, yaw, atan2f(v.y, h), WHITE, "Hit", d.stick - s.fuse, false);  // AnimImpact, played once on Payload.Impact
    return Models::draw(m, s.pos, yaw, atan2f(v.y, h));
}

enum class Screen { Menu, Lobby, Play, Replays, Missions, Loading };

// Match frames over W4NX_HITCH_MS (default 20) to log.txt with their section ms (20 lines per 10 s at most, plus the
// window's worst frame), and every 10 s the sim ticks per frame histogram and the frame time spread.
struct Pace {
    double limit = getenv("W4NX_HITCH_MS") ? atof(getenv("W4NX_HITCH_MS")) : 20, sum = 0, sq = 0, worst = 0, jitter = 0, last = 0;
    int hist[4] = {}, frames = 0, over = 0, logged = 0, dropped = 0;
    unsigned seen[L_COUNT] = {}, sounds = 0;
    std::string worstLine;  // the worst frame's HITCH line while the rate limit held it back
    double gpuSum[16] = {}, gpuMax = 0;
    int gpuFrames = 0, gpuOver = 0;
    int gpuN = 0;
    void gpu(const double *ms, int n) {  // a frame's GPU ms of its first n sections, from GpuClock
        double t = 0;
        for (int i = 0; i < n; i++) gpuSum[i] += ms[i], t += ms[i];
        gpuFrames++, gpuMax = fmax(gpuMax, t), gpuOver += t > 1000 / 60.0, gpuN = n;
    }
    void frame(double ms, int ticks, int chunks, const double *cost, const char *const *names, int n) {
        unsigned now[L_COUNT], snd = Audio::started();
        memcpy(now, loads, sizeof now);
        if (frames++) jitter += fabs(ms - last);  // the first frame's delta spans the screen change
        last = ms, sum += ms, sq += ms * ms, hist[std::min(ticks, 3)]++;
        if (ms > limit) {
            char b[768];
            int k = snprintf(b, sizeof b, "HITCH %.1f ms, %d ticks, %d chunks, %d particles, %u sounds |", ms, ticks, chunks, Fx::count(), snd - sounds);
            double known = 0;
            for (int i = 0; i < n; i++) known += cost[i], k += snprintf(b + k, sizeof b - k, " %s %.1f", names[i], cost[i] * 1000);
            k += snprintf(b + k, sizeof b - k, " other %.1f | loads tex %u shader %u model %u wave %u image %u fbo %u", ms - known * 1000,
                          now[L_TEX] - seen[L_TEX], now[L_SHADER] - seen[L_SHADER], now[L_MODEL] - seen[L_MODEL], now[L_SOUND] - seen[L_SOUND],
                          now[L_IMAGE] - seen[L_IMAGE], now[L_FBO] - seen[L_FBO]);
            over++;
            if (logged++ < 20) TraceLog(LOG_INFO, "%s", b);
            else if (dropped++, ms > worst) worstLine = b;
        }
        worst = fmax(worst, ms);
        memcpy(seen, now, sizeof now), sounds = snd;
        if (frames < 600) return;
        if (!worstLine.empty()) TraceLog(LOG_INFO, "%s (worst of the window)", worstLine.c_str());
        double avg = sum / frames;
        TraceLog(LOG_INFO, "PACE %d frames: ticks/frame 0:%d 1:%d 2:%d 3+:%d | frame avg %.2f sd %.2f max %.1f ms, %d over %.0f ms (%d not logged) | jitter %.2f ms",
                 frames, hist[0], hist[1], hist[2], hist[3], avg, sqrt(fmax(0, sq / frames - avg * avg)), worst, over, limit, dropped, jitter / (frames - 1));
        if (gpuFrames) {
            char b[512];
            double t = 0;
            for (int i = 0; i < gpuN; i++) t += gpuSum[i];
            int k = snprintf(b, sizeof b, "GPU %d frames: avg %.2f max %.1f ms, %d over 16.7 ms |", gpuFrames, t / gpuFrames, gpuMax, gpuOver);
            for (int i = 0; i < gpuN; i++) k += snprintf(b + k, sizeof b - k, " %s %.2f", names[i], gpuSum[i] / gpuFrames);
            TraceLog(LOG_INFO, "%s", b);
        }
        *this = Pace{};
        memcpy(seen, now, sizeof now), sounds = snd;
    }
};

// GPU ms per perf section from timestamp queries read a few frames later, so nothing waits on the GPU
// (GL 3.3 core; GLES needs EXT_disjoint_timer_query)
#ifdef __SWITCH__
extern "C" void *eglGetProcAddress(const char *);
extern "C" const unsigned char *glGetString(unsigned);
#else
extern "C" void (*glad_glGenQueries)(int, unsigned *), (*glad_glQueryCounter)(unsigned, unsigned), (*glad_glGetQueryObjectiv)(unsigned, unsigned, int *),
    (*glad_glGetQueryObjectui64v)(unsigned, unsigned, uint64_t *);
#endif
extern "C" void glGetIntegerv(unsigned, int *);
struct GpuClock {
    static constexpr int F = 4, N = 16;  // frames in flight, marks per frame
    void (*gen)(int, unsigned *) = nullptr;
    void (*counter)(unsigned, unsigned) = nullptr;
    void (*avail)(unsigned, unsigned, int *) = nullptr;
    void (*get)(unsigned, unsigned, uint64_t *) = nullptr;
    unsigned q[F][N] = {};
    int sec[F][N] = {}, used[F] = {}, slot = 0;
    bool init() {
#ifdef __SWITCH__
        const char *ext = (const char *)glGetString(0x1F03);  // GL_EXTENSIONS
        if (!ext || !strstr(ext, "GL_EXT_disjoint_timer_query")) return false;
        gen = (void (*)(int, unsigned *))eglGetProcAddress("glGenQueriesEXT"), counter = (void (*)(unsigned, unsigned))eglGetProcAddress("glQueryCounterEXT");
        avail = (void (*)(unsigned, unsigned, int *))eglGetProcAddress("glGetQueryObjectivEXT");
        get = (void (*)(unsigned, unsigned, uint64_t *))eglGetProcAddress("glGetQueryObjectui64vEXT");
#else
        gen = glad_glGenQueries, counter = glad_glQueryCounter, avail = glad_glGetQueryObjectiv, get = glad_glGetQueryObjectui64v;
#endif
        if (!gen || !counter || !avail || !get) return gen = nullptr, false;
        gen(F * N, &q[0][0]);
        return true;
    }
    void mark(int section) {  // section: what ran since the previous mark (-1: frame start)
        if (section < 0) used[slot] = 0;
        if (gen && used[slot] < N) counter(q[slot][used[slot]], 0x8E28), sec[slot][used[slot]++] = section;  // GL_TIMESTAMP
    }
    // next frame; returns true and fills ms[] when the slot about to be reused had complete results
    bool next(double *ms, int n) {
        if (!gen) return false;
        slot = (slot + 1) % F;
        int k = used[slot], ready = 1, disjoint = 0;
        used[slot] = 0;
        if (k < 2) return false;
        avail(q[slot][k - 1], 0x8867, &ready);  // GL_QUERY_RESULT_AVAILABLE: the last mark implies the others
#ifdef __SWITCH__
        glGetIntegerv(0x8FBB, &disjoint);  // GL_GPU_DISJOINT_EXT: a clock change voids the frame
#endif
        if (!ready || disjoint) return false;
        uint64_t t[N];
        for (int i = 0; i < k; i++) get(q[slot][i], 0x8866, &t[i]);  // GL_QUERY_RESULT
        for (int i = 0; i < n; i++) ms[i] = 0;
        for (int i = 1; i < k; i++) if (sec[slot][i] >= 0 && sec[slot][i] < n) ms[sec[slot][i]] += (t[i] - t[i - 1]) / 1e6;
        return true;
    }
};

int main(int argc, char **argv) {
    SetTraceLogCallback(logLine);
    if ((argc > 1 && !strcmp(argv[1], "--netbot")) || getenv("W4NX_HIDDEN")) SetConfigFlags(FLAG_WINDOW_HIDDEN);
    int winW = 1280, winH = 720;  // W4NX_GFX="msaa aniso=N res=WxH": graphics levers timed by --bench (docs/tests.md "Render budget")
    if (const char *g = getenv("W4NX_GFX")) {
        if (strstr(g, "msaa")) SetConfigFlags(FLAG_MSAA_4X_HINT);
        if (const char *a = strstr(g, "aniso=")) Lit::aniso = atoi(a + 6);
        if (const char *r = strstr(g, "res=")) sscanf(r + 4, "%dx%d", &winW, &winH);
    }
    InitWindow(winW, winH, "Worms4NX");
    SetExitKey(KEY_NULL);  // Esc is back / pause; quit from the title screen
#ifdef __SWITCH__
    SetTargetFPS(0);  // eglSwapInterval(1) paces the frames; raylib's busy-wait timer on top of it beats against the vblank
#else
    SetTargetFPS(60);
#endif
    rlSetClipPlanes(0.5, 500);  // default 0.01 near plane z-fights the water on GLES depth buffers
    // boot splash: W4M's spinning worm on black while a worker decodes the assets and this thread uploads them
    static Texture2D bootWorm = LoadTexture(DATA_DIR "assets/ui/fe2/loading_worm.png");
    static const double bootT0 = GetTime();
    auto boot = [] {
        BeginDrawing();
        ClearBackground(BLACK);
        if (bootWorm.id) {
            float s = 200, deg = (float)fmod((GetTime() - bootT0) * 180, 360);  // double clock: raw GetTime() is huge on Switch
            DrawTexturePro(bootWorm, {0, 0, (float)bootWorm.width, (float)bootWorm.height}, {640, 360, s, s}, {s / 2, s / 2}, deg, WHITE);
        }
        EndDrawing();
    };
    // GL-only work first, on black: shader compiles and the font can't run on the workers
    double bt[5] = {GetTime()};  // BOOT log marks
    Ui::load();
    Fx::load();
    Models::upload(0);  // compiles the model shader
    bt[1] = GetTime();
    std::vector<std::string> maps = {""};  // "" = procedural island
    std::thread sounds([&] {
        Audio::init();
        for (const char *dir : {ROMFS_DIR "maps", DATA_DIR "assets/maps"}) {  // assets/ = maps imported from the user's W4M install
            if (!DirectoryExists(dir)) continue;
            FilePathList files = LoadDirectoryFilesEx(dir, ".json", false);
            for (unsigned i = 0; i < files.count; i++) {
                std::string m = files.paths[i];  // GetFileNameWithoutExt() isn't thread-safe
                m = m.substr(m.find_last_of('/') + 1), m.resize(m.size() - 5);
                if (std::find(maps.begin(), maps.end(), m) == maps.end()) maps.push_back(m);
            }
            UnloadDirectoryFiles(files);
        }
    });
    std::thread meshes(Models::prepare);
    Controls::load(DATA_DIR "controls.txt");
    BeginDrawing(), ClearBackground(BLACK), EndDrawing();  // flushes those uploads before the spinner starts
    for (double until = 0; Ui::preload(until) | Models::upload(until); until = GetTime() + 0.008) boot();  // ~half a frame of uploads
    bt[2] = GetTime();
    meshes.join(), sounds.join();
    std::sort(maps.begin() + 1, maps.end(), [](const std::string &a, const std::string &b) {  // readdir order differs between PC and Switch
        int c = strcasecmp(Ui::mapTitle(a).c_str(), Ui::mapTitle(b).c_str());
        return c ? c < 0 : a < b;
    });
    bt[3] = GetTime();
    FrontBg::load();
    bt[4] = GetTime();
    Audio::music(true);
    TraceLog(LOG_INFO, "BOOT: gl %.0f ms, decode+upload %.0f, join %.0f, menu scene %.0f, music %.0f, total %.0f", (bt[1] - bt[0]) * 1000,
             (bt[2] - bt[1]) * 1000, (bt[3] - bt[2]) * 1000, (bt[4] - bt[3]) * 1000, (GetTime() - bt[4]) * 1000, (GetTime() - bt[0]) * 1000);
    logAsync();
    // --animshot <clip> [held] [aim clip] [aim t]: 8 poses of a worm clip (animshot.png) and quit; --animshot <weapon>: a turn firing it, anim_<frame>.png
    if (argc > 2 && !strcmp(argv[1], "--animshot") && (argv[2][0] < '0' || argv[2][0] > '9')) {
        const char *mdl = getenv("W4NX_MODEL") ? getenv("W4NX_MODEL") : "worm";  // W4NX_MODEL / W4NX_ZOOM: another model, camera distance x
        float L = Models::clipLength(mdl, argv[2]), zoom = getenv("W4NX_ZOOM") ? (float)atof(getenv("W4NX_ZOOM")) : 1;
        Camera3D c = {{0, 0.4f * zoom, 11 * zoom}, {0, 0.4f * zoom, 0}, {0, 1, 0}, 30, CAMERA_PERSPECTIVE};
        Models::Layers ly;  // W4NX_LAYERS="<face clip> lookYaw lookPitch [gestYaw gestPitch eyeYaw eyePitch]": acting layers on every pose
        static char face[64] = "";
        bool layered = getenv("W4NX_LAYERS") && sscanf(getenv("W4NX_LAYERS"), "%63s %f %f %f %f %f %f", face, &ly.lookYaw, &ly.lookPitch, &ly.gestYaw,
                                                        &ly.gestPitch, &ly.eyeYaw, &ly.eyePitch) >= 1;
        ly.face = strcmp(face, "-") ? face : nullptr;
        static char act[64] = "";  // W4NX_ACT="<gesture> <weight>": that acting gesture over the clip, same time
        if (getenv("W4NX_ACT") && sscanf(getenv("W4NX_ACT"), "%63s %f", act, &ly.actW[0]) == 2) ly.act[0] = act, layered = true;
        BeginDrawing();
        ClearBackground(SKYBLUE);
        Lit::frame(c.position);
        BeginMode3D(c);
        for (int k = 0; k < 8; k++) {
            Vector3 p = Vector3Scale({-3.6f + (k % 4) * 2.4f, k < 4 ? 1 : -1.6f, 0}, zoom);
            Matrix m;
            const char *aim = argc > 5 ? argv[4] : nullptr;
            float aimT = argc > 5 ? atof(argv[5]) : 0;
            ly.actT[0] = L * k / 7;
            Models::draw(mdl, p, getenv("W4NX_YAW") ? (float)atof(getenv("W4NX_YAW")) : PI / 2, 0, WHITE, argv[2], L * k / 7, false, aim, aimT, layered ? &ly : nullptr);
            const char *loc = getenv("W4NX_LOC") ? getenv("W4NX_LOC") : "WeaponLocator";  // W4NX_LOC: another attach joint (Pack_Locator)
            if (argc > 3 && Models::joint("worm", loc, argv[2], L * k / 7, false, &m, aim, aimT))
                Models::draw(argv[3], MatrixMultiply(!strcmp(argv[3], "hold_starburst") && !getenv("W4NX_LOC") ? MatrixMultiply(MatrixRotateX(-PI / 2), m) : m, MatrixMultiply(MatrixRotateY(PI / 2), MatrixTranslate(p.x, p.y, p.z))), WHITE, getenv("W4NX_LOC") || Models::clipLength(argv[3], argv[2]) > 0 ? argv[2] : "Rest", L * k / 7);
        }
        EndMode3D();
        for (int k = 0; k < 8; k++) Ui::text(TextFormat("%.2fs", L * k / 7), 200 + (k % 4) * 290, k < 4 ? 20 : 380, 24, BLACK, 1);
        Ui::text(TextFormat("%s %.2fs", argv[2], L), 640, 690, 24, BLACK, 1);
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, "animshot.png");
        EndDrawing();
        return 0;
    }

    // Shot mode (flag file or --shot): scripted turn, screenshot, quit. Lets us check rendering in the emulator.
    // --cpu [map] [level]: every team is played by the AI (until the team setup menu lands)
    // --ui title|main|local|network|myworms|helpopts|confirm|setup|options|hud|panel|ready|loading|gameover [map]: capture that screen to ui.png (loading: ui_<frame>.png) and quit
    const char *uiShot = argc > 2 && !strcmp(argv[1], "--ui") ? argv[2] : nullptr;
    // shot flag file "ui <screen> [frames...] [map]": the same, ui_<frame>.png at each frame (Switch has no args); intro = title, A at frame 20, Local at 80
    char *flag = argc <= 3 && FileExists(DATA_DIR "shot") ? LoadFileText(DATA_DIR "shot") : nullptr, flagUi[32], capPath[64], flagMap[64] = "";
    std::vector<int> uiFrames;
    if (int n = 0, f; flag && sscanf(flag, "ui %31s%n", flagUi, &n) == 1) {
        char *p = flag + n;
        for (; sscanf(p, "%d%n", &f, &n) == 1; p += n) uiFrames.push_back(f);
        sscanf(p, "%63s", flagMap);  // optional map after the frames
        uiShot = flagUi, UnloadFileText(flag), flag = nullptr;
    }
    bool intro = uiShot && !strcmp(uiShot, "intro");
    bool loadShot = uiShot && !strcmp(uiShot, "loading");
    if (uiFrames.empty() && loadShot) uiFrames = {20, 60, 120, 170};  // intro, intro, loading screen x2
    if (uiFrames.empty()) uiFrames.push_back(10);
    // --bench <map> [frames] [nosync]: CPU-vs-CPU match, uncapped, one sim tick per frame, prints per-section ms and exits
    bool bench = argc > 2 && !strcmp(argv[1], "--bench");
    int benchFrames = bench && argc > 3 ? atoi(argv[3]) : 1200;
    // --netbot host port name create|join [turns] [map] [rules] [scheme] [roundMin]: own teams played by the AI online,
    // checksums on stdout, exit 1 on desync. The token is kept in ./netbot.token so a restarted bot resumes its match.
    bool netbot = argc > 5 && !strcmp(argv[1], "--netbot"), botCreate = netbot && !strcmp(argv[5], "create"), desynced = false;
    int botTurns = netbot && argc > 6 ? atoi(argv[6]) : 6, turns = 0;
    bool cpuAll = argc > 1 && (!strcmp(argv[1], "--cpu") || bench), shot = !cpuAll && !uiShot && !netbot && (argc > 1 || FileExists(DATA_DIR "shot"));
    int shotWeapon = argc > 2 ? atoi(argv[2]) : 0;  // --shot N: use weapon N
    // --aimshot <weapon> [map] [fine]: shot mode held in aim mode, aim.png at frame 60
    bool aimShot = shot && argc > 2 && !strcmp(argv[1], "--aimshot");
    // --aimseq <weapon> [map]: aim mode from frame 40 to 90, aimseq_NNN.png around the entry and the exit
    bool aimSeq = shot && argc > 2 && !strcmp(argv[1], "--aimseq");
    bool animShot = shot && argc > 2 && !strcmp(argv[1], "--animshot");
    // --utilshot <weapon name> [map]: that weapon in hand from the start (one unit), a girder preview stepped ahead and up
    const char *utilShot = shot && argc > 2 && !strcmp(argv[1], "--utilshot") ? argv[2] : nullptr;
    uint8_t prevShotJump = 0;
    if (utilShot && (!strcmp(utilShot, "Binoculars") || strstr(utilShot, "Homing"))) Controls::forceAim = 1;
    if (aimShot) Controls::forceAim = argc > 4 && !strcmp(argv[4], "fine") ? 2 : 1;
    // W4NX_BENCH=<frames> [W4NX_LOCK=1] with --aimshot: that view, still from frame 60 (homing: locked), timed as --bench;
    // with --shot: that weapon fired (explosion spikes), timed the same
    int shotBench = shot && getenv("W4NX_BENCH") ? atoi(getenv("W4NX_BENCH")) : 0, aimBench = aimShot ? shotBench : 0;
    if (shotBench) bench = true, benchFrames = shotBench, SetTargetFPS(0);
    if (bench) countGl();
    if (!loadWeapons(ROMFS_DIR "weapons.json")) TraceLog(LOG_WARNING, "weapons.json missing or invalid, using built-in weapons");

    // server.txt on the SD card: "<host> [port] [name]", rewritten by the Options screen
    std::string host = "127.0.0.1", name = "Worm";
    int port = 7777;
    if (char *txt = LoadFileText(DATA_DIR "server.txt")) {
        char h[64] = "", n[32] = "";
        int k = sscanf(txt, "%63s %d %31s", h, &port, n);
        if (k >= 1) host = h;
        if (k >= 3) name = n;
        UnloadFileText(txt);
    }

    Game game;
    Net net;
    bool online = netbot;
    Screen screen = shot ? Screen::Play : netbot ? Screen::Lobby : Screen::Menu;
    int roomSel = 0, lastSec = -1;
    GameConfig opt;
    uint32_t tick = 0;
    std::string status;
    std::map<uint32_t, float> offlineSince;  // player id -> clock when seen offline
    // --shot [weapon] [map] [rules]
    // the shot flag file may name the map (Switch has no args)
    std::string shotMap = argc > 3 ? argv[3] : "";
    // --view <map> x y z tx ty tz: shot mode from a fixed camera (overview captures)
    bool fixedView = argc > 8 && !strcmp(argv[1], "--view");
    Camera3D viewCam = {{0, 0, 0}, {0, 0, 0}, {0, 1, 0}, Controls::FOV0, CAMERA_PERSPECTIVE};
    if (fixedView) {
        shotMap = argv[2];
        viewCam.position = {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])};
        viewCam.target = {(float)atof(argv[6]), (float)atof(argv[7]), (float)atof(argv[8])};
    }
    if (flag) {
        char m[64] = "";
        if (sscanf(flag, "%63s", m) == 1) shotMap = m;
        UnloadFileText(flag);
    }
    if (shot) {
        GameConfig sc = {1234, 2, 2, shotMap, argc > 4 && !fixedView ? (uint32_t)atoi(argv[4]) : 0u};
        if (utilShot) loadCustomWeapons(DATA_DIR "custom_weapons.json", sc.custom);  // --utilshot <Factory weapon name>
        game.start(sc);
        Audio::preloadVoices(game.teams);
        game.terrain.remesh();
        Fx::theme(game.terrain.theme, game.terrain.sky, game.terrain.time);
        levelFx(game);
    }
    for (size_t i = 0; (utilShot || shotBench) && i < WEAPONS.size(); i++)
        if ((utilShot && WEAPONS[i].name == utilShot) || (shotBench && (int)i == shotWeapon)) game.ammo[game.worms[game.current].team][i] = 1, game.delays[game.worms[game.current].team][i] = 0, game.weapon = (int)i, game.picked.assign(game.teams, (int)i);

    Camera3D cam = {{40, 30, 0}, {40, 8, 40}, {0, 1, 0}, Controls::FOV0, CAMERA_PERSPECTIVE};
    float acc = 0, clock = 0, reconnectAt = 0;
    // perf overlay (L+R / F3 cycles off, CPU, GPU-synced): ms per section, smoothed. CPU mode only times command
    // submission (GPU work lands in "present"); synced mode glFinish()es after each section to charge the GPU cost to it.
    enum { T_SIM, T_REMESH, T_CAMERA, T_SHADOW, T_PIP, T_SKY, T_TERRAIN, T_DECOR, T_MODELS, T_FX, T_UI, T_PRESENT, T_COUNT };
    static const char *const NAMES[T_COUNT] = {"sim", "remesh", "camera", "shadow", "pip", "sky+water", "terrain", "decor", "models", "fx", "ui", "present+wait"};
    double perf[T_COUNT] = {}, cost[T_COUNT] = {}, cpuCost[T_COUNT] = {}, mark = 0;
    int perfOn = bench ? (argc > 4 ? 1 : 2) : 0;  // --bench map frames nosync: real fps, no glFinish
    GpuClock gpuClock;
    TraceLog(LOG_INFO, "GPU: timestamp queries %s", gpuClock.init() ? "on" : "unavailable");
    auto lap = [&](int k) {
        if (perfOn == 2 || gpuClock.gen) rlDrawRenderBatchActive();  // the batch's draws belong to this section
        gpuClock.mark(k);
        double c = GetTime();  // before glFinish: CPU submission only
        if (perfOn == 2) glFinish();
        double t = GetTime();
        cpuCost[k] += c - mark, cost[k] += t - mark, mark = t;
    };
    Pace pace;
    int paceFrame = -2, stepped = 0;  // stepped: sim ticks this frame
    double paceAt = 0;
    double benchSum[T_COUNT] = {}, benchCpu[T_COUNT] = {}, benchMax[T_COUNT] = {}, benchGpu[T_COUNT] = {}, benchGpuN = 0, benchStart = 0, frameMax = 0, frameSq = 0, frameStart = 0, benchDraws = 0, benchBinds = 0;

    // Match recording (saved to replays/ at game over or quit), Replays playback, instant replay of big shots (local only).
    Recording rec, play;
    Snapshot snap;  // turn start, for the instant replay
    bool recSaved = true, playing = false, paused = false, freeCam = false, instant = true, shotDone = false;
    int speed = 1, irEnd = -1, replaySel = 0;  // irEnd: live tick the instant replay catches up to, -1 = live
    uint32_t irTick = 0, irLive = 0, fireTick = 0, shotTick = 0;
    bool irSwallow = false;  // the A that skipped the replay must not jump/fire afterwards
    float irAcc = 0, fcYaw = 0, fcPitch = 0;
    Vector3 fcPos{};
    std::vector<std::string> replayFiles;
    if (char *t = FileExists(DATA_DIR "replay.txt") ? LoadFileText(DATA_DIR "replay.txt") : nullptr) instant = t[0] != '0', UnloadFileText(t);
    std::thread saver;  // the SD write takes 100+ ms on Switch: off the frame
    auto saveRec = [&] {
        if (recSaved || playing || rec.inputs.empty() || shot || bench || uiShot || netbot || rec.cfg.mission) return;  // replays don't carry missions
        recSaved = true;
        rec.checksum = irEnd < 0 && rec.inputs.size() == tick ? game.checksum() : 0;
        if (saver.joinable()) saver.join();
        saver = std::thread([r = rec, path = std::string(DATA_DIR "replays/") + replayName(rec.cfg)] {
            MakeDirectory(DATA_DIR "replays");
            if (!r.save(path)) TraceLog(LOG_WARNING, "cannot save %s", path.c_str());
        });
    };
    auto irFinish = [&] {  // back to the live state set aside at the replay's start
        if (irEnd < 0) return;
        if (irTick == (uint32_t)irEnd && game.checksum() != irLive) TraceLog(LOG_ERROR, "instant replay diverged from the live state");  // local only: keep playing
        if (!snap.forward(game))
            for (; irTick < (uint32_t)irEnd; irTick++) game.step(rec.inputs[irTick]);
        irEnd = -1;
        irSwallow = true;
        Fx::clear();
        Audio::stopSfx();  // the skipped ticks' sounds must not outlive the replay
    };
    int livePad = -1;  // pad of the human playing this turn here, -1 none
    auto feel = [&](const GameEvent &e) {  // HD rumble, local players' own pads only
        if (playing || shot || bench || netbot || uiShot) return;
        auto padOf = [&](int team) {
            if (team < (int)game.cfg.teamSetup.size() && game.cfg.teamSetup[team].cpu) return -1;
            if (online) return team < (int)net.owners.size() && net.owners[team] == net.id ? 0 : -1;
            return IsGamepadAvailable(team) ? team : 0;
        };
        int view = online ? 0 : livePad >= 0 ? livePad : 0;
        if (e.kind == GameEvent::Fire && e.worm >= 0) Controls::rumble(livePad, 0.3f, 0.08f);
        if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom)
            Controls::rumble(view, (e.kind == GameEvent::BigBoom ? 1 : 0.8f) * Clamp(1 - Vector3Distance(e.pos, cam.target) / 30, 0, 1), 0.3f);
        if (e.kind == GameEvent::Fall && e.worm >= 0) Controls::rumble(padOf(game.worms[e.worm].team), 100 / 255.0f, 0.5f);  // W4M 0x5ac3e0: RumbleService Heavy 100, 500 ms
    };
    auto stepOnce = [&](const Input &in) {
        auto flying = [&] { return game.phase == Phase::Flying || (game.phase == Phase::Settle && !game.shots.empty()); };  // retreat can end mid-flight
        bool was = flying();
        game.step(in);
        tick++, stepped++;
        if (!playing) rec.inputs.push_back(in);
        if (was && !flying()) shotDone = true, shotTick = tick;
        for (const GameEvent &e : game.events) {
            onEvent(game, e);
            feel(e);
            if (e.kind == GameEvent::Fire && e.worm >= 0) fireTick = tick;
            if (e.kind == GameEvent::TurnStart && !online && !playing) snap.take(game, tick);
            if (e.kind == GameEvent::GameOver) saveRec();
            if (e.kind != GameEvent::TurnStart && e.kind != GameEvent::GameOver) continue;
            if (online) net.turnEnd(tick, game.checksum());
            if (netbot) printf("[%s] turn %d tick %u checksum %08x%s\n", name.c_str(), ++turns, tick, game.checksum(), e.kind == GameEvent::GameOver ? " gameover" : "");
        }
        if (snap.valid && !online && !playing && tick != snap.tick && (tick - snap.tick) % 30 == 0) snap.mark(game, tick);  // a replay re-simulates 30 ticks at most
    };
    // Match prep while the loading screen animates: CPU work (sim start, texture decode, voices) on `loader`, then
    // GL steps between frames. The sim starts (and online inputs leave net's buffer) once screen is Play.
    GameConfig loadCfg;
    int loadStep = -1, warmIcon = 0;  // -1: done
    double loadMs[5] = {}, loadT0 = 0;
    std::thread loader;
    std::atomic<bool> loaderDone{false};
    auto prepStep = [&](bool wait) {
        double t0 = GetTime();
        switch (loadStep) {
        case 0: FrontBg::unload(); break;  // ~9 MB of menu scene; the next menu frame reloads it
        case 1:
            if (!loaderDone && !wait) return;
            loader.join();
            loadMs[1] = (GetTime() - loadT0) * 1000;  // wall clock, the screen kept drawing
            Fx::theme(game.terrain.theme, game.terrain.sky, game.terrain.time);
            levelFx(game);
            game.terrain.remesh(0);  // texture upload and shadow columns only
            break;
        case 2: game.terrain.remesh(0.012); break;
        case 3: game.terrain.drawObjects(0, false), Audio::preloadMusic("victory"), Audio::preloadMusic("theme"); break;  // decor models; the match-end tracks
        case 4: if (Ui::warmHud(warmIcon, wait ? 1e30 : GetTime() + 0.008)) return; break;  // ~half a frame of uploads
        }
        if (loadStep != 1 || wait) loadMs[loadStep] += (GetTime() - t0) * 1000;
        else loadMs[0] = std::max(loadMs[0], (GetTime() - t0) * 1000);  // longest main-thread block of step 1
        if (loadStep == 2 && std::count(game.terrain.dirty.begin(), game.terrain.dirty.end(), true)) return;
        if (++loadStep == 5)
            loadStep = -1, TraceLog(LOG_INFO, "LOAD: thread %.0f ms (start, decode, voices), upload %.0f, remesh %.0f, decor %.0f, total %.0f",
                                    loadMs[1], loadMs[0], loadMs[2], loadMs[3], (GetTime() - loadT0) * 1000);
    };
    auto mapMusic = [&] { Audio::music(true, game.terrain.theme.empty() ? "theme" : game.terrain.theme.c_str()); };  // once the match shows
    auto loadProgress = [&] {
        if (loadStep < 0) return 1.0f;
        const std::vector<bool> &d = game.terrain.dirty;
        float part = loadStep == 2 && !d.empty() ? 1 - (float)std::count(d.begin(), d.end(), true) / d.size() : 0;
        return (loadStep + part) / 5;
    };
    auto startMatch = [&](const GameConfig &c) {
        game.terrain.undo = nullptr;
        snap.valid = false;
        rec = {c, {}, 0};
        recSaved = shotDone = false;
        irEnd = -1;
        if (loader.joinable()) loader.join();  // online: Start again mid-load
        loadCfg = c, loadStep = 0, warmIcon = 0;
        for (double &m : loadMs) m = 0;
        loaderDone = false, loadT0 = GetTime();  // from the intro on: off the main thread's core it doesn't touch the menu frames
        loader = std::thread([&] {
            Loading::pinCore(1), game.start(loadCfg), game.terrain.decodeTextures();
            if (std::string f = Fx::skyFile(game.terrain.theme, game.terrain.time); FileExists(f.c_str())) Models::decode(f.c_str());
            Audio::preloadVoices(game.teams), loaderDone = true;
        });
        tick = 0;
        acc = 0;
        Controls::reset();
        if (bench || netbot || (uiShot && !loadShot)) {  // no loading screen: game is ready on return
            while (loadStep >= 0) prepStep(true);
            mapMusic();
            screen = Screen::Play;
        } else Loading::begin(c), screen = Screen::Loading;
    };
    bool botStarted = false;
    float botAt = 0, botDone = 1e9f;
    int proxyTurn = -1, botSpeed = getenv("W4NX_SPEED") ? atoi(getenv("W4NX_SPEED")) : 8;  // ticks per frame
    // LAN: this console hosts (embedded relay, its own Net joins over loopback) or joins a game heard on the LAN
    LanHost lanHost;
    LanScan lanScan;
    bool lan = false;
    int lanSel = 0;
    std::string connHost = host;  // where `net` connects / reconnects (host:port stays the Options server)
    int connPort = port;
    auto leaveLan = [&] { net.close(), lanHost.close(), net.roomId = 0, net.players.clear(), net.profiles.clear(); };
    if (netbot) {  // host "lan": create = host the LAN game on `port`, join = find it by its beacon
        host = argv[2], port = atoi(argv[3]), name = argv[4];
        lan = host == "lan";
        connHost = lan ? "127.0.0.1" : host, connPort = port;
        if (FILE *f = fopen("netbot.token", "r")) botStarted = fscanf(f, "%llx", (unsigned long long *)&net.token) == 1, fclose(f);
        opt.map = argc > 7 ? argv[7] : "";
        opt.rules = argc > 8 ? (uint32_t)atoi(argv[8]) : 0;
        if (argc > 9) opt.scheme = SCHEMES[atoi(argv[9]) % SCHEMES.size()].s;
        if (argc > 10) opt.scheme.roundTime = atoi(argv[10]);
        opt.teamSetup = {{"Bot " + name}};
        if (getenv("W4NX_CPU")) opt.teamSetup.push_back({"CPU", (uint8_t)atoi(getenv("W4NX_CPU"))});  // host's CPU team level
        opt.teams = (int)opt.teamSetup.size();
        if (lan && botCreate && !lanHost.open(port, "netbot")) return printf("[%s] cannot host on port %d\n", name.c_str(), port), 1;
        if (lan && !botCreate) lanScan.open();
        else if (!net.connect(connHost.c_str(), connPort, name.c_str())) return printf("[%s] cannot reach %s\n", name.c_str(), host.c_str()), 1;
    }
    if (cpuAll) {
        if (bench) SetTargetFPS(0), opt.seed = 1234;
        opt.map = argc > 2 ? argv[2] : "";
        opt.teamSetup.assign(4, {"CPU", (uint8_t)(!bench && argc > 3 ? atoi(argv[3]) : 2)});
        startMatch(opt);
    }
    Ai ai;
    Ui::Frontend front;
    Ui::Hud hud;
    Ui::Pause pause;
    // Single player: missions list (loaded on first use), progress.txt, the end-of-mission screen's choice
    std::vector<MissionSpec> missions;
    Progress progress;
    Ui::MissionMenu missionMenu;
    int missionIdx = -1, missionAct = 0;
    bool missionSaved = false;
    auto openMissions = [&] {
        if (missions.empty()) missions = listMissions(ROMFS_DIR, DATA_DIR), progress.load(DATA_DIR "progress.txt");
        screen = Screen::Missions;
    };
    auto startMission = [&](int i) {
        missionIdx = i, missionSaved = false, online = false;
        startMatch(missionConfig(missions[i], (uint32_t)(GetTime() * 1000)));
    };
    // help | helpmenu: the hold - controls overlay over a match / the main menu
    Ui::forceHelp = uiShot && (!strcmp(uiShot, "help") || !strcmp(uiShot, "helpmenu"));
    if (uiShot && (!strcmp(uiShot, "hud") || !strcmp(uiShot, "panel") || !strcmp(uiShot, "pause") || !strcmp(uiShot, "help") || !strcmp(uiShot, "ready") || !strcmp(uiShot, "loading") || !strcmp(uiShot, "gameover"))) {
        startMatch({1234, 2, 2, argc > 3 ? argv[3] : flagMap, 0u, {{"Red Rockets"}, {"Blue Bombers"}}});
        if (strcmp(uiShot, "ready")) game.hotSeat = 0;  // every shot but "ready" skips the hot-seat pause
        for (size_t i = 0; argc > 4 && i < WEAPONS.size(); i++) if (WEAPONS[i].name == argv[4]) game.weapon = (int)i;  // --ui hud <map> <weapon>
        hud.open = !strcmp(uiShot, "panel");
        pause.open = !strcmp(uiShot, "pause");
        if (!strcmp(uiShot, "gameover")) game.phase = Phase::GameOver, game.winner = 0, onEvent(game, {GameEvent::GameOver, {}, -1, -1});  // runs 10 s: W4NX_CAPFRAMES
    } else if (uiShot) {
        front.screen = !strcmp(uiShot, "main") || Ui::forceHelp ? Ui::Frontend::Main : !strcmp(uiShot, "setup") ? Ui::Frontend::Setup
                     : !strcmp(uiShot, "options") ? Ui::Frontend::Options : !strcmp(uiShot, "controls") ? Ui::Frontend::Controls : Ui::Frontend::Title;
        static const std::pair<const char *, Ui::Frontend::Screen> MENUS[] = {{"local", Ui::Frontend::Local}, {"network", Ui::Frontend::Network},
            {"myworms", Ui::Frontend::MyWorms}, {"helpopts", Ui::Frontend::HelpOpts}, {"confirm", Ui::Frontend::Confirm}};
        for (auto &m : MENUS) if (!strcmp(uiShot, m.first)) front.screen = m.second;
        if (!strcmp(uiShot, "wormpot")) front.screen = Ui::Frontend::Wormpot, opt.wormpot = WP_DOUBLE_DAMAGE | WP_CRATE_SHOWER << 8 | WP_QUICK_WALK << 16;
        if (!strcmp(uiShot, "factory") || !strcmp(uiShot, "weapon")) front.screen = !strcmp(uiShot, "weapon") ? Ui::Frontend::FactoryEdit : Ui::Frontend::Factory;
        if (!strcmp(uiShot, "replays") || !strcmp(uiShot, "playback")) replayFiles = listReplays(DATA_DIR "replays"), screen = Screen::Replays;
        if (!strcmp(uiShot, "playback") && !replayFiles.empty() && play.load(DATA_DIR "replays/" + replayFiles[0])) playing = true, startMatch(play.cfg);
        // missions | briefing | missionhud | missionend [mission id]
        if (!strncmp(uiShot, "mission", 7) || !strcmp(uiShot, "briefing")) openMissions(), missionMenu.brief = !strcmp(uiShot, "briefing");
        for (size_t i = 0; i < missions.size() && (!strcmp(uiShot, "missionhud") || !strcmp(uiShot, "missionend")); i++)
            if (argc > 3 ? missions[i].id == argv[3] : i == 0) {
                startMission((int)i);
                if (!strcmp(uiShot, "missionend")) game.run.result = 1, game.run.ticks = 5000, game.phase = Phase::GameOver;
                break;
            }
    }
    auto cpu = [&](int team) { return team < (int)game.cfg.teamSetup.size() && game.cfg.teamSetup[team].cpu > 0; };
    auto pressed = [](std::initializer_list<int> buttons, std::initializer_list<int> keys) { return pressedAny(-1, buttons, keys); };

    // W4NX_INPUTSCRIPT=file of "<frame> <raylib key> <1 down|0 up>" (key -1 = exit): replayed key events, screen changes logged
    FILE *inScript = getenv("W4NX_INPUTSCRIPT") ? fopen(getenv("W4NX_INPUTSCRIPT"), "r") : nullptr;
    int sf = -1, sk = 0, sd = 0;
    Screen shown = screen;
    // W4NX_CAPFRAMES="10 11 12": cap_<frame>.png of the match / loading screen at those frames, fixed dt
    std::vector<int> capFrames;
    for (char *c = getenv("W4NX_CAPFRAMES"), *e; c && *c; c = e) { int f = strtol(c, &e, 10); if (e == c) break; capFrames.push_back(f); }
    auto capture = [&](int frame) {
        if (!std::count(capFrames.begin(), capFrames.end(), frame)) return;
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, TextFormat("cap_%d.png", frame));
        UnloadImage(img);
    };
    for (int frame = 0; !WindowShouldClose(); frame++) {
        // a frame shown for whole vblanks steps exactly that many ticks: timing noise must not make 0 / 2 tick frames at 60 Hz
        float raw = fminf(GetFrameTime(), 0.25f), vblanks = roundf(raw / Game::DT);
        bool fixedDt = bench || shot || uiShot || !capFrames.empty(), paced = !fixedDt && vblanks >= 1 && fabsf(raw - vblanks * Game::DT) < 0.002f;
        float dt = fixedDt ? Game::DT : paced ? vblanks * Game::DT : raw;  // fixed: reproducible captures
        if (paced) acc = floorf(acc / Game::DT) * Game::DT + Game::DT / 2;
        clock += dt;
        Lit::profile(GetFrameTime());
        bool scriptEnd = false;
        for (; inScript && (sf >= 0 || fscanf(inScript, "%d %d %d", &sf, &sk, &sd) == 3) && sf <= frame; sf = -1)
            if (sk < 0) scriptEnd = true;
            else PlayAutomationEvent({0, sd ? 2u : 1u, {sk}});  // rcore.c INPUT_KEY_DOWN / INPUT_KEY_UP
        if (inScript && (screen != shown || frame == 0)) printf("frame %d screen %d\n", frame, (int)(shown = screen)), fflush(stdout);
        if (scriptEnd) break;
        Ui::pollStick();
        Audio::update();
        Controls::update(dt);

        if (online) {
            lanHost.poll();
            net.poll();
            Net::Event e;
            while (net.next(e)) {
                switch (e.type) {
                case Net::Welcome:
                    if (lanHost.running()) net.createRoom((name + "'s game").c_str(), 4);
                    else net.listRooms();
                    status = "Connected to " + connHost;
                    break;
                case Net::RoomList: if (lan && !net.roomId && !net.rooms.empty()) net.joinRoom(net.rooms[0].id); break;
                case Net::RoomState: if (net.roomId && !opt.teamSetup.empty()) net.sendProfile(opt.teamSetup[0], opt.teams - 1); break;
                case Net::Error: status = "Server: " + e.text; break;
                case Net::Start: startMatch(net.cfg); status.clear(); break;
                case Net::Desync: status = TextFormat("DESYNC at tick %u", e.a); desynced = true; break;
                case Net::Chat: status = e.text; break;
                case Net::Disconnected: status = "Disconnected: " + e.text; reconnectAt = clock + 3; break;
                default: break;
                }
                if (!netbot || e.type == Net::RoomList || e.type == Net::RoomState || e.type == Net::Pong) continue;
                printf("[%s] %s\n", name.c_str(), e.type == Net::Welcome ? "welcome" : e.type == Net::Start ? "start" : status.c_str());
                fflush(stdout);
                if (e.type == Net::Welcome)
                    if (FILE *f = fopen("netbot.token", "w")) fprintf(f, "%llx\n", (unsigned long long)net.token), fclose(f);
            }
            // the token kept in `net` resumes the match: server resends Start + the whole input log
            if (!net.online() && (screen == Screen::Play || screen == Screen::Loading) && clock > reconnectAt) {
                net.connect(connHost.c_str(), connPort, name.c_str());
                reconnectAt = clock + 3;
            }
        }

        if (screen == Screen::Menu) {
            if (intro && frame == 20) front.screen = Ui::Frontend::Main;
            if (intro && frame == 80) front.go(Ui::Frontend::Local);
            if (uiShot && std::count(uiFrames.begin(), uiFrames.end(), frame))
                snprintf(capPath, sizeof capPath, uiShot == flagUi ? DATA_DIR "ui_%d.png" : "ui.png", frame), front.capture = capPath, TraceLog(LOG_INFO, "UI: frame %d", frame);
            if (uiShot && frame > uiFrames.back()) break;
            Ui::Frontend::Action a = front.frame(opt, maps, host, port, name);
            if (a == Ui::Frontend::Quit) break;
            if (a == Ui::Frontend::Replays && saver.joinable()) saver.join();  // the last match's file complete
            if (a == Ui::Frontend::Replays) replayFiles = listReplays(DATA_DIR "replays"), replaySel = 0, screen = Screen::Replays;
            if (a == Ui::Frontend::SinglePlayer) missionMenu.tab = front.missionTab, missionMenu.brief = false, openMissions();
            if (a == Ui::Frontend::QuickMatch) {  // you vs one level-2 CPU team on a random map, Standard scheme; opt untouched
                GameConfig q = opt;
                q.teams = 2, q.wormsPerTeam = 4, q.rules = 0, q.wormpot = 0, q.mission = nullptr, q.scheme = SCHEMES[0].s;
                q.custom.clear();
                q.teamSetup.resize(2);
                q.teamSetup[0].cpu = 0;
                q.teamSetup[1] = {"CPU", 2, 1, 0};
                q.map = maps[GetRandomValue(0, (int)maps.size() - 1)];
                q.seed = (uint32_t)(clock * 1000) + frame;
                online = lan = false;
                startMatch(q);
            }
            if (a == Ui::Frontend::StartLocal) {
                online = false;
                opt.seed = (uint32_t)(clock * 1000) + frame;
                startMatch(opt);
            } else if (a == Ui::Frontend::StartOnline) {
                online = true, lan = false, connHost = host, connPort = port;
                status = net.connect(host.c_str(), port, name.c_str()) ? "Connecting to " + host + "..." : "Cannot reach " + host;
                screen = Screen::Lobby;
            } else if (a == Ui::Frontend::StartLan) {
                online = lan = true;
                net.close();
                status = lanScan.open() ? "" : "Network unavailable";
                screen = Screen::Lobby;
            }
            continue;
        }

        if (screen == Screen::Loading) {
            BeginDrawing();
            bool done = Loading::frame(fixedDt ? dt : GetFrameTime(), loadProgress());  // wall clock, unclamped: in step with the audio, like W4M's
            if (loadShot && std::count(uiFrames.begin(), uiFrames.end(), frame)) {
                rlDrawRenderBatchActive();
                Image img = LoadImageFromScreen();
                ExportImage(img, TextFormat(DATA_DIR "ui_%d.png", frame));
                UnloadImage(img);
            }
            capture(frame);
            EndDrawing();
            if (loadShot && frame >= uiFrames.back()) break;
            if (Loading::ready() && loadStep >= 0) prepStep(false);  // after EndDrawing: the frame shows while this step blocks
            if (done) screen = Screen::Play, mapMusic();
            continue;
        }

        if (screen == Screen::Missions) {
            BeginDrawing();
            Ui::background();
            int pick = Ui::missionMenu(missionMenu, missions, progress);
            if (Ui::helpHeld()) Ui::controls(false);
            if (uiShot && frame == 10) {
                rlDrawRenderBatchActive();
                Image img = LoadImageFromScreen();
                ExportImage(img, "ui.png");
                UnloadImage(img);
            }
            EndDrawing();
            if (uiShot && frame >= 10) break;
            if (pick == -2) screen = Screen::Menu;
            else if (pick >= 0) missionMenu.brief = false, startMission(pick);
            continue;
        }

        if (screen == Screen::Replays) {
            bool was = instant;
            BeginDrawing();
            Ui::background();
            int pick = Ui::replayList(replayFiles, replaySel, instant);
            if (Ui::helpHeld()) Ui::controls(false);
            if (uiShot && frame == 10) {
                rlDrawRenderBatchActive();
                Image img = LoadImageFromScreen();
                ExportImage(img, "ui.png");
                UnloadImage(img);
            }
            EndDrawing();
            if (uiShot && frame >= 10) break;
            if (instant != was) SaveFileText(DATA_DIR "replay.txt", (char *)(instant ? "1\n" : "0\n"));
            if (pick == -2) screen = Screen::Menu;
            else if (pick >= 0 && play.load(DATA_DIR "replays/" + replayFiles[pick])) {
                online = false, playing = true, paused = freeCam = false, speed = 1;
                startMatch(play.cfg);
            } else if (pick >= 0) TraceLog(LOG_WARNING, "unreadable replay %s", replayFiles[pick].c_str());
            continue;
        }

        if (screen == Screen::Lobby) {
            bool inRoom = net.roomId != 0, isHost = inRoom && net.hostId == net.id;
            if (netbot && botStarted && clock > 15) {
                printf("[%s] no match to resume\n", name.c_str());
                desynced = true;
                break;
            }
            if (netbot && !botStarted && net.id && clock > botAt) {  // botStarted: resuming, the server replays the match
                botAt = clock + 0.5f;
                if (!inRoom && botCreate) net.createRoom("netbot", 2);
                else if (!inRoom) net.rooms.empty() ? net.listRooms() : net.joinRoom(net.rooms[0].id);
            }
            if (lan && !net.online()) {  // LAN list: games heard by their beacon, or host one here
                if (!lanHost.running()) lanScan.open();
                lanScan.poll(clock);
                const std::vector<LanGame> &games = lanScan.games;
                int n = (int)games.size();
                lanSel = n ? (lanSel + pressed({GAMEPAD_BUTTON_LEFT_FACE_DOWN}, {KEY_DOWN}) - pressed({GAMEPAD_BUTTON_LEFT_FACE_UP}, {KEY_UP}) + n) % n : 0;
                bool join = n && (netbot ? !botCreate : pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER}));
                if (!netbot && pressed({GAMEPAD_BUTTON_RIGHT_FACE_UP}, {KEY_C})) {
                    if (lanHost.open(port, (name + "'s game").c_str())) connHost = "127.0.0.1", connPort = port;
                    else status = TextFormat("Cannot host: port %d busy", port);
                }
                if (join) connHost = games[lanSel].ip, connPort = games[lanSel].port;
                if (join && netbot) printf("[%s] lan game at %s:%d\n", name.c_str(), connHost.c_str(), connPort);
                if (join || lanHost.running()) {
                    lanScan.close();
                    status = net.connect(connHost.c_str(), connPort, name.c_str()) ? "Connecting to " + connHost + "..." : "Cannot reach " + connHost;
                }
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) { lanScan.close(); online = lan = false; screen = Screen::Menu; }
                BeginDrawing();
                Ui::background();
                Ui::lanGames(games, lanSel, status);
                if (Ui::helpHeld()) Ui::controls(false);
                EndDrawing();
                continue;
            }
            if (!inRoom) {
                int n = (int)net.rooms.size();
                if (n) roomSel = (roomSel + pressed({GAMEPAD_BUTTON_LEFT_FACE_DOWN}, {KEY_DOWN}) - pressed({GAMEPAD_BUTTON_LEFT_FACE_UP}, {KEY_UP}) + n) % n;
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER}) && n) net.joinRoom(net.rooms[roomSel].id);
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_UP}, {KEY_C})) net.createRoom((name + "'s room").c_str(), 4);
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_R})) net.listRooms();
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) {
                    if (lan) leaveLan();
                    else net.close(), online = false, screen = Screen::Menu;
                }
            } else {
                if (isHost && net.players.size() >= 2 && (netbot ? !botStarted : pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER}))) {
                    botStarted = true;
                    GameConfig c = opt;
                    c.seed = (uint32_t)(clock * 1000) + frame;
                    net.startMatch(c);  // one team per console + the host's CPU teams
                }
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) {
                    if (lan) leaveLan();
                    else net.leave(), net.listRooms();
                }
            }
            BeginDrawing();
            Ui::background();
            if (inRoom) Ui::room(net, opt, lan, status);
            else {
                Ui::text(lan ? "JOINING..." : "ONLINE LOBBY", 640, 60, 60, {255, 220, 120, 255}, 1);
                for (size_t i = 0; i < net.rooms.size(); i++) {
                    const NetRoom &r = net.rooms[i];
                    Ui::text(TextFormat("%s %s  (%d/%d)%s", (int)i == roomSel ? ">" : " ", r.name.c_str(), r.players, r.maxPlayers, r.started ? " playing" : ""),
                                     640, 160 + (int)i * 40, 30, (int)i == roomSel ? YELLOW : WHITE, 1);
                }
                if (net.rooms.empty()) Ui::text("No rooms yet", 640, 200, 30, LIGHTGRAY, 1);
                Ui::text(status.c_str(), 640, 660, 22, ORANGE, 1);
            }
            if (Ui::helpHeld()) Ui::controls(false);
            EndDrawing();
            continue;
        }

        mark = GetTime();
        gpuClock.mark(-1);
        game.terrain.remeshWait();  // the meshing thread reads the voxels the sim is about to change
        lap(T_REMESH);
        const Worm &cur = game.worms[game.current];
        int pad = !online && IsGamepadAvailable(cur.team) ? cur.team : 0;
        bool quit;
        if (playing) {  // match playback controls
            if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE})) paused = !paused;
            if (pressed({GAMEPAD_BUTTON_RIGHT_TRIGGER_1}, {KEY_TAB})) speed = speed == 4 ? 1 : speed * 2;
            if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_UP}, {KEY_C})) {
                Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                freeCam = !freeCam, fcPos = cam.position, fcYaw = atan2f(f.x, f.z), fcPitch = asinf(f.y);
            }
            if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_N})) {  // fast-forward to the next turn, unrendered
                for (bool next = false; !next && tick < play.inputs.size();) {
                    game.step(play.inputs[tick++]);
                    for (const GameEvent &e : game.events) next |= e.kind == GameEvent::TurnStart || e.kind == GameEvent::GameOver;
                }
                Fx::clear();
                game.terrain.remesh();
            }
            quit = pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN, GAMEPAD_BUTTON_MIDDLE_RIGHT}, {KEY_ESCAPE, KEY_BACKSPACE});
        } else quit = !shot && pause.update() == Ui::Pause::Quit;
        if (quit) {
            irFinish();
            saveRec();
            playing = false;
            if (lan) leaveLan();  // a host leaving ends the LAN game: it is the server
            else if (online) net.leave(), net.listRooms();
            screen = online ? Screen::Lobby : game.cfg.mission ? Screen::Missions : Screen::Menu;
            Audio::music(true, "theme");
            Audio::stopSfx();
            PollInputEvents();  // no EndDrawing this frame: else the menu sees the same A/Enter press and restarts
            continue;
        }
        for (uint32_t o : net.owners) {  // an owner who left the room counts as offline too
            auto p = std::find_if(net.players.begin(), net.players.end(), [&](const NetPlayer &pl) { return pl.id == o; });
            if (p != net.players.end() && p->online) offlineSince.erase(o);
            else offlineSince.emplace(o, clock);
        }
        // the host plays idle turns for owners gone > 30 s so a dropout can't stall the match
        // ponytail: if the owner reconnects mid-turn both may send the same tick; server keeps the first
        auto proxied = [&](int team) {
            auto it = team < (int)net.owners.size() ? offlineSince.find(net.owners[team]) : offlineSince.end();
            return net.hostId == net.id && it != offlineSince.end() && clock - it->second > 30;
        };
        // the host also plays the CPU teams
        auto owns = [&](int team) { return (cpu(team) && net.hostId == net.id) || (team < (int)net.owners.size() && (net.owners[team] == net.id || proxied(team))); };
        bool remoteTurn = online && game.phase != Phase::GameOver && !owns(cur.team);
        bool padTurn = !shot && !remoteTurn && !pause.open && !playing && irEnd < 0;
        if (aimSeq) Controls::forceAim = frame >= 40 && frame < 90;
        Controls::cpuTurn = cpu(cur.team) && ai.striking() && !playing && irEnd < 0;  // the CPU's own Blimp: only for a planned strike
        Input pin = Controls::read(game, pad, aimShot || aimSeq || utilShot || (padTurn && !cpu(cur.team)), dt);
        if (Controls::fireRefused()) Audio::play(Audio::Sfx::FeError);  // W4M Weapon.NotClearToFire plays weapons/Gong
        static bool wasLocked = false;  // HomingLockOnGraphicEntity::OnTargetSelected 0x560420
        if (game.locked && !wasLocked) Audio::play(Audio::Sfx::LockOn);
        wasLocked = game.locked;
        int late = animShot ? 2 * shotWeapon : 0;  // animshot: fire once the weapon cycling is over
        Input in = shot ? (late && frame >= late ? scriptInput(frame - late, 0, true) : scriptInput(frame, shotBench ? 0 : shotWeapon, !aimShot && !aimSeq && !late)) : pause.open || playing || irEnd >= 0 ? Input{} : pin;
        if (aimSeq && frame > 40 && frame < 80) in.aim = 127;
        if (utilShot && game.girderOn && frame < 38) in.buttons |= Input::TARGET | (frame % 2 ? Input::PITCH : 0), in.walk = 127, in.aim = frame % 2 ? 127 : 0;
        if (utilShot && WEAPONS[game.weapon].kind == Kind::Icarus && frame > 100)  // jump, then flap on each window
            in = Input{}, in.walk = 60, in.buttons = (game.icarus == 1 && frame % 30 == 0) || (game.icarus == 2 && game.clock >= game.flapAt && !(prevShotJump & Input::JUMP)) ? Input::JUMP : 0;
        prevShotJump = in.buttons;
        if (utilShot && (WEAPONS[game.weapon].kind == Kind::Abduction || WEAPONS[game.weapon].kind == Kind::Binoculars)) {  // face the first enemy, FIRE once
            in = Input{}, in.buttons = frame == 40 ? Input::FIRE : 0;
            for (const Worm &e : game.worms)
                if (frame == 2 && e.alive && e.team != cur.team) {
                    Vector3 d = Vector3Subtract(e.pos, cur.pos);
                    game.worms[game.current].yaw = atan2f(d.x, d.z), game.worms[game.current].pitch = asinf(d.y / Vector3Length(d));
                    break;
                }
        }
        if (utilShot && WEAPONS[game.weapon].kind == Kind::Homing && frame >= 60) {  // target view, lock on, then launch
            in = Input{};
            if (Controls::targetView(game)) in.buttons = Input::TARGET | (frame == 61 ? Input::FIRE : 0);
            else if (game.locked) in.buttons = frame % 2 ? Input::FIRE : 0;
        }
        if (aimShot && Controls::targetView(game) && frame > 30)  // Blimp: pan, yaw, then tilt down
            in.buttons |= Input::TARGET | (frame >= 45 ? Input::PITCH : 0), in.walk = frame < 45 ? 127 : 0, in.turn = frame < 45 ? 40 : 0, in.aim = frame >= 45 ? -60 : 0;
        if (aimBench && frame >= 60) in = Input{}, in.buttons = Controls::targetView(game) ? Input::TARGET | (frame == 61 && getenv("W4NX_LOCK") ? Input::FIRE : 0) : 0;
        if (irSwallow) {
            if (in.buttons & (Input::FIRE | Input::JUMP)) in.buttons &= ~(Input::FIRE | Input::JUMP);
            else irSwallow = false;
        }
        hud.input(game, in, padTurn, pad, tick);
        bool feedPad = padTurn && !hud.open;
        auto local = [&] { return feedPad ? Controls::tick(in) : in; };  // stick rates spread over ticks
        livePad = padTurn && !cpu(cur.team) ? pad : -1;
        if (playing) {
            for (acc += paused ? 0 : dt * speed; acc >= Game::DT; acc -= Game::DT) {
                if (tick >= play.inputs.size()) { acc = 0; break; }
                stepOnce(play.inputs[tick]);
            }
        } else if (irEnd >= 0) {  // instant replay: slow motion, A skips
            if (!pause.open && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER})) irFinish();
            for (irAcc += pause.open ? 0 : dt * 0.5f; irAcc >= Game::DT && irEnd >= 0; irAcc -= Game::DT) {
                game.step(rec.inputs[irTick++]), stepped++;
                for (const GameEvent &e : game.events)
                    if (e.kind != GameEvent::GameOver) onEvent(game, e);  // the jingle already played live
                if (irTick == (uint32_t)irEnd) irFinish();
            }
        } else if (!online) {
            // user-requested (2026-10-03): any local key press skips a CPU's hot seat ("Ready")
            bool skipReady = !shot && game.hotSeat > 0 && cpu(game.worms[game.current].team) && (GetGamepadButtonPressed() || GetKeyPressed());
            for (acc += pause.open ? 0 : dt; acc >= Game::DT; acc -= Game::DT) {
                Input in = !shot && cpu(game.worms[game.current].team) ? ai.think(game) : local();
                if (skipReady) in.flags |= Input::CAMERA, skipReady = false;  // any flag ends the hot seat (Game::step)
                stepOnce(in);
            }
        } else {
            // remote/replayed inputs first, then ours when we own the active team; otherwise wait
            acc = netbot ? Game::DT * botSpeed : fminf(acc + dt, Game::DT * 4);
            for (int budget = 240; budget > 0 && !(netbot && turns >= botTurns); budget--) {
                Input r;
                if (net.remoteInput(tick, r)) { stepOnce(r); continue; }
                bool mine = game.phase != Phase::GameOver && tick >= net.replay && owns(game.worms[game.current].team);
                if (!mine || acc < Game::DT) break;
                int team = game.worms[game.current].team;
                bool bot = netbot && team < (int)net.owners.size() && net.owners[team] == net.id;
                Input mineIn = cpu(team) || bot ? ai.think(game) : proxied(team) ? Input{} : local();
                if (netbot && proxied(team) && proxyTurn != turns) printf("[%s] playing turn %d for offline team %d\n", name.c_str(), turns + 1, team), proxyTurn = turns;
                net.sendInput(tick, mineIn);
                stepOnce(mineIn);
                acc -= Game::DT;
            }
        }
        if (shotDone && game.phase == Phase::Aim) shotDone = false;
        bool wait = false;
        if (shotDone && instant && snap.valid && irEnd < 0 && !online && !playing && !shot && !bench) {
            // let the live action settle first (worms landed, nothing flying), capped at 2.5 s
            bool still = !game.shots.empty();
            for (const Worm &w : game.worms) still |= w.alive && (!w.grounded || Vector3LengthSqr(w.vel) > 0.01f);
            for (const Object &o : game.objects) still |= o.falling || Vector3LengthSqr(o.vel) > 0.01f;
            uint32_t since = tick - shotTick;
            bool ready = since >= 150 || game.phase == Phase::GameOver || (since >= 90 && !still);
            int dmg = 0;
            bool kill = false;
            for (size_t i = 0; i < game.worms.size() && i < snap.g.worms.size(); i++) {
                const Worm &a = snap.g.worms[i], &b = game.worms[i];
                if (a.alive) dmg += std::max(0, a.hp - std::max(0, b.hp)), kill |= !b.alive || b.hp <= 0;
            }
            wait = !ready;
            if (ready && (kill || dmg >= 30)) {  // replay from just before the shot, at most the last 8 s
                double t0 = GetTime();
                irLive = game.checksum(), irEnd = (int)tick;
                irTick = std::max({snap.tick, fireTick > 45 ? fireTick - 45 : 0, tick > 480 ? tick - 480 : 0});
                uint32_t from = snap.restore(game, irTick);
                for (uint32_t t = from; t < irTick; t++) game.step(rec.inputs[t]);
                snap.valid = false;
                int built = game.terrain.remesh();  // what the kept meshes do not cover
                TraceLog(LOG_INFO, "REPLAY: %u ticks re-simulated, %d chunks remeshed, %d kept, %.1f ms", irTick - from, built,
                         (int)game.terrain.liveKept.size(), (GetTime() - t0) * 1000);
                Fx::clear();
                irAcc = 0;
            }
        }
        if (!wait) shotDone = false;
        lap(T_SIM);
        int chunks = game.terrain.remeshAsync(0.010);  // the meshing thread starts no chunk past 10 ms, while this frame renders
        lap(T_REMESH);
        // W4M HudClockEntity 0x5efe80 on the displayed seconds (rounded up): ClockFast loops at 5 and under (0x5efd80), ClockSlow from 15
        // at volume min(1, (15 - s) 0.11) (0x5efc40, Event property 1); both stop at 0, past 15 and during EFMV.Active
        int sec = game.phase == Phase::Aim && !game.hotSeat && cur.alive && !game.abducting() && game.timer > 0 ? (game.timer + 59) / 60 : 0;
        Audio::loop(Audio::Sfx::Tick, sec > 0 && sec <= 5 && !pause.open);
        Audio::loop(Audio::Sfx::TickSlow, sec > 5 && sec <= 15 && !pause.open, nullptr, fminf(1, (15 - sec) * 0.11f));
        if (sec > 0 && sec <= 5 && sec != lastSec) Controls::rumble(livePad, 0.12f, 0.05f);
        lastSec = sec;
        if (netbot && botDone > 1e8f && (game.phase == Phase::GameOver || turns >= botTurns)) botDone = clock + 2;  // let the last inputs and sums out
        if (netbot && clock > botDone) break;
        if (game.phase == Phase::GameOver && !pause.open && !playing && irEnd < 0 && !game.cfg.mission && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE})) {
            screen = online ? Screen::Lobby : Screen::Menu;
            Audio::music(true, "theme");
            Audio::stopSfx();
        }

        // camera (controls.cpp): free orbit, first person in aim mode, chasing the projectile, sniper scope
        const WeaponDef &wd = WEAPONS[game.weapon];
        // W4M FEV loops: FuseLoop until the stick blows, MineArmLoop while a mine ticks, HolyGrenadeHeld in hand, the crowd at the end
        const Vector3 *stick = nullptr, *mine = nullptr;
        for (const Projectile &s : game.shots) if (WEAPONS[s.weapon].name == "Dynamite") stick = &s.pos;
        for (const Object &o : game.objects) if (o.type == Object::Mine && o.fuse >= 0 && !o.dead) mine = &o.pos;
        const bool onMatch = screen == Screen::Play;  // the exit above may have left it this frame: stopSfx must stick
        Audio::loop(Audio::Sfx::Dynamite, onMatch && stick && !pause.open, stick);
        // MissileLoop: Bazooka WEAPTWK LoopSfx, Homing via WXP_HomingMissileGlow's EmitterSoundFX; follows the shot, 1 s trigger delay, Time envelope ends at 5.03 s
        static float rocketAge = 0;
        const Vector3 *rocket = nullptr;
        for (const Projectile &s : game.shots)
            if (!s.child && !customWeapon(s.weapon) && (WEAPONS[s.weapon].name == "Bazooka" || WEAPONS[s.weapon].kind == Kind::Homing)) rocket = &s.pos;
        rocketAge = rocket ? rocketAge + GetFrameTime() : 0;
        Audio::hold(Audio::Sfx::Homing, onMatch && rocket && rocketAge < 5.029f && !pause.open, rocket);
        Audio::loop(Audio::Sfx::MineBeep, onMatch && mine && !pause.open, mine);
        Audio::loop(Audio::Sfx::HolyHeld, onMatch && game.phase == Phase::Aim && cur.alive && wd.name == "Holy Hand Grenade" && !pause.open, &cur.pos);
        Audio::loop(Audio::Sfx::Cheer, onMatch && game.phase == Phase::GameOver && won(game));
        // W4M PowerbarMeterEntity 0x5f5c70: PoweringUpStart plays the type's sound on the worm, cut at the launch (0x1a = kWeaponPoisonArrow, table 0x90c920)
        const WeaponDef &sw = WEAPONS[game.weapon];
        bool charging = onMatch && game.phase == Phase::Aim && cur.alive && game.power > 0 && powered(sw.kind) && !pause.open;
        Audio::hold(Audio::Sfx::PowerRocket, charging && sw.kind == Kind::Shell && sw.name != "Poison Arrow", &cur.pos);
        Audio::hold(Audio::Sfx::PowerHoming, charging && sw.kind == Kind::Homing, &cur.pos);
        // Accessory.Init = the weapon shown in hand (game.weapon): EquipSfx at most once per 9 s per worm (+0xd4)
        static int equipW = -1, equipWorm = -1;
        static double equipAt[256];
        bool aiming = onMatch && game.phase == Phase::Aim && cur.alive, hand = aiming && !pause.open;
        if (aiming && (game.weapon != equipW || game.current != equipWorm) && (unsigned)game.current < 256 && GetTime() > equipAt[game.current] + 9)
            Audio::equip(customWeapon(game.weapon) ? "Bazooka" : wd.name.c_str(), cur.pos), equipAt[game.current] = GetTime();
        equipW = aiming ? game.weapon : -1, equipWorm = aiming ? game.current : -1;
        // WAE_* Input.TauntPressed (DIK 20, no joypad), state 1 -> 2: the Acting taunt scene plus the WXAnimTaunt clip until it ends.
        // It needs kWPS_Ambulatory (WormData+0xf0 == 0) and no PlayFireAnim since Accessory.Init (+0xc5, 0x58c333): not between shotgun shots
        static double tauntEnd[256];
        bool ambulatory = cur.grounded && !cur.motion.slide && game.vault.t <= 0;
        if (hand && ambulatory && !game.shotsLeft && IsKeyPressed(KEY_T) && (unsigned)game.current < 256 && (size_t)game.current < wormAnims.size() && GetTime() > tauntEnd[game.current]) {
            Acting::taunt(game, game.current, wd.name);
            const char *tc = tauntClip(wd), *hc = nullptr, *hm = heldModel(wd, &hc);
            if (float len = tc ? Models::clipLength("worm", tc) : 0)
                wormAnims[game.current].act = {tc, 0, hm && hm[0] ? hm : nullptr, nullptr}, tauntEnd[game.current] = GetTime() + len;
        }
        Audio::hold(Audio::Sfx::HeldSheep, hand && wd.name == "Sheep", &cur.pos);
        Audio::loop(Audio::Sfx::HeldSentry, hand && wd.name == "Sentry Gun", &cur.pos);
        Audio::hold(Audio::Sfx::HeldScouser, hand && wd.name == "Inflatable Scouser", &cur.pos);
        Audio::hold(Audio::Sfx::HeldOldWoman, hand && wd.name == "Old Woman", &cur.pos);
        Audio::hold(Audio::Sfx::PowerBow, charging && sw.name == "Poison Arrow", &cur.pos);
        bool chase = game.phase != Phase::Aim && !game.shots.empty() && !dropped(WEAPONS[game.shots[0].weapon]);  // dynamite: the camera stays on the worm
        Controls::Reticle ret = Controls::reticle(game, chase);
        bool scope = !chase && Controls::scoped(game), fp = ret == Controls::Reticle::Aim;  // W4M first-person aim
        static float minusT = 0;  // - held 1.5 s: perf overlay (user-requested, 2026-10-04); the controls help shows from 0.35 s
        float minusWas = minusT;
        minusT = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_LEFT) ? minusT + dt : 0;
        if ((minusWas < 1.5f && minusT >= 1.5f) || IsKeyPressed(KEY_F3))
            perfOn = (perfOn + 1) % 3;
        Controls::camera(cam, game, chase, scope, !pause.open && !hud.open && !(playing && freeCam), dt);
        updateBomber(game, pause.open ? 0 : dt);
        updateUfo(game, pause.open ? 0 : dt);
        if (Camera3D sc = cam; !(playing && freeCam) && (bomberCam(&sc) || ufoCam(&sc))) cam = sc;  // W4M scene cam outranks every logical camera
        Audio::listen(cam);
        bool inside = Vector3Distance(cam.position, cur.pos) < 1.2f;  // the aim camera has flown into the worm
        hud.fp = fp || Controls::sinceFirstPerson() < 0.3f;
        Camera3D view = cam;  // shaken copy: the smoothed camera itself never drifts
        if (fixedView) view = viewCam;
        if (getenv("W4NX_UFOVIEW") && ufo.live) view.target = Vector3Transform({0, 0, 0}, ufo.at), view.position = Vector3Add(view.target, {30, -6, 30}), view.up = {0, 1, 0};  // captures: the saucer from outside
        if (animShot) {  // side view of the shooter
            static int who = game.current;
            const Worm &s = game.worms[who];
            view.target = Vector3Add(s.pos, {0, 0.3f, 0}), view.fovy = 45;
            view.position = Vector3Add(view.target, {cosf(s.yaw) * 5, 1, -sinf(s.yaw) * 5});
        }
        if (playing && freeCam) {  // LS / arrows move, RS / A D W S look, ZL ZR / Z X down up
            auto ax = [&](int a) { float v = GetGamepadAxisMovement(pad, a); return fabsf(v) < 0.2f ? 0.0f : v; };
#ifdef __SWITCH__
            const float up = 1;
#else
            const float up = -1;
#endif
            fcYaw -= (ax(GAMEPAD_AXIS_RIGHT_X) + IsKeyDown(KEY_D) - IsKeyDown(KEY_A)) * dt * 2;
            fcPitch = Clamp(fcPitch + (up * ax(GAMEPAD_AXIS_RIGHT_Y) + IsKeyDown(KEY_W) - IsKeyDown(KEY_S)) * dt * 1.5f, -1.4f, 1.4f);
            Vector3 f = {sinf(fcYaw) * cosf(fcPitch), sinf(fcPitch), cosf(fcYaw) * cosf(fcPitch)}, right = {-cosf(fcYaw), 0, sinf(fcYaw)};
            float mv = up * ax(GAMEPAD_AXIS_LEFT_Y) + IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN), st = ax(GAMEPAD_AXIS_LEFT_X) + IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT);
            float lift = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) + IsKeyDown(KEY_X) - IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_2) - IsKeyDown(KEY_Z);
            fcPos = Vector3Add(fcPos, Vector3Scale(Vector3Add(Vector3Add(Vector3Scale(f, mv), Vector3Scale(right, st)), {0, lift, 0}), dt * 15));
            view.position = fcPos, view.target = Vector3Add(fcPos, f), view.fovy = Controls::FOV0;
        }
        Vector3 jolt = Vector3Scale({sinf(clock * 53), sinf(clock * 61 + 1) * 0.7f, sinf(clock * 47 + 2)}, Fx::shake);
        view.position = Vector3Add(view.position, jolt), view.target = Vector3Add(view.target, jolt), view.up = Controls::viewUp(view);
        lap(T_CAMERA);
        animateWorms(game, dt, view);
        // W4M lighting pass: land and still decor cached until remeshed; clip-played decor, worms, graves, shots and objects every frame
        Lit::shadowPass(game.terrain.bounds, Terrain::meshVer, [&] { game.terrain.drawShadow(), game.terrain.drawObjects(0, true, 1); }, [&](std::vector<Vector4> &at) {
            game.terrain.drawObjects(clock, true, 2, &at);
            for (const Worm &w : game.worms)
                if (Models::visible(w.pos, 2)) (w.alive || w.counted > 0) ? (void)drawWorm(game, w, clock) : drawGrave(game, w), at.push_back({w.pos.x, w.pos.y, w.pos.z, 2});
            for (const Projectile &s : game.shots) drawShot(s, clock, game.terrain), at.push_back({s.pos.x, s.pos.y, s.pos.z, 3});
            for (const Object &o : game.objects)
                if (Models::visible(o.pos, 2)) Models::draw(objectModel(o), o.pos, (&o - game.objects.data()) * 1.3f), at.push_back({o.pos.x, o.pos.y, o.pos.z, 2});
        });
        lap(T_SHADOW);
        // W4M PiP: the event camera in one low-resolution pass (terrain, worms, shots, objects, water), before the main view
        static RenderTexture2D pipRt = {};
        Camera3D pipView;
        float pipShow = 0, pipFull = 0;
        bool pipOn = !fixedView && !animShot && Controls::inset(pipView, pipShow, pipFull);
        if (!pipRt.id) pipRt = LoadRenderTexture(320, 240);  // on the match's first frame, not the first PiP event's
        if (pipOn) {
            BeginTextureMode(pipRt);
            ClearBackground(Fx::fog());
            BeginMode3D(pipView);
            Fx::drawSky(pipView, game.terrain.origin, game.terrain.scale / 20);
            game.terrain.setView(pipView.position);
            game.terrain.draw();
            for (const Worm &w : game.worms) if ((w.alive || w.counted > 0) && Models::visible(w.pos, 2)) drawWorm(game, w, clock);
            for (const Projectile &s : game.shots) if (!drawShot(s, clock, game.terrain)) DrawSphere(s.pos, 0.2f, DARKGRAY);
            for (const Object &o : game.objects)
                if (Models::visible(o.pos, 2)) Models::draw(objectModel(o), o.pos, (&o - game.objects.data()) * 1.3f);
            Fx::drawWater(pipView, game.water, clock, 12000 * game.terrain.scale / 20, {game.terrain.origin.x, game.terrain.origin.z});
            game.terrain.drawFringe();
            Fx::draw(pipView);
            EndMode3D();
            EndTextureMode();
        }
        lap(T_PIP);

        BeginDrawing();
        ClearBackground(Fx::fog());
        BeginMode3D(view);
        Fx::drawSky(view, game.terrain.origin, game.terrain.scale / 20);
        lap(T_SKY);
        game.terrain.setView(view.position);
        game.terrain.draw();
        lap(T_TERRAIN);
        game.terrain.drawObjects(clock);
        lap(T_DECOR);
        toolFx(game, dt);
        jetAudio(game);
        Xray::begin();
        bool blimp = (playing && freeCam) || Controls::targetView(game) || (wd.kind == Kind::Binoculars && Controls::firstPerson(game));  // W4M outlines every live worm in the blimp view and binoculars (camera-w4m.md section 5)
        auto shown = [&](const Worm &w) { return (w.alive || w.counted > 0) && !(inside && &w == &cur) && Models::visible(w.pos, 2); };
        auto outlined = [&](const Worm &w) { return Models::has("worm") && shown(w) && (blimp || &w == &cur); };  // W4M OutlinedWorms1: the active worm only, outside the blimp view
        bool masked = false;  // every outlined worm's depth in one render-target bind
        for (const Worm &w : game.worms)
            if (outlined(w) && (masked || (masked = Xray::mask()))) Xray::team(w.team), drawWorm(game, w, clock);
        if (masked) Xray::unmask();
        for (const Worm &w : game.worms) {
            if (!w.alive && w.counted <= 0) { drawGrave(game, w); continue; }
            if (!shown(w)) continue;
            if (Models::has("worm")) {  // the passes below draw the same pose: no re-skin
                bool bin = masked && outlined(w);
                float op = &w == &cur && !blimp ? Xray::opacity(view.position, w.pos) : 1;
                if (op > 0 && op < 1) Xray::depth(), drawWorm(game, w, clock), Xray::fade(op);
                if (op > 0) drawWorm(game, w, clock);
                Xray::done();
                if (bin) Xray::hidden(), drawWorm(game, w, clock), Xray::done();
            } else {
                Vector3 f = flat(w.yaw), side = {f.z, 0, -f.x};
                DrawCapsule({w.pos.x, w.pos.y - 0.2f, w.pos.z}, {w.pos.x, w.pos.y + 0.3f, w.pos.z}, 0.35f, 8, 6, TEAM_COLORS[w.team]);
                for (float s : {-0.13f, 0.13f})
                    DrawSphere(Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 0.3f), Vector3Add(Vector3Scale(side, s), {0, 0.4f, 0}))), 0.1f, WHITE);
            }
            if ((game.cfg.rules & RULE_KING) && int(&w - game.worms.data()) % game.perTeam == 0)
                DrawCylinderEx({w.pos.x, w.pos.y + 0.55f, w.pos.z}, {w.pos.x, w.pos.y + 0.8f, w.pos.z}, 0.28f, 0.08f, 6, GOLD);
        }
        if (game.roped)
            for (int k = 0; k < game.rope.n; k++) DrawLine3D(game.rope.pt[k], k + 1 < game.rope.n ? game.rope.pt[k + 1] : cur.pos, BROWN);
        if (game.grapple.on) DrawLine3D(game.grapple.at, cur.pos, BROWN);  // the hook in flight (rope mode 2)
        drawUtilities(game);
        drawBomber(pause.open ? 0 : dt);
        drawUfo(pause.open ? 0 : dt);
        for (const Projectile &s : game.shots) {
            const WeaponDef &d = WEAPONS[s.weapon];
            if (drawShot(s, clock, game.terrain)) continue;
            Vector3 f = Vector3Normalize({s.vel.x, 0, s.vel.z});
            switch (d.kind) {
            case Kind::Sheep:
                DrawCube(s.pos, 0.5f, 0.4f, 0.5f, WHITE);
                DrawCube(Vector3Add(s.pos, {f.x * 0.3f, 0.15f, f.z * 0.3f}), 0.22f, 0.22f, 0.22f, BLACK);
                break;
            case Kind::Donkey: DrawCube(s.pos, 1.0f, 1.4f, 0.7f, GRAY); break;
            case Kind::Airstrike: DrawCylinderEx(s.pos, Vector3Add(s.pos, Vector3Scale(Vector3Normalize(s.vel), -1)), 0.15f, 0.15f, 6, MAROON); break;
            default:
                DrawSphere(s.pos, s.child ? 0.15f : 0.2f, s.child ? ORANGE : d.radius >= 5 ? GOLD : d.clusters ? YELLOW : d.fuse > 0 ? DARKGREEN : DARKGRAY);
            }
        }
        dudFx(game.objects);
        for (const Object &o : game.objects) {
            const char *m = objectModel(o);
            if (o.type == Object::Crate) crateChute(o, &o - game.objects.data(), pause.open ? 0 : dt);
            if (!Models::visible(Vector3Add(o.pos, {0, 1, 0}), 2.5f)) continue;  // incl. the parachute
            if (!Models::draw(m, o.pos, (&o - game.objects.data()) * 1.3f)) {
                if (o.type == Object::Mine) DrawCylinder({o.pos.x, o.pos.y - 0.1f, o.pos.z}, 0.15f, 0.2f, 0.2f, 8, DARKGRAY);
                else if (o.type == Object::Barrel) DrawCylinder({o.pos.x, o.pos.y - 0.5f, o.pos.z}, 0.35f, 0.35f, 1, 10, MAROON);
                else if (o.type == Object::Target) {  // bullseye facing the active worm
                    Vector3 f = Vector3Normalize({cur.pos.x - o.pos.x, 0, cur.pos.z - o.pos.z});
                    for (int k = 0; k < 3; k++)
                        DrawCylinderEx(Vector3Add(o.pos, Vector3Scale(f, 0.02f * k)), Vector3Add(o.pos, Vector3Scale(f, 0.02f * k + 0.06f)), 0.5f - 0.15f * k, 0.5f - 0.15f * k, 16, k % 2 ? WHITE : RED);
                    DrawCylinderEx(Vector3Add(o.pos, {0, -0.5f, 0}), Vector3Add(o.pos, {0, -1.2f, 0}), 0.05f, 0.05f, 6, BROWN);
                }
                else DrawCube(o.pos, 0.8f, 0.8f, 0.8f, o.weapon < 0 ? RAYWHITE : BROWN);
            }
            if (o.type == Object::Mine && o.fuse >= 0 && fmodf(clock, 0.3f) < 0.15f) DrawSphere(Vector3Add(o.pos, {0, 0.15f, 0}), 0.08f, RED);
        }
        if (game.cfg.mission)  // reach objectives: a gold beacon
            for (const MissionSpec::Goal &g : game.cfg.mission->objectives) {
                if (g.type != MissionSpec::Goal::Reach) continue;
                Vector3 p = placeOf(game, g.at);
                DrawCylinderEx(p, Vector3Add(p, {0, 10, 0}), 0.15f, 0.15f, 8, Fade(GOLD, 0.5f));
                DrawCircle3D(Vector3Add(p, {0, 0.05f, 0}), g.radius, {1, 0, 0}, 90, GOLD);
            }
        if (game.cfg.rules & RULE_ROPE_RACE) {
            DrawCylinderEx(game.raceFinish, Vector3Add(game.raceFinish, {0, 10, 0}), 0.15f, 0.15f, 8, Fade(GOLD, 0.5f));
            DrawCube(Vector3Add(game.raceFinish, {0, 10.3f, 0}), 1.2f, 0.6f, 0.08f, RED);
        }
        lap(T_MODELS);
        Fx::drawWater(view, game.water, clock, 12000 * game.terrain.scale / 20, {game.terrain.origin.x, game.terrain.origin.z});
        game.terrain.drawFringe();
        lap(T_SKY);
        float fxDt = pause.open ? 0 : dt;
        for (const Projectile &s : game.shots) Fx::trail(s, fxDt, {game.wind, 0, game.windZ});
        for (const Game::Gas &c : game.gas)  // W4M WXP_GasCloudDelayed: green puffs over the cloud, ~4 a second
            if (fxDt > 0 && GetRandomValue(0, 14) == 0)
                Fx::puff(Vector3Add(c.pos, {GetRandomValue(-40, 40) / 10.0f, GetRandomValue(0, 15) / 10.0f, GetRandomValue(-40, 40) / 10.0f}), {game.wind, 0.2f, game.windZ}, 5, 2, 3.5f, {120, 200, 60, 110});
        drawBubbles(game, fxDt);
        Fx::setWind({game.wind, 0, game.windZ}), Fx::levelSync(game.terrain.emitters), Fx::weather(game.cfg.seed, game.clock);
        Fx::update(fxDt);
        Fx::draw(view);
        Fx::drawFlare(view, fxDt, [&](Vector3 o, Vector3 d) {
            Vector3 at;
            if (game.terrain.raycast({o, d}, 1000, &at)) return 1;
            auto near = [&](Vector3 c) { Vector3 q = Vector3Subtract(c, o); float t = Vector3DotProduct(q, d); return t > 1 && Vector3LengthSqr(q) - t * t < Game::R * Game::R; };  // W4M: line from 20 units out
            for (const Worm &w : game.worms) if (w.alive && near(w.pos)) return 2;
            for (const Object &b : game.objects) if (!b.dead && near(b.pos)) return 2;
            return 0;
        });
        lap(T_FX);
        EndMode3D();
        if (blimp) Xray::outline(TEAM_COLORS);
        if (fp && inside && !scope && (wd.kind == Kind::Shell || wd.kind == Kind::Homing || wd.kind == Kind::Shotgun)) {  // held weapon over the scene with its own near plane, never clipped by it or the terrain
            glClear(0x100);  // GL_DEPTH_BUFFER_BIT
            rlSetClipPlanes(0.05, 50);
            BeginMode3D(view);
            drawWorm(game, cur, clock, &view);
            EndMode3D();
            rlSetClipPlanes(0.5, 500);
        }

        // HomingLockOnGraphicEntity: on the target until the shot, when in front of the camera
        Vector2 lockAt = GetWorldToScreen(game.lockAt, view);
        bool locked = game.locked && wd.kind == Kind::Homing && Vector3DotProduct(Vector3Subtract(game.lockAt, view.position), Vector3Subtract(view.target, view.position)) > 0;
        if (ret == Controls::Reticle::Aim && wd.kind == Kind::Homing) {  // W4M Homing.Cursor in the aim view (observed), not the shell reticle
            Vector2 at = GetWorldToScreen(Controls::aimPoint(game), view);
            Ui::targetCursor(wd, game.target().y <= game.water + 1e-3f ? 1 : 0, locked ? &lockAt : nullptr, &at);
        } else if (ret == Controls::Reticle::Aim) Ui::reticle(wd, GetWorldToScreen(Controls::aimPoint(game), view), scope);  // W4M per-weapon aim reticle
        else if (ret == Controls::Reticle::Blimp) {
            Vector3 h;
            bool hit = !game.cursorOn || game.blimpHit(&h);
            Ui::targetCursor(wd, !hit ? 2 : game.target().y <= game.water + 1e-3f ? 1 : 0, locked ? &lockAt : nullptr);
        } else if (ret == Controls::Reticle::Lock && locked) Ui::targetCursor(wd, -1, &lockAt);
        if (pipOn) Ui::pipInset(pipRt, pipShow, pipFull);
        hud.pipShow = pipOn ? pipShow : 0, hud.pipFull = pipFull;
        hud.quiet = pause.open || playing || irEnd >= 0;  // those draw their own hints
        hud.draw(game, view, tick);
        if (const MissionSpec *ms = game.cfg.mission; ms && game.phase != Phase::GameOver) Ui::missionHud(game, *ms);
        else if (ms && !pause.open && missionIdx >= 0) {
            if (!missionSaved && !uiShot) progress.record(ms->id, game.run.result > 0, game.run.ticks), progress.save(DATA_DIR "progress.txt");
            missionSaved = true;
            int next = missionIdx + 1;
            bool more = next < (int)missions.size() && missions[next].kind == ms->kind && missions[next].campaign == ms->campaign && progress.unlocked(missions, next);
            missionAct = Ui::missionEnd(game, *ms, progress.get(ms->id), more);
        }
        pause.draw(online);
        if (playing) {
            bool end = tick >= play.inputs.size();
            Ui::playbackBar(paused, speed, freeCam, tick * Game::DT, play.inputs.size() * Game::DT,
                            !end ? "" : play.checksum && game.checksum() != play.checksum ? "Replay out of sync (other game version?)" : "End of replay");
        } else if (irEnd >= 0) Ui::replayBadge();
        if (remoteTurn) Ui::text("Remote player's turn", 640, 90, 24, WHITE, 1);
        if (online && !status.empty()) Ui::text(status.c_str(), 640, 120, 24, ORANGE, 1);
        Loading::overlay(dt);
        if (Ui::helpHeld()) Ui::controls(true);
        if (perfOn && !bench) {  // bench: keep the overlay out of the measured ui cost
            int tris = 0;
            for (const auto &ps : game.terrain.parts)
                for (const Terrain::Part &p : ps) tris += p.mesh.triangleCount;
            DrawRectangle(6, 34, 270, 40 + T_COUNT * 20, Fade(BLACK, 0.6f));
            double total = 0;
            for (double c : perf) total += c;
            Ui::text(TextFormat("%d fps  %.2f ms %s", GetFPS(), total * 1000, perfOn == 2 ? "gpu sync" : "cpu"), 14, 38, 20, YELLOW);
            for (int k = 0; k < T_COUNT; k++) {
                Ui::text(NAMES[k], 14, 60 + k * 20, 18, WHITE);
                Ui::text(TextFormat("%.2f", perf[k] * 1000), 200, 60 + k * 20, 18, WHITE, 2);
            }
            Ui::text(TextFormat("terrain %dk tris  fx %d", tris / 1000, Fx::count()), 14, 62 + T_COUNT * 20, 18, LIGHTGRAY);
        } else DrawFPS(10, 10);
        lap(T_UI);
        if (shot && !shotBench && (frame == 35 || frame == 150)) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, frame == 35 ? DATA_DIR "shot_aim.png" : DATA_DIR "shot.png");
            UnloadImage(img);
        }
        if (aimShot && !aimBench && frame == 60) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, "aim.png");
            UnloadImage(img);
        }
        if (animShot && frame >= 40 + 2 * shotWeapon && frame % 6 == 4) {
            if (frame < 46 + 2 * shotWeapon) printf("animshot: %s\n", wd.name.c_str());
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, TextFormat("anim_%03d.png", frame));
            UnloadImage(img);
        }
        if (aimSeq && ((frame >= 38 && frame < 56) || (frame >= 88 && frame < 106))) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, TextFormat("aimseq_%03d.png", frame));
            UnloadImage(img);
        }
        if (uiShot && (loadShot ? std::count(uiFrames.begin(), uiFrames.end(), frame) > 0 : frame == 40)) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, loadShot ? TextFormat(DATA_DIR "ui_%d.png", frame) : "ui.png");
            UnloadImage(img);
        }
        capture(frame);
        EndDrawing();
        lap(T_PRESENT);
        double gpuMs[T_COUNT];
        bool gpuDone = gpuClock.next(gpuMs, T_COUNT);
        if (gpuDone) pace.gpu(gpuMs, T_PRESENT);  // the swap's span is mostly the vblank wait
        if (paceFrame == frame - 1) pace.frame((mark - paceAt) * 1000, stepped, chunks, cost, NAMES, T_COUNT);
        paceFrame = frame, paceAt = mark, stepped = 0;
        if (missionAct) {  // end-of-mission choice, outside the frame: next, retry, back to the list
            int act = missionAct;
            missionAct = 0;
            if (act == 3) screen = Screen::Missions, Audio::music(true, "theme");
            else startMission(act == 1 ? missionIdx + 1 : missionIdx);
            continue;
        }
        const int warm = aimBench ? 70 : 10;  // skip load / first-use frames
        if (bench && frame == warm) benchStart = GetTime();
        if (bench && frame > warm) {
            frameMax = fmax(frameMax, GetTime() - frameStart), frameSq += (GetTime() - frameStart) * (GetTime() - frameStart);
            benchDraws += glDraws, benchBinds += glBinds;
            for (int k = 0; k < T_COUNT; k++) benchSum[k] += cost[k], benchCpu[k] += cpuCost[k], benchMax[k] = fmax(benchMax[k], cost[k]);
            for (int k = 0; k < T_COUNT && gpuDone; k++) benchGpu[k] += gpuMs[k];
            benchGpuN += gpuDone;
        }
        frameStart = GetTime();
        glDraws = glBinds = 0;
        for (int k = 0; k < T_COUNT; k++) perf[k] = perf[k] * 0.95 + cost[k] * 0.05, cost[k] = 0, cpuCost[k] = 0;
        if (bench && frame == warm + benchFrames) {
            double n = benchFrames, wall = GetTime() - benchStart, cpu = 0, gpu = 0;
            printf("BENCH map=%s frames=%d  %.1f fps (%s)  frame avg %.2f ms sd %.2f max %.2f ms\n", opt.map.empty() ? "(procedural)" : opt.map.c_str(), benchFrames, n / wall, perfOn == 2 ? "gpu-synced" : "no sync",
                   wall / n * 1000, sqrt(fmax(0, frameSq / n - wall / n * wall / n)) * 1000, frameMax * 1000);
            printf("%-13s %8s %8s %8s %8s %8s\n", "section", "cpu", "sync", "gpu", "syncmax", "timer");
            double timer = 0;
            for (int k = 0; k < T_COUNT; k++) {
                double c = benchCpu[k] / n * 1000, s = benchSum[k] / n * 1000, g = benchGpuN ? benchGpu[k] / benchGpuN : 0;
                cpu += c, gpu += s - c, timer += k == T_PRESENT ? 0 : g;
                printf("%-13s %8.3f %8.3f %8.3f %8.2f %8.3f\n", NAMES[k], c, s, s - c, benchMax[k] * 1000, g);
            }
            gpu -= (benchSum[T_PRESENT] - benchCpu[T_PRESENT]) / n * 1000;  // vsync wait, not GPU work
            // Switch handheld 720p, fitted on sw-log3 (docs/tests.md "Render budget"): 7x cpu, 14x the timer GPU of a nosync run
            // (glFinish idles the GPU between sections: synced timer and gpu read 3-5x high, 3x synced gpu as a fallback)
            bool timed = timer > 0 && perfOn != 2;
            double sw = fmax(7 * cpu, timed ? 14 * timer : 3 * gpu);
            printf("total cpu %.2f ms gpu %.2f (synced) %.2f (timer) ms -> Switch handheld ~%.1f ms = max(7x cpu, %s) = %.0f fps\n", cpu, gpu, timer, sw,
                   timed ? "14x timer" : "3x synced gpu", 1000 / sw);
            int tris = 0, parts = 0;
            for (const auto &ps : game.terrain.parts)
                for (const Terrain::Part &p : ps) tris += p.mesh.triangleCount, parts++;
            printf("terrain %d tris %d meshes, %d decor, phase %d turn tick %u\n", tris, parts, (int)game.terrain.objects.size(), (int)game.phase, tick);
            printf("per frame: %.0f draw calls, %.1f framebuffer binds\n", benchDraws / n, benchBinds / n);
            fflush(stdout);
            break;
        }
        if ((shot && !shotBench && frame == (getenv("W4NX_SHOTEND") ? atoi(getenv("W4NX_SHOTEND")) : 150) + (animShot ? 2 * shotWeapon + 30 : 0)) || (aimShot && !aimBench && frame == 60) || (aimSeq && frame == 106) || (uiShot && frame == (loadShot ? uiFrames.back() : strcmp(uiShot, "gameover") ? 40 : 600))) break;
    }
    if (loader.joinable()) loader.join();
    if (screen == Screen::Play) irFinish(), saveRec();
    if (saver.joinable()) saver.join();
    net.close();
    game.terrain.unload();
    Models::unload();
    Ui::unload();
    Fx::unload();
    Audio::shutdown();
    CloseWindow();
    if (netbot) printf("[%s] done: %d turns, %s\n", name.c_str(), turns, desynced ? "FAILED" : "in sync");
    return desynced;
}
