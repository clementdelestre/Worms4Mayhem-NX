#include "acting.h"
#include "audio.h"
#include "controls.h"
#include "fx.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __SWITCH__
#define ACTING_FILE "sdmc:/switch/worms4nx/assets/acting.txt"
#else
#define ACTING_FILE "./assets/acting.txt"
#endif

namespace {
using V = Audio::Voice;
constexpr float UNIT = 1 / 20.0f;  // m per W4M unit (sim.h: STEP_UP 1 m = the 20-unit worm)
constexpr float MS = 1e-3f;

// Acting.Trigger indices: W4M name table 0x9214f0
enum Trig { PAYLOAD5, PAYLOAD4, PAYLOAD3, PAYLOAD2, PAYLOAD1, BLAST_SPLAT, FALL_SPLAT, IDLE, SICK, ABDUCTED, DAMAGE, DAMAGE_SILENT,
            BORING, MISTAKE, DEATH, COLLECT, SHORT_ON_TIME, SKIP_GO, PUNCH, CRATE_DROP, WEAPON_FIRED, FIRST_BLOOD, MAX_DAMAGE,
            START_TURN, WAITING, AIRSTRIKE, BLASTED, WORM_BOUNCE, FIRE_DAMAGE, REVENGE, MISSED, VICTORY, TARGETED, POISONED, ZAP,
            TAUNT_MELEE, TAUNT_RANGED, TAUNT_STRIKE, TITTER, GRENADE5, GRENADE4, GRENADE3, GRENADE2, GRENADE1, ITEM_REACT, BORED,
            RETREAT, THINKING, TRIGGERS };
const char *const TRIG_NAMES[TRIGGERS] = {
    "TimedPayloadFive", "TimedPayloadFour", "TimedPayloadThree", "TimedPayloadTwo", "TimedPayloadOne", "BlastSplat", "FallSplat",
    "Idle", "Sick", "Abducted", "DamageInflicted", "DamageSilent", "Boring", "Mistake", "Death", "Collect", "ShortOnTime", "SkipGo",
    "Punch", "CrateDrop", "WeaponFired", "FirstBlood", "MaxDamage", "StartTurn", "Waiting", "Airstrike", "Blasted", "WormBounce",
    "FireDamage", "Revenge", "Missed", "Victory", "Targeted", "Poisoned", "Zap", "TauntMelee", "TauntRanged", "TauntStrike", "Titter",
    "GrenadeFive", "GrenadeFour", "GrenadeThree", "GrenadeTwo", "GrenadeOne", "ItemReact", "Bored", "Retreat", "Thinking"};
// Track criteria: token table 0x921498, bit = 1 << index (parser 0x60d640)
enum : uint32_t { CRIT = 1, FRIEND = 2, FOE = 4, SICK_T = 8, ABDUCTED_T = 0x10, SEE = 0x20, BLIND = 0x40, NEAR = 0x80, SPECIAL = 0x100,
                  PAYLOAD = 0x200, IN_FRONT = 0x400, BEHIND = 0x800, ACTIVE = 0x1000, ON_SCREEN = 0x2000, IDLE_T = 0x4000,
                  TARGETED_T = 0x8000, INTERESTING = 0x10000, SAFE = 0x20000, THREAT = 0x40000, GOODIES = 0x80000, DISTRACTION = 0x100000,
                  LOS = 0x200000 };
const char *const TOKENS[22] = {"crit", "friend", "foe", "sick", "abducted", "see", "blind", "near", "special", "payload", "infront",
                                "behind", "active", "onscreen", "idle", "targeted", "interesting", "safe", "threat", "goodies", "distraction", "los"};
constexpr int NONE = 127, CAMERA = 126, STOP = 125;  // W4M actor ids 0x7f (none), 0x7e (track -1: the camera, assumed), 0x7d
const float SEE_COS = cosf(1.22173f);            // 0x60c4a3: See / Blind / InFront cone, 70 degrees

struct Ev { int ms; char op; std::string arg; int n; };
struct TrackDef { uint32_t flags = 0; int loyal = 0, see = 0, nearT = 0, blind = 0, rel = 0; float nearR = 20; std::vector<Ev> ev; };
struct SceneDef { std::string name; int n = 0; std::vector<TrackDef> tracks; };
std::vector<SceneDef> defs;
std::vector<int> lists[TRIGGERS];  // scene indices, highest N first (0x60d5e0)
int turns[TRIGGERS];               // equal-N groups rotate after each trigger (0x60c410)
bool loaded = false;

struct Prop { Vector3 pos; int type; bool shot; };  // W4M actor types: 2 payload / threat, 3 goodies (crate), 5 other detail
std::vector<Prop> props;

struct Run { int scene = -1; float t = 0; int cast[30]; Vector3 at[30]; std::vector<size_t> cur; };
std::vector<Run> runs;

struct Actor {
    int run = -1, track = -1;
    const char *dflt = "Angry";  // 0x5a595d: Angry or Frown, by a coin flip
    std::string emote;  // "": dflt
    // acting clips (WormPoseManager +0xf4 new / +0xf8 old), weights +0xfc / +0x100, fade-out per update +0x104, ground factor +0x194
    struct Gest { std::string clip; float t = 0, w = 0; } act[2];
    float actS = 1, stopRate = 0;
    int look = NONE, gest = NONE; Vector3 lookAt{}, gestAt{};
    // WormPoseManager (0x59da40), radians: head (+0x134/8), eyes (+0x154/8), gesture (+0x160/+0x15c); old: their snapshot (0x59bb90)
    struct Pose { float hy = 0, hp = 0, ey = 0, ep = 0, gy = 0, gp = 0; } cur, old;
    float headY = 0, headP = 0;                    // head target (+0x128 / +0x130)
    float poseT = 1, headW = 1, eyeW = 1, lookW = 1;  // PoseBlend time (s), head and eye weights (+0x48 / +0x4c), Forbid Lookaround (+0x198)
    float eyeMove = 10, eyeOld = 10, mode = 0;     // PermittedEyeMovement now / before (+0x16c / +0x170; degrees, default 10), head mode
    float coy = 0, coyOff = 0;                     // the emote's Coyness (+0x188, radians), the head's offset from it (+0x18c)
    bool threatened = false, abducted = false, targeted = false, onScreen = false;
    float aimMs = 0, coolMs = 0, calm = 0, sickW = 0, abdW = 0;
    bool kicked = false, flying = false, air = false, dead = false;
    int poison = 0, hp0 = 0;
    Vector3 last{};
    struct Fx { std::string name; float t; } fx[3]; int nfx = 0;
};
std::vector<Actor> actors;
std::vector<Vector3> rested;  // timed payloads already announced (0x577158 flag)
int lastClock = 0, ambientIdx = 0, lastSec = -1;
float ambientIn = 0.3f;  // Thinking, ItemReact, FallSplat, Revenge: no sender in the PC exe, never fired
bool fired = false, maxDamage = false, anyDeath = false;
Phase lastPhase = Phase::Aim;
Vector3 camPos{};
const Game *G = nullptr;

uint32_t pick(const Game &g, int i, uint32_t salt) {
    uint32_t h = (g.cfg.seed ^ (salt * 0x9E3779B9u)) + uint32_t(i) * 40503u + uint32_t(g.clock) * 2654435761u;
    h ^= h >> 15, h *= 2246822519u, h ^= h >> 13, h *= 3266489917u;
    return h ^ (h >> 16);
}
bool has(const std::string &clip) { return Models::clipLength("worm", clip.c_str()) > 0; }
int worms() { return (int)actors.size(); }
bool isWorm(int a) { return a >= 0 && a < worms(); }
Vector3 feet(const Worm &w) { return {w.pos.x, w.pos.y - Game::R, w.pos.z}; }
Vector3 posOf(int a) {
    if (isWorm(a)) return feet(G->worms[a]);
    if (a == CAMERA) return camPos;
    return a - worms() < (int)props.size() && a >= worms() ? props[a - worms()].pos : Vector3{};
}
Vector3 facing(int a) { return isWorm(a) ? Vector3{sinf(G->worms[a].yaw), 0, cosf(G->worms[a].yaw)} : Vector3{0, 0, 1}; }
int team(int a) { return isWorm(a) ? G->worms[a].team : -1; }
int activeWorm(const Game &g) { return g.current < (int)g.worms.size() && g.worms[g.current].alive && g.phase != Phase::GameOver ? g.current : NONE; }

void load() {
    loaded = true;
    char *text = LoadFileText(ACTING_FILE);
    if (!text) return TraceLog(LOG_INFO, "ACTING: no %s (tools/w4m-re/acting.py): no scenes", ACTING_FILE);
    for (char *line = strtok(text, "\n"); line; line = strtok(nullptr, "\n")) {
        SceneDef s;
        std::string l = line;
        size_t tab = l.find('\t');
        s.name = l.substr(0, tab);
        while (tab != std::string::npos) {
            size_t next = l.find('\t', tab + 1);
            std::string f = l.substr(tab + 1, next == std::string::npos ? std::string::npos : next - tab - 1);
            tab = next;
            TrackDef t;
            std::string tag = f.substr(0, f.find('|')), low = tag;
            for (char &c : low) c = (char)tolower(c);
            for (int k = 0; k < 22; k++) {  // case-insensitive substring, then atoi on what follows (0x60d640)
                size_t at = low.find(TOKENS[k]);
                if (at == std::string::npos) continue;
                t.flags |= 1u << k;
                const char *p = tag.c_str() + at + strlen(TOKENS[k]);
                int n = atoi(p);
                if (k == 1 || k == 2) t.loyal = n;
                else if (k == 5 || k == 21) t.see = n;
                else if (k == 6) t.blind = n;
                else if (k == 10 || k == 11) t.rel = n;
                else if (k == 7) {
                    t.nearT = n;
                    const char *c = strchr(p, ',');
                    if (c && c - p < 4) t.nearR = (float)atof(c + 1);  // "Near0,4"; default 20 (0x60d640)
                }
            }
            std::string ev = f.substr(std::min(f.size(), f.find('|') + 1));
            for (const char *p = ev.c_str(); *p;) {
                char *end;
                int ms = (int)strtol(p, &end, 10);
                if (end == p || !*end) break;
                Ev e{ms, *end, "", 0};
                p = end + 1;
                while (*p && *p != ' ') e.arg += *p++;
                e.n = atoi(e.arg.c_str());
                t.ev.push_back(e);
                while (*p == ' ') p++;
            }
            s.tracks.push_back(t);
        }
        defs.push_back(s);
    }
    UnloadFileText(text);
    for (int k = 0; k < TRIGGERS; k++) {  // each scene filed under every trigger its name contains (0x60e100)
        std::string tn = TRIG_NAMES[k];
        for (char &c : tn) c = (char)tolower(c);
        for (int i = 0; i < (int)defs.size(); i++) {
            std::string low = defs[i].name;
            for (char &c : low) c = (char)tolower(c);
            size_t at = low.find(tn);
            if (at == std::string::npos) continue;
            lists[k].push_back(i);
            if (at == 0) defs[i].n = atoi(defs[i].name.c_str() + tn.size());
        }
        std::stable_sort(lists[k].begin(), lists[k].end(), [](int a, int b) { return defs[a].n > defs[b].n; });
    }
    TraceLog(LOG_INFO, "ACTING: %d scenes from %s", (int)defs.size(), ACTING_FILE);
}

// The list in play order: highest N first, each equal-N group rotated by the trigger count (a/b/c round-robin)
std::vector<int> order(int trig) {
    std::vector<int> l = lists[trig];
    for (size_t i = 0; i < l.size();) {
        size_t j = i;
        while (j < l.size() && defs[l[j]].n == defs[l[i]].n) j++;
        std::rotate(l.begin() + i, l.begin() + i + turns[trig] % (j - i), l.begin() + j);
        i = j;
    }
    return l;
}

void line(int worm, V v) {  // W4M speech is 3D at the speaker; audio.cpp keeps one line per category
    if (worm >= 0 && worm < (int)G->worms.size()) Audio::voice(G->worms[worm].team, v, G->worms[worm].pos);
}
void say(int worm, const std::string &name) {
    static const std::pair<const char *, V> LINES[] = {
        {"Startled", V::Startled}, {"GrenadeLanded", V::Grenade}, {"Shriek", V::Shriek}, {"Gasp", V::Gasp}, {"ShakeFist", V::ShakeFist},
        {"Titter", V::Titter}, {"Disbelief", V::Disbelief}, {"Incoming", V::Incoming}, {"Missed", V::Missed}, {"Mistake", V::Mistake},
        {"Traitor", V::Traitor}, {"DamageInflictedA", V::Damage}, {"DamageInflictedB", V::DamageB}, {"FirstBlood", V::FirstBlood},
        {"SadSigh", V::SadSigh}, {"Yawn", V::Yawn}, {"Sneeze", V::Sneeze}, {"ClutchChest", V::ClutchChest}, {"Nooo", V::Nooo},
        {"WormBounce", V::Bounce}, {"Taunt", V::Taunt}, {"Waiting", V::Waiting}, {"ShortOnTime", V::ShortOnTime}, {"SkipGo", V::SkipGo},
        {"Collect", V::Collect}, {"NoDamageA", V::NoDamageA}, {"NoDamageB", V::NoDamageB}, {"MaxDamage", V::MaxDamage},
        {"PointAndLaugh", V::PointAndLaugh}};  // FriendlyDeath, Victory: main.cpp onEvent says them
    for (auto &[n, v] : LINES)
        if (name == n) return line(worm, v);
}

void release(int r) {
    for (int k = 0; k < 30; k++) {
        int a = runs[r].cast[k];
        if (isWorm(a) && actors[a].run == r) actors[a].run = -1, actors[a].track = -1, actors[a].threatened = false;  // 0x60bf70
    }
    runs[r].scene = -1;
}

bool los(Vector3 a, Vector3 b) {
    Vector3 d = Vector3Subtract(b, a), hit;
    float l = Vector3Length(d);
    return l < 0.1f || !G->terrain.raycast({a, Vector3Scale(d, 1 / l)}, l, &hit);
}

// Criteria for actor x on track t (0x60c480); false: x can't play it. idleOk: flag B, bored actors already acting may idle
bool fits(int x, const TrackDef &t, const int *cast, bool idleOk) {
    int type = isWorm(x) ? 1 : props[x - worms()].type;
    if (type == 5) return false;
    Actor *a = isWorm(x) ? &actors[x] : nullptr;
    auto ref = [&](int n, Vector3 *p) { int c = n < 0 ? CAMERA : n < 30 ? cast[n] : NONE; if (c != NONE) *p = posOf(c); return c != NONE; };
    Vector3 o = posOf(x), p;
    if ((t.flags & SAFE) && a && a->threatened) return false;
    if ((t.flags & THREAT) != (type == 2 ? THREAT : 0) || (t.flags & GOODIES) != (type == 3 ? GOODIES : 0) || (t.flags & DISTRACTION)) return false;
    if (t.flags & INTERESTING) return false;  // no prop is flagged Interesting here
    if (t.flags & (FRIEND | FOE)) {
        int tm = t.loyal < 0 ? team(activeWorm(*G)) : t.loyal < 30 && cast[t.loyal] != NONE ? team(cast[t.loyal]) : -2;
        if (tm == -2) return false;
        if (((t.flags & FRIEND) != 0) != (team(x) == tm)) return false;
    }
    if (a) {
        if ((t.flags & ON_SCREEN) && !a->onScreen) return false;
        if ((t.flags & TARGETED_T) && !a->targeted) return false;
        if (((t.flags & ABDUCTED_T) != 0) != a->abducted) return false;
        bool sick = G->worms[x].poison > 0;
        if (((t.flags & SICK_T) != 0) != sick && !a->abducted) return false;
        if ((t.flags & IDLE_T) && a->run >= 0 && !(a->calm > 90 && idleOk)) return false;  // 90000 ms gate 0x5a47d0 (assumed)
    } else if (t.flags & (ON_SCREEN | TARGETED_T | ABDUCTED_T | SICK_T)) return false;
    if (t.flags & (SEE | LOS)) {
        if (!ref(t.see, &p)) return false;
        Vector3 d = Vector3Normalize(Vector3Subtract(p, o));
        if ((t.flags & SEE) && Vector3DotProduct(facing(x), d) < SEE_COS) return false;
        if ((t.flags & LOS) && !los(Vector3Add(o, {0, 0.5f, 0}), Vector3Add(p, {0, 0.5f, 0}))) return false;
    }
    if (t.flags & BLIND) {
        if (!ref(t.blind, &p)) return false;
        if (Vector3DotProduct(facing(x), Vector3Normalize(Vector3Subtract(p, o))) > SEE_COS) return false;
    }
    if (t.flags & (IN_FRONT | BEHIND)) {  // x in (or out of) the other actor's 70-degree front cone
        if (t.rel < 0 || t.rel >= 30 || cast[t.rel] == NONE) return false;
        int r = cast[t.rel];
        float c = Vector3DotProduct(facing(r), Vector3Normalize(Vector3Subtract(o, posOf(r))));
        if ((t.flags & IN_FRONT) ? c < SEE_COS : c > SEE_COS) return false;
    }
    return true;
}

// Casts track t from pool: the first actor that fits, the nearest within 20 R units for Near tracks (0x60d0f0)
bool castFrom(std::vector<int> &pool, const TrackDef &t, int *cast, int k, bool idleOk) {
    float best = (t.nearR * 20 * UNIT) * (t.nearR * 20 * UNIT);
    int got = -1;
    Vector3 c{};
    bool near = t.flags & NEAR;
    if (near && !(t.nearT < 0 ? (c = camPos, true) : t.nearT < 30 && cast[t.nearT] != NONE && (c = posOf(cast[t.nearT]), true))) return false;
    for (int i = 0; i < (int)pool.size(); i++) {
        if (!fits(pool[i], t, cast, idleOk)) continue;
        if (!near) { got = i; break; }
        float d = Vector3LengthSqr(Vector3Subtract(posOf(pool[i]), c));
        if (d < best) best = d, got = i;
    }
    if (got < 0) return false;
    cast[k] = pool[got];
    pool.erase(pool.begin() + got);
    return true;
}

void start(int scene, const int *cast) {
    int slot = 0;
    while (slot < (int)runs.size() && runs[slot].scene >= 0) slot++;
    if (slot == (int)runs.size()) runs.emplace_back();
    const SceneDef &s = defs[scene];
    for (size_t k = 0; k < s.tracks.size(); k++)  // a cast worm leaves its old scene, which ends for all (0x60b750)
        if (isWorm(cast[k]) && actors[cast[k]].run >= 0 && !(s.tracks[k].flags & (PAYLOAD | ACTIVE))) release(actors[cast[k]].run);
    Run &r = runs[slot];
    r.scene = scene, r.t = 0, r.cur.assign(s.tracks.size(), 0);
    for (int k = 0; k < 30; k++) r.cast[k] = k < (int)s.tracks.size() ? cast[k] : NONE, r.at[k] = r.cast[k] != NONE ? posOf(r.cast[k]) : Vector3{};
    for (size_t k = 0; k < s.tracks.size(); k++)
        if (isWorm(cast[k])) actors[cast[k]].run = slot, actors[cast[k]].track = (int)k, actors[cast[k]].calm = 0;  // flag A clears "bored"
}

// The chooser 0x60d830: the first scene whose tracks can be cast; fa unused here (priority bookkeeping), fc: a Special miss rejects
bool choose(int trig, int payload, int active, std::vector<int> &A, std::vector<int> &B, bool fb, bool fc) {
    for (int si : order(trig)) {
        const SceneDef &s = defs[si];
        if (s.tracks.size() > 30) continue;
        int cast[30];
        std::fill(cast, cast + 30, NONE);
        std::vector<int> a = A, b = B;
        bool ok = true, any = false;
        for (size_t k = 0; k < s.tracks.size() && ok; k++) {
            const TrackDef &t = s.tracks[k];
            if (t.flags & PAYLOAD) { if (payload == NONE) ok = false; else cast[k] = payload; continue; }
            if (t.flags & ACTIVE) {
                std::vector<int> one = {active};
                if (active == NONE || !castFrom(one, t, cast, (int)k, fb)) ok = false; else any = true;
                continue;
            }
            if (castFrom(t.flags & SPECIAL ? a : b, t, cast, (int)k, fb)) any = true;
            else if ((t.flags & SPECIAL) ? fc : (t.flags & CRIT) != 0) ok = false;
        }
        if (!ok || !any) continue;
        A = a, B = b;
        if (getenv("W4NX_ACTLOG")) {  // W4M logs "Chosen Scene:" too (0x60dc73)
            printf("%d Chosen Scene: %s", G->clock, s.name.c_str());
            for (size_t k = 0; k < s.tracks.size(); k++) printf(" %d", cast[k]);
            printf("\n"), fflush(stdout);
        }
        start(si, cast);
        return true;
    }
    return false;
}

void shuffle(const Game &g, std::vector<int> &v, uint32_t salt) {  // 0x60c390: Fisher-Yates on the presentation rng
    for (int i = (int)v.size() - 1; i > 0; i--) std::swap(v[i], v[pick(g, i, salt) % (i + 1)]);
}

// One Acting.Trigger (handler 0x60e2e0, jump table 0x60e818): the trigger's candidate pools, then the chooser
void fire(const Game &g, int trig, int subject = NONE, int payload = NONE) {
    if (lists[trig].empty() && trig != DAMAGE && trig != FIRST_BLOOD && trig != MAX_DAMAGE) return;
    int active = activeWorm(g), n = worms();
    std::vector<int> A, B;
    auto alive = [&](int i) { return g.worms[i].alive && g.worms[i].hp > 0; };
    auto pools = [&](bool props_, int skip1, int skip2) {
        for (int i = 0; i < n; i++) if (alive(i) && i != skip1 && i != skip2) B.push_back(i);
        for (int i = 0; props_ && i < (int)props.size(); i++) B.push_back(n + i);
    };
    auto damaged = [&] { for (int i = 0; i < n; i++) if (alive(i) || g.worms[i].hp <= 0) (actors[i].hp0 > g.worms[i].hp ? A : B).push_back(i); };
    uint32_t salt = 100 + trig;
    switch (trig) {
    case PAYLOAD5: case PAYLOAD4: case PAYLOAD3: case PAYLOAD2: case PAYLOAD1: case CRATE_DROP: {  // threatened: within 100 units (0x60e392, squared 10000)
        Vector3 p = posOf(payload);
        for (int i = 0; i < n; i++)
            if (alive(i) && i != active) (Vector3Distance(feet(g.worms[i]), p) < 100 * UNIT ? A : B).push_back(i);
        shuffle(g, A, salt), shuffle(g, B, salt + 50);
        while (!A.empty() && choose(trig, payload, active, A, B, true, true)) {}
        break;
    }
    case BLAST_SPLAT: case FALL_SPLAT: case DEATH: case COLLECT: case BLASTED: case POISONED: case ZAP:
        A.push_back(subject), pools(false, subject, active), shuffle(g, B, salt);
        choose(trig, NONE, active, A, B, true, true);
        break;
    case IDLE: case SICK: case ABDUCTED: case ITEM_REACT: case THINKING:
        pools(true, active, NONE), shuffle(g, B, salt);
        choose(trig, NONE, active, A, B, false, true);
        break;
    case BORED:
        for (int i = 0; i < n; i++) if (alive(i) && i != active && actors[i].calm > 90) B.push_back(i);
        shuffle(g, B, salt);
        choose(trig, NONE, active, A, B, false, true);
        break;
    case DAMAGE: case FIRST_BLOOD: case MAX_DAMAGE:  // then DamageSilent for the other hurt worms (0x60e682)
        damaged(), shuffle(g, A, salt), shuffle(g, B, salt + 50);
        if (choose(trig, NONE, active, A, B, trig != DAMAGE, true) || trig == DAMAGE)
            while (!A.empty() && choose(DAMAGE_SILENT, NONE, active, A, B, true, true)) {}
        break;
    case MISTAKE:
        A.push_back(subject), pools(false, subject, NONE), shuffle(g, B, salt);
        choose(trig, NONE, active, A, B, true, true);
        break;
    case VICTORY:
        for (int i = 0; i < n; i++) if (alive(i)) (g.worms[i].team == team(subject) ? A : B).push_back(i);
        shuffle(g, A, salt), shuffle(g, B, salt + 50);
        choose(trig, NONE, active, A, B, true, false);
        break;
    default:  // case 4/6/7: everyone but the active worm; WeaponFired brings its payload
        pools(false, active, NONE), shuffle(g, B, salt);
        choose(trig, payload, active, A, B, trig != SKIP_GO, true);
        break;
    }
    turns[trig]++;
}

int addProp(Vector3 p, int type, bool shot) { props.push_back({p, type, shot}); return worms() + (int)props.size() - 1; }

void refreshProps(const Game &g) {
    props.clear();
    for (const Object &o : g.objects) addProp(o.pos, o.type == Object::Crate ? 3 : o.type == Object::Mine || o.type == Object::Barrel ? 2 : 5, false);
}

void particle(int i, const std::string &name) {  // WXP_* emitters of PARTTWK.XOM, at HatLocator / VomitLocator
    Actor &a = actors[i];
    if (a.nfx < 3) a.fx[a.nfx++] = {name, 0};
}

// Locator in world space: HatLocator, or VomitLocator (0, 3, 1) units on the head = (0, -15, 2) from HatLocator (w4m-models --list)
Vector3 locator(int i, bool vomit) {
    const Worm &w = G->worms[i];
    Matrix m;
    Vector3 f = feet(w);
    if (!Models::joint("worm", "HatLocator", "Base", 0, true, &m)) return Vector3Add(f, {0, 0.9f, 0});
    Vector3 p = Vector3Transform(vomit ? Vector3{0, -15, 2} : Vector3{0, 0, 0}, m);
    return Vector3Add(Vector3Transform(p, MatrixRotateY(w.yaw)), f);
}

void stepFx(const Game &g, int i, float dt) {
    Actor &a = actors[i];
    const Worm &w = g.worms[i];
    Vector3 fwd = {sinf(w.yaw), 0, cosf(w.yaw)};
    for (int k = 0; k < a.nfx;) {
        Actor::Fx &e = a.fx[k];
        float t0 = e.t;
        e.t += dt;
        bool done = false;
        if (e.name == "Vomit_Fluid") {  // after 2000 ms: 3 drops, 0.75 units, 600 +- 200 ms, launched (0, 0.5, 3) units/frame
            if (t0 < 2 && e.t >= 2)
                for (int d = 0; d < 3; d++) {
                    uint32_t q = pick(g, i, 40 + d);
                    auto rr = [&](int s) { return ((q >> s) & 255) / 127.5f - 1; };
                    Vector3 v = Vector3Add(Vector3Scale(fwd, (3 + rr(0) * 0.2f) * UNIT / 0.02f), {rr(8) * UNIT / 0.02f, (0.5f + rr(16) * 0.2f) * UNIT / 0.02f, 0});
                    Fx::sprite(locator(i, true), v, 0.6f + rr(24) * 0.2f, 0.75f * 4 * UNIT, 0.75f * 2 * UNIT, {230, 180, 100, 255}, 9.8f, true);
                }
            done = e.t >= 2;
        } else if (e.name == "Sneeze") {  // after 100 ms: 8 drops, 0.9 units, 400 +- 200 ms, (0, -0.05, 0.5) units/frame
            if (t0 < 0.1f && e.t >= 0.1f)
                for (int d = 0; d < 8; d++) {
                    uint32_t q = pick(g, i, 50 + d);
                    auto rr = [&](int s) { return ((q >> s) & 255) / 127.5f - 1; };
                    Vector3 v = Vector3Add(Vector3Scale(fwd, (0.5f + rr(0) * 0.3f) * UNIT / 0.02f), {rr(8) * 0.05f * UNIT / 0.02f, (-0.05f + rr(16) * 0.05f) * UNIT / 0.02f, 0});
                    Fx::sprite(locator(i, true), v, 0.4f + rr(24) * 0.2f, 0.9f * 4 * UNIT, 0.8f * 2 * UNIT, {204, 204, 140, 255}, 2, true);
                }
            done = e.t >= 0.1f;
        } else done = true;
        if (done) a.fx[k] = a.fx[--a.nfx];
        else k++;
    }
}

// PoseBlend: Blend.Scale.y keys, played at 1 / BlendTime (0.3 s, every WORMACTING emote) from a new look or gesture target
float poseA(const Actor &a) {
    static const float K[3][6] = {{1, 0, 1, 0, 0, 0}, {0.70703125f, 0.70703125f, 0.70703125f, 0.70703125f, 0.75f, 1}, {1, 0, 1, 0, 1, 1}};
    return Models::curve(K, 3, a.poseT / 0.3f);
}

// A new look or gesture target: the layers cross-fade from where they are (0x59bb90)
void snap(Actor &a) {
    float *o = &a.old.hy, *c = &a.cur.hy, A = poseA(a);
    for (int k = 0; k < 6; k++) o[k] += A * (c[k] - o[k]);
    a.eyeOld += A * (a.eyeMove - a.eyeOld);
    a.poseT = 0;
}

// Track k of run r: its events up to the run's clock (WormScenePlayerService 0x60b1b0)
void play(int r) {
    Run &run = runs[r];
    const SceneDef &s = defs[run.scene];
    bool more = false;
    for (size_t k = 0; k < s.tracks.size(); k++) {
        const std::vector<Ev> &ev = s.tracks[k].ev;
        int x = run.cast[k];
        for (; run.cur[k] < ev.size() && ev[run.cur[k]].ms <= run.t * 1000; run.cur[k]++) {
            if (!isWorm(x) || actors[x].run != r) continue;
            const Ev &e = ev[run.cur[k]];
            Actor &a = actors[x];
            auto target = [&](int n, int *id, Vector3 *at) {  // own track: stop (0x7d), < 0: the camera (0x7e)
                if (n == (int)k) *id = STOP;
                else if (n < 0) *id = CAMERA;
                else if (n < 30 && run.cast[n] != NONE) *id = run.cast[n], *at = run.at[n];
            };
            switch (e.op) {
            case 'e': {  // name[,PermittedEyeMovement[,Coyness]]
                float eye = 0, coy = 0;
                std::string em = e.arg.substr(0, e.arg.find(','));
                if (em.size() < e.arg.size()) sscanf(e.arg.c_str() + em.size(), ",%f,%f", &eye, &coy);
                a.emote = em == "Default" ? "" : em, a.eyeOld = a.eyeMove, a.eyeMove = eye, a.coy = coy * DEG2RAD;  // 0x59e7e6
                break;
            }
            case 'p':  // 0x59c990: the playing gesture becomes the old one (weight at most 0.9), the same clip is not restarted
                if (has(e.arg) && e.arg != a.act[0].clip) a.act[1] = a.act[0], a.act[1].w = fminf(a.act[0].w, 0.9f), a.act[0] = {e.arg, 0, 1 - a.act[1].w}, a.stopRate = 0;
                break;  // unknown names (ShakeFist, CoverHead...) play nothing
            case 'x': if (e.n > 0) a.stopRate = 20.f / e.n; break;  // BlendTime ms; 0 (141 of 149) does nothing (0x59e85a)
            case 's': say(x, e.arg); break;
            case 'l': snap(a), target(e.n, &a.look, &a.lookAt);
                if (a.look == STOP) a.headY = a.headP = a.cur.ey = a.cur.ep = 0;  // 0x59e92b
                else a.coyOff = a.coy;  // 0x59ea23: the sign follows the head's turn, which the exe measures as 0: always +
                break;
            case 'g': snap(a), target(e.n, &a.gest, &a.gestAt); break;
            case 't': a.threatened = e.n != 0; break;
            case 'f': particle(x, e.arg); break;
            }
        }
        more |= run.cur[k] < ev.size();
    }
    if (!more) release(r);
}

// Head and arm angles toward an actor, in the worm's frame, from 9 units above its feet (0x59be40, 0x59c3e0)
void aimAt(int i, int id, Vector3 &stored, float *yaw, float *pitch) {
    if (id == NONE || id == STOP) return void(*yaw = *pitch = 0);
    const Worm &w = G->worms[i];
    Vector3 t = id == CAMERA ? camPos : isWorm(id) ? Vector3Add(feet(G->worms[id]), {0, 9 * UNIT, 0}) : stored;
    if (!isWorm(id) && id != CAMERA)
        for (const Projectile &s : G->shots) if (Vector3Distance(s.pos, stored) < 3) t = stored = s.pos;  // follows a payload in flight
    Vector3 d = Vector3Subtract(t, Vector3Add(feet(w), {0, 9 * UNIT, 0}));
    float mx = d.x * cosf(w.yaw) - d.z * sinf(w.yaw), mz = d.x * sinf(w.yaw) + d.z * cosf(w.yaw);
    *yaw = atan2f(mx, mz), *pitch = atan2f(d.y, sqrtf(mx * mx + mz * mz));
}
float ease(float v, float to, float dt) { return v + (to - v) * (1 - powf(0.9f, dt / 0.02f)); }  // 0.1 per 20 ms frame (0x59c106; law assumed)

// 0x47a1a0, once per update: v += clamp((to - v) / (k + 1), +-max)
float smooth(float v, float to, float k, float max) { return v + Clamp((to - v) / (k + 1), -max, max); }

// 0x59bd80: past lo (around the Coyness offset) the head target takes the eyes' lead, fully from hi (soft in between)
void lead(float *eye, float lo, float hi, float *head, float coy) {
    float r = *eye - coy, ex = r > lo ? r - lo : r < -lo ? r + lo : 0, w = hi - lo;
    float m = ex * (w > 0 ? fminf(1, ex * ex / (w * w)) : 1);
    *head += m, *eye -= m;
}

// The head target stops at [lo, hi]; the eyes take the rest, up to eyeMax (0x59c282)
void limit(float *head, float *eye, float lo, float hi, float eyeMax) {
    if (*head > hi) *eye = fminf(*eye + *head - hi, eyeMax), *head = hi;
    if (*head < lo) *eye = fmaxf(*eye + *head - lo, -eyeMax), *head = lo;
}

// One WormPoseManager update (0x59da40, once per 20 ms tick, at most once per frame): acting weights, look (0x59be40),
// gesture (0x59c3e0), head ease (0x59b450)
void lookStep(Actor &a, int i, bool forbid, bool busy) {
    a.actS = (a.actS + !busy) / 2, a.lookW = (a.lookW + !forbid) / 2;  // +0x194 (off the ground or walking: 0), +0x198
    for (Actor::Gest &c : a.act)
        if (!c.clip.empty() && c.t >= Models::clipLength("worm", c.clip.c_str())) c = {};  // a finished clip drops out (0x59dc41)
    a.act[1].w = fmaxf(0, a.act[1].w - 0.1f);
    if (a.stopRate > 0 && (a.act[0].w -= a.stopRate) <= 0) a.act[0] = {}, a.stopRate = 0;  // StopAnimation's fade (0x59dd9c)
    else if (a.stopRate <= 0 && !a.act[0].clip.empty()) a.act[0].w = 1 - a.act[1].w;
    float m = Clamp(a.mode, 0, 2), y, p;  // Blend.Rotate.y: 0 head and eyes, 1 eyes only, 2 neither
    a.headW = smooth(a.headW, m < 1 ? 1 - m : 0, 1, 0.1f), a.eyeW = smooth(a.eyeW, m < 1 ? 1 : 2 - m, 1, 0.1f);
    aimAt(i, a.look, a.lookAt, &y, &p);
    float E = (a.eyeOld + poseA(a) * (a.eyeMove - a.eyeOld)) * DEG2RAD, H = a.headW;
    y -= a.headY, p -= a.headP;
    lead(&y, 0.6f * E, E, &a.headY, a.coyOff), lead(&p, 0.6f * E, E, &a.headP, 0);  // pitch Coyness +0x190 stays 0
    limit(&a.headP, &p, -0.785f * H, 1.222f * H, 1.047f);  // pitch -45..+70 degrees, as in the exe (HeadRotX itself stops at +45)
    limit(&a.headY, &y, -1.047f * H, 1.047f * H, 1.222f);
    a.cur.ey = y, a.cur.ep = p;
    aimAt(i, a.gest, a.gestAt, &a.cur.gy, &a.cur.gp);  // instant: only the PoseBlend fade smooths a retarget
    a.cur.hy = smooth(a.cur.hy, a.headY, 2, 0.5236f), a.cur.hp = smooth(a.cur.hp, a.headP, 2, 0.5236f);
}
}  // namespace

void Acting::event(const Game &g, const GameEvent &e) {
    if (!loaded) load();
    if (actors.size() != g.worms.size()) return;
    G = &g;
    refreshProps(g);
    int tm = e.worm >= 0 ? g.worms[e.worm].team : -1, cur = activeWorm(g), curTeam = cur != NONE ? g.worms[cur].team : -1;
    switch (e.kind) {
    case GameEvent::TurnStart:
        fired = maxDamage = false, lastSec = -1;
        for (size_t i = 0; i < actors.size(); i++) actors[i].hp0 = g.worms[i].hp;
        if (cur != NONE) fire(g, START_TURN, cur);
        break;
    case GameEvent::Fire: {
        if (e.worm < 0) break;  // sentry gun
        const WeaponDef &d = WEAPONS[e.weapon];
        Kind k = d.kind;
        if (k == Kind::SkipGo || k == Kind::Surrender) { fire(g, SKIP_GO, e.worm); break; }
        if (utility(k) || k == Kind::Flood) break;
        fired = true;
        if (k == Kind::Airstrike || k == Kind::Donkey) { fire(g, AIRSTRIKE, e.worm); break; }  // Bomber 0x54d7c0
        const Projectile *shot = nullptr;
        for (const Projectile &s : g.shots)
            if (Vector3Distance(s.pos, e.pos) < (shot ? Vector3Distance(shot->pos, e.pos) : 4)) shot = &s;
        fire(g, WEAPON_FIRED, e.worm, shot ? addProp(shot->pos, 2, true) : NONE);  // 0x585e3d, its payload on the Payload track
        break;
    }
    case GameEvent::Hurt: {
        for (const GameEvent &x : g.events) if (x.kind == GameEvent::TurnStart) return;  // poison tick
        if (e.worm < 0) break;
        actors[e.worm].kicked = true;
        for (const GameEvent &x : g.events)  // a worm in the blast's full-damage core: Turn.MaxDamage (0x5ae6b5)
            if (x.kind == GameEvent::Boom && x.weapon >= 0) {
                Blast b = blastOf(WEAPONS[x.weapon], false);
                float d = fmaxf(0, Vector3Distance(e.pos, x.pos) - Game::R);
                if (b.damage > 0 && d < b.reach && 1.2f * (b.reach - d) / b.reach >= 1) maxDamage = true;
            }
        break;
    }
    case GameEvent::Death:
        if (cur == NONE || e.worm < 0) break;
        if (tm != curTeam && anyDeath) line(cur, V::EnemyDeath);  // lsd category, no scene
        anyDeath = true;
        break;
    case GameEvent::Splash: if (e.worm >= 0) line(e.worm, V::Drown); break;
    case GameEvent::Zap: fire(g, ZAP, e.worm); break;  // UpdateAbductee 0x5a9d69 (link to the Zap trigger assumed)
    case GameEvent::Collect:
        if (e.worm < 0) break;
        fire(g, COLLECT, e.worm);
        break;
    case GameEvent::CrateDrop: if (cur != NONE) line(cur, V::CrateDrop); break;
    case GameEvent::CrateLand: fire(g, CRATE_DROP, NONE, addProp(e.pos, 3, false)); break;  // sender 0x5c4710 (on landing, assumed)
    case GameEvent::GameOver:
        for (size_t i = 0; i < actors.size(); i++)
            if (g.worms[i].alive && g.worms[i].team == g.winner) { fire(g, VICTORY, (int)i); break; }
        break;
    default: break;
    }
}

void Acting::taunt(const Game &g, int worm, const std::string &weapon) {
    static const std::pair<const char *, int> T[] = {
        {"Grenade", TAUNT_MELEE}, {"Dynamite", TAUNT_MELEE}, {"Landmine", TAUNT_MELEE}, {"Baseball Bat", TAUNT_MELEE}, {"Prod", TAUNT_MELEE},
        {"Fire Punch", TAUNT_MELEE}, {"Tail Nail", TAUNT_MELEE},
        {"Airstrike", TAUNT_STRIKE}, {"Flood", TAUNT_STRIKE}, {"Concrete Donkey", TAUNT_STRIKE}, {"Alien Abduction", TAUNT_STRIKE}, {"Super Airstrike", TAUNT_STRIKE},
        {"Bazooka", TAUNT_RANGED}, {"Cluster Grenade", TAUNT_RANGED}, {"Holy Hand Grenade", TAUNT_RANGED}, {"Banana Bomb", TAUNT_RANGED},
        {"Shotgun", TAUNT_RANGED}, {"Homing Missile", TAUNT_RANGED}, {"Sheep", TAUNT_RANGED}, {"Gas Canister", TAUNT_RANGED}, {"Old Woman", TAUNT_RANGED},
        {"Super Sheep", TAUNT_RANGED}, {"Starburst", TAUNT_RANGED}, {"Inflatable Scouser", TAUNT_RANGED},
        {"Poison Arrow", TAUNT_RANGED}, {"Sentry Gun", TAUNT_RANGED}, {"Sniper Rifle", TAUNT_RANGED}};
    if (!loaded) load();
    if (actors.size() != g.worms.size() || worm < 0) return;
    G = &g;
    refreshProps(g);
    for (const auto &e : T) if (weapon == e.first) return fire(g, e.second, worm);
}

void Acting::update(const Game &g, float dt, const std::vector<uint8_t> &busy, const Camera3D &cam) {
    if (!loaded) load();
    G = &g, camPos = cam.position;
    size_t n = g.worms.size();
    if (actors.size() != n || g.clock < lastClock) {  // new match, or a replay rewound
        actors.assign(n, Actor{});
        runs.clear(), rested.clear();
        for (size_t i = 0; i < n; i++) {
            Actor &a = actors[i];
            a.dflt = pick(g, (int)i, 1) & 0x100 ? "Frown" : "Angry", a.poison = g.worms[i].poison, a.last = g.worms[i].pos, a.hp0 = g.worms[i].hp;
        }
        if (g.clock == 0) anyDeath = false;
        lastPhase = g.phase;
    }
    lastClock = g.clock;
    refreshProps(g);
    int cur = activeWorm(g);
    bool control = cur != NONE && g.phase == Phase::Aim;

    // the shot is over (GameLogic.ApplyDamage, evaluator 0x5b22a0): Mistake, FirstBlood, MaxDamage, Boring or DamageInflicted
    if (lastPhase != Phase::Settle && g.phase == Phase::Settle && cur != NONE) {
        int enemy = 0, friendly = 0;
        bool dies = false;
        for (size_t i = 0; i < n; i++) {
            int d = actors[i].hp0 - std::max(0, g.worms[i].hp);
            if (d > 0) (g.worms[i].team == g.worms[cur].team ? friendly : enemy) += d;
            dies |= d > 0 && g.worms[i].hp <= 0;
        }
        bool hurtSelf = actors[cur].hp0 > g.worms[cur].hp, mistake = friendly > enemy / 3;
        if (mistake && hurtSelf) fire(g, MISTAKE, cur);
        else if (enemy + friendly > 0 && !mistake && dies && !anyDeath) fire(g, FIRST_BLOOD);
        else if (!mistake && maxDamage) fire(g, MAX_DAMAGE);
        else if (enemy + friendly == 0) fire(g, fired ? MISSED : BORING);  // Missed: Timer.StartPostActivity 0x50fe0d
        else fire(g, DAMAGE);
        fired = false;
    }
    // TimerLogicEntity 0x50f44b: RetreatTimeRemaining passing 4000 ms
    static bool late = false;
    bool now = (g.phase == Phase::Flying || g.phase == Phase::Retreat) && g.retreatTicks(WEAPONS[g.weapon]) > msTicks(4000) && g.timer <= msTicks(4000);
    if (now && !late && cur != NONE) fire(g, RETREAT, cur);
    late = now;
    lastPhase = g.phase;

    // HudClockEntity 0x5f0185: the displayed seconds reach 5 (ShortOnTime) or 15 (Waiting)
    int sec = (g.timer + 59) / 60;
    if (control && !g.hotSeat && sec != lastSec && (sec == 5 || sec == 15)) fire(g, sec == 5 ? SHORT_ON_TIME : WAITING, cur);
    lastSec = control ? sec : -1;

    for (size_t i = 0; i < n; i++) {
        const Worm &w = g.worms[i];
        Actor &a = actors[i];
        if (!w.alive) { if (a.run >= 0) release(a.run); a.act[0] = a.act[1] = {}; continue; }
        Vector2 s = GetWorldToScreen(w.pos, cam);  // OnScreen: in the view frustum (0x5a2420)
        a.onScreen = Vector3DotProduct(Vector3Subtract(w.pos, cam.position), Vector3Subtract(cam.target, cam.position)) > 0 && s.x >= 0 &&
                     s.y >= 0 && s.x < GetScreenWidth() && s.y < GetScreenHeight();
        // Targeted (0x5a2600): 100 ms in the first-person aim view, then 3000 ms before it can fire again
        bool aimed = control && (int)i != cur && a.onScreen && Controls::firstPerson(g);
        a.aimMs = Clamp(a.aimMs + (aimed ? 1 : -1) * dt * 1000, 0, 200), a.coolMs = fmaxf(0, a.coolMs - dt * 1000);
        if (a.coolMs <= 0 && a.aimMs >= 100) a.coolMs = 3000, a.targeted = true, fire(g, TARGETED, (int)i);
        else if (a.coolMs <= 0) a.targeted = false;
        // physics: blasted off (event 13, vy > 0.1 units/ms), its landing (BlastSplat 0x5a3d4d)
        bool moved = Vector3Distance(w.pos, a.last) > 1e-3f || !w.grounded;
        a.calm = moved ? 0 : a.calm + dt, a.last = w.pos;
        if (!w.grounded) {
            if (a.kicked && w.vel.y > 0.1f * UNIT * 1000 && !a.flying) a.flying = true, fire(g, BLASTED, (int)i);
            a.air = true;
        } else if (a.air) {
            if (a.flying) fire(g, BLAST_SPLAT, (int)i);
            a.air = a.flying = false;
        }
        a.kicked = false;
        if (g.dying() == (int)i && !a.dead && !g.drowned((int)i)) a.dead = true, fire(g, DEATH, (int)i);  // kWPS_DeathThroes (0x5a3ffb)
        if (w.poison > a.poison) fire(g, POISONED, (int)i);  // 0x5a1a89
        a.poison = w.poison;
        // W4M Sick.Colour / Abducted.Colour weights ease in and out (rate assumed)
        a.abducted = w.abducted;  // 0x547d39 sets it as the UFO spits the worm out, poison and Worm.Antidote clear it (0x5ade00, 0x5adf13)
        a.sickW = ease(a.sickW, w.poison > 0 ? 1.f : 0.f, dt), a.abdW = ease(a.abdW, a.abducted ? 1.f : 0.f, dt);
    }
    // a timed payload comes to rest: TimedPayload by the whole seconds left (PayloadLogicEntity 0x577181)
    for (const Projectile &s : g.shots) {
        const WeaponDef &d = WEAPONS[s.weapon];
        if (d.kind != Kind::Shell || d.fuse <= 0 || (!dropped(d) && Vector3Length(s.vel) > 2)) continue;
        bool seen = false;
        for (Vector3 p : rested) seen |= Vector3Distance(p, s.pos) < 1;
        if (seen) continue;
        rested.push_back(s.pos);
        int left = (int)s.fuse;
        fire(g, left >= 4 ? PAYLOAD5 : PAYLOAD1 - left, NONE, addProp(s.pos, 2, true));
    }
    for (size_t k = 0; k < g.objects.size(); k++) {  // an armed mine counts as a resting timed payload (assumed)
        const Object &o = g.objects[k];
        bool armed = o.type == Object::Mine && o.fuse >= 0 && !o.dud, seen = false;
        for (Vector3 p : rested) seen |= Vector3Distance(p, o.pos) < 0.3f;
        if (armed && !seen) rested.push_back(o.pos), fire(g, o.fuse >= 4 ? PAYLOAD5 : PAYLOAD1 - (int)o.fuse, NONE, worms() + (int)k);
    }
    // ambient triggers every 300-600 ms: Idle, Sick, Abducted, Bored (WXWormManagerService 0x5b3534, table 0x9200dc)
    if (g.phase != Phase::GameOver && (ambientIn -= dt) <= 0) {
        static const int CYCLE[4] = {IDLE, SICK, ABDUCTED, BORED};
        fire(g, CYCLE[ambientIdx % 4]);
        ambientIdx = (ambientIdx + 1) % 12;
        ambientIn = (300 + pick(g, 0, 9) % 300) * MS;
    }
    for (int r = 0; r < (int)runs.size(); r++)
        if (runs[r].scene >= 0) runs[r].t += dt, play(r);
    // the per-frame task queue runs on time rounded up to 20 ms (0x68d57a), the worm updates when that moved (0x5a47a0)
    static float tick = 0;
    bool step = (tick += dt) >= 0.02f;
    if (step) tick = fmodf(tick, 0.02f);
    for (size_t i = 0; i < n; i++) {
        Actor &a = actors[i];
        if (!g.worms[i].alive) continue;
        for (Actor::Gest &c : a.act) c.t += dt;  // XAnim clips run in real time, whatever their weight
        a.poseT += dt;
        // the aiming worm: weapons set Forbid Lookaround (0x59f3a0)
        if (step) lookStep(a, (int)i, (int)i == g.current && g.phase == Phase::Aim, busy[i]);
        stepFx(g, (int)i, dt);
    }
}

const char *Acting::clip(const Game &g, int i, float clock, float *t, bool *loop, Models::Layers *ly) {
    if (i < 0 || i >= (int)actors.size()) return nullptr;
    const Actor &a = actors[i];
    bool sick = g.worms[i].poison > 0 && g.worms[i].hp > 0;
    std::string emote = a.emote.empty() && sick ? "Ill" : a.emote;
    const char *face = !emote.empty() && has(emote) ? nullptr : a.dflt;
    static std::string keep[64];  // the c_str() handed out must outlive this call
    std::string &e = keep[i % 64];
    e = emote;
    if (ly) {
        *ly = {};
        ly->face = face ? (has(face) ? face : nullptr) : e.c_str(), ly->faceT = clock + i * 1.3f;
        float A = poseA(a);
        auto at = [&](float o, float c) { return o + A * (c - o); };  // A new + (1 - A) old
        ly->lookYaw = a.lookW * at(a.old.hy, a.cur.hy), ly->lookPitch = a.lookW * at(a.old.hp, a.cur.hp);
        ly->gestYaw = a.lookW * at(a.old.gy, a.cur.gy), ly->gestPitch = a.lookW * at(a.old.gp, a.cur.gp);
        ly->eyeYaw = a.eyeW * at(a.old.ey, a.cur.ey), ly->eyePitch = a.eyeW * at(a.old.ep, a.cur.ep);
        for (int k = 0; k < 2; k++)  // gesture weight x ground factor (0x59dcac)
            if (!a.act[k].clip.empty()) ly->act[k] = a.act[k].clip.c_str(), ly->actT[k] = a.act[k].t, ly->actW[k] = a.actS * a.act[k].w;
    }
    if (face) return nullptr;
    *t = clock + i * 1.3f, *loop = true;
    return e.c_str();
}

void Acting::headMode(int i, float deg) {
    if (i >= 0 && i < (int)actors.size()) actors[i].mode = deg;
}

Color Acting::tint(int i, Color c) {
    if (i < 0 || i >= (int)actors.size()) return c;
    const Actor &a = actors[i];
    // 0x5a1bb1: 1 + 2 (sick (Sick.Colour/255 - .5) + abducted (Abducted.Colour/255 - .5)), green as /255 * .5 (sic)
    const float SICK[3] = {120, 120, 110}, ABD[3] = {0, 120, 255};
    unsigned char *ch[3] = {&c.r, &c.g, &c.b};
    for (int k = 0; k < 3; k++) {
        auto f = [&](float v) { return k == 1 ? v / 255 * 0.5f : v / 255 - 0.5f; };
        float m = 1 + 2 * (a.sickW * f(SICK[k]) + a.abdW * f(ABD[k]));
        *ch[k] = (unsigned char)Clamp(*ch[k] * m, 0, 255);
    }
    return c;
}
