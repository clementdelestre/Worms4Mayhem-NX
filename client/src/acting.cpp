#include "acting.h"
#include "audio.h"
#include "models.h"
#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstring>

namespace {
using V = Audio::Voice;
constexpr V NO = V::Count;
// clip: an emote (looped until the next), "-" clears the emote, "" ends the scene, else a gesture; ms from the scene start
struct Beat { int ms; const char *clip; V v = NO; };
using Track = std::vector<Beat>;
struct Scene { Track self, friendT, foeT; };  // subject; a friend / a foe of the subject nearby

// Transcribed from WORMACTING.XOM; unexported clips and face-only emotes (Happy, Angry, Frown...) became "-" or were dropped.
const Scene PAYLOAD5[] = {  // TimedPayloadFive0a/b/c, 10 (n = fuse seconds left)
    {{{80, "Startled"}, {340, "Scared"}, {1660, "-"}, {2050, "Shake_Fist", V::ShakeFist}, {3660, "Cover_Head"}, {5820, "Terror"}, {7470, ""}},
     {}, {{2100, "Titter", V::Titter}, {3200, ""}}},
    {{{80, "Startled", V::Startled}, {570, "Scared"}, {620, "Gasp", V::Gasp}, {2760, "Cover_Head"}, {7470, ""}},
     {}, {{2100, "Titter", V::Titter}, {6320, "Sad"}, {8580, ""}}},
    {{{80, "Startled"}, {130, "Scared"}, {600, "Blow"}, {6240, ""}}, {}, {{2100, "Chuckle"}, {4760, "Cover_Head"}, {6500, ""}}},
    {{{70, "Scared"}, {1030, "Shriek", V::Shriek}, {1400, "CantLook"}, {7470, ""}},
     {{60, "Scared"}, {1020, "Shriek", V::Shriek}, {7470, ""}}, {}},
};
const Scene PAYLOAD1_4[] = {  // TimedPayloadOne0..Four0 (Three0's "CoverHead" names no clip in W4M either)
    {{{280, "Startled", V::Disbelief}, {710, "Terror"}, {1740, "Scared"}, {3870, ""}}},
    {{{90, "Terror"}, {590, nullptr, V::Startled}, {1040, "CantLook"}, {2790, "Shriek", V::Shriek}, {5970, ""}}},
    {{{60, "Scared"}, {350, "CantLook"}, {3000, ""}}},
    {{{90, "Disbelief", V::Disbelief}, {3100, "Pray"}, {7350, ""}}},
};
const Scene TARGETED[] = {  // Targeted0a, 10a (foe aimed at), 0b (friend)
    {{{110, "CowerEmote"}, {4140, "Scared"}, {4740, "Wipe_Brow"}, {8310, ""}}},
    {{{160, "Terror"}, {800, "Indicate"}, {4870, "Startled"}, {6010, "ShakeHead"}, {7060, ""}}},
    {{{1140, "ShakeHead"}, {7850, "Salute"}, {10300, ""}}},
};
const Track STRIKE_CRIT = {{190, "Scared"}, {260, nullptr, V::Incoming}, {7560, "Shriek"}, {10000, ""}};  // Airstrike10
const Track STRIKE_FOE[] = {{{120, "Scared"}, {5400, "ShakeHead"}, {8410, "Cover_Head"}, {10000, ""}},
                            {{110, "Scared"}, {6660, "Shriek"}, {8940, "Cover_Head"}, {10000, ""}}};
const Track STRIKE_FRIEND = {{3740, "Watch_Distant"}, {4260, "Scared"}, {8000, ""}};
const Track FIRED_FOE[] = {{{700, "Scared"}, {2200, "Gasp"}, {5510, "-"}, {7530, ""}},  // WeaponFired10
                           {{610, "Scared"}, {1710, "Watch_Distant"}, {5510, "-"}, {7530, ""}}};
const Scene BLASTED = {{{20, "Sad"}, {110, nullptr, V::Nooo}, {3000, ""}},  // Blasted10
                       {{290, "CantLook"}, {3600, "-"}, {4800, ""}}, {{190, "Scared"}, {440, "PointAndLaugh"}, {6000, ""}}};
const Scene BOUNCE = {{{20, nullptr, V::Bounce}, {100, ""}},  // WormBounce10
                      {{100, "Gasp", V::Gasp}, {230, "Sad"}, {3000, ""}}, {{150, "PointAndLaugh"}, {6000, ""}}};
const Scene DAMAGE = {{{50, "-"}, {3180, nullptr, V::Traitor}, {3300, ""}},  // DamageInflicted0a, self: friendly fire only
                      {{150, "Sad"}, {310, "SeeImpact"}, {1030, "Doh"}, {3310, "WhatWereYouThinking"}, {7000, ""}},
                      {{750, "Chuckle"}, {950, nullptr, V::Titter}, {3270, "Cheer"}, {8970, ""}}};
const Scene MISSED = {{}, {{220, "SighAndShakeHead"}, {3000, ""}}, {{300, "Wipe_Brow"}, {1020, nullptr, V::Missed}, {3000, ""}}};  // Missed10a, of the shooter
const Scene MISTAKE = {{{20, "Sad"}, {2010, "Doh", V::Mistake}, {6000, ""}},  // Mistake0a
                       {{100, "SighAndShakeHead"}, {3000, ""}}, {{200, "PointAndLaugh"}, {6000, ""}}};
const Track DEATH[] = {{{50, "Sad"}, {630, "WhatWereYouThinking"}, {3000, ""}}, {{100, "Sad"}, {810, "SighAndShakeHead"}, {3000, ""}},  // Death5a-e
                       {{100, "Sad"}, {810, "Salute"}, {3000, ""}}, {{100, "Sad"}, {160, "ClutchChest"}, {3000, ""}},
                       {{90, "Sad"}, {200, "Doh"}, {3000, ""}}};
const Track DEATH_SICK = {{70, "Ill"}, {90, "Vomit"}, {3500, ""}};  // Death15a
const Track MOURN = {{100, "Sad"}, {4000, ""}};                      // Death10: friends nearby
const Track POISONED[] = {{{70, "Ill"}, {90, "ClutchChest", V::ClutchChest}, {7500, ""}}, {{70, "Ill"}, {90, "Vomit"}, {5850, ""}}};  // Poisoned5a/b
const Track SICK[] = {{{80, "Ill"}, {1260, "Vomit"}, {8890, "Sneeze", V::Sneeze}, {17940, "Sneeze", V::Sneeze}, {20000, ""}},  // Sick10a-c
                      {{160, "Ill"}, {6220, "Sneeze", V::Sneeze}, {9000, ""}},
                      {{80, "Ill"}, {1240, "Sneeze", V::Sneeze}, {9030, "Sneeze", V::Sneeze}, {12000, ""}}};
const Track IDLE[] = {  // Idle20a-f, Bored0a-c, plus the two original fidgets
    {{3150, "Wave"}, {5500, ""}}, {{1390, "Yawn2", V::Yawn}, {4140, "Sad"}, {7000, ""}}, {{1390, "Salute"}, {4500, ""}},
    {{1390, "Thumbs_Up"}, {4000, ""}}, {{1390, "Cheer"}, {4000, ""}}, {{80, "Nervous"}, {240, "Watch_Distant"}, {6000, ""}},
    {{1390, "Bored", V::SadSigh}, {8760, "Sad"}, {14200, ""}}, {{1390, "Yawn"}, {5100, ""}}, {{300, "ScratchHead"}, {2500, ""}}};
const Track NEAR_FRIEND[] = {{{270, "Salute"}, {3300, ""}}, {{270, "Cheer"}, {3000, ""}}, {{1400, "ClaspHands"}, {5000, ""}}};  // Idle0a/b/d
const Track NEAR_FOE[] = {{{320, "BringItOn"}, {3110, "Taunt1"}, {9190, "Scared"}, {10410, "Nervous"}, {13410, ""}},  // Idle10a/b/c
                          {{320, "Taunt1"}, {3240, nullptr, V::Taunt}, {9190, "Scared"}, {11940, ""}},
                          {{810, "Startled", V::Startled}, {1600, "Shake_Fist", V::ShakeFist}, {3900, "Scared"}, {7560, ""}}};
const Track SKIPGO = {{150, "SighAndShakeHead"}, {410, nullptr, V::SkipGo}, {3270, ""}};  // SkipGo5
const Track CHEER = {{920, "Cheer"}, {4560, ""}};                                        // FirstBlood10
const Track VICTORY[] = {{{420, "ClaspHands"}, {6080, "ClaspHands"}, {17140, "ClaspHands"}, {19960, ""}},
                         {{400, "Cheer"}, {9400, "Cheer"}, {20000, ""}}, {{410, "Cheer"}, {4980, "Cheer"}, {10960, "Cheer"}, {20000, ""}}};
const char *const EMOTES[] = {"Scared", "Terror", "Nervous", "CowerEmote", "CantLook", "Sad", "Ill"};

// Priorities: a scene only replaces a lower one.
enum { P_IDLE, P_NEAR, P_MOURN, P_FIRED = 15, P_TARGET = 20, P_MISS = 25, P_DAMAGE = 28, P_HIT = 30, P_STRIKE = 35, P_PAYLOAD = 40, P_DEATH = 50, P_WIN = 60 };

struct Role {
    const Track *tr = nullptr; size_t beat = 0; int prio = -1; float t = 0; bool active = false;
    const char *gesture = nullptr, *emote = nullptr; float gt = 0;
    float idleIn = 0, aimed = 0, still = 0, fall = 0; Vector3 last{};
    int poison = 0, targetTurn = -1, nearTurn = -1, waitTurn = -1;
    bool threat = false, dead = false, blasted = false, air = false;
};
std::vector<Role> roles;
int lastClock = 0, turnNo = 0, shooter = -1;
bool fired = false, hurtFoe = false, hurtFriend = false, damageSaid = false, firstBlood = false, shortSaid = false;
Phase lastPhase = Phase::Aim;

uint32_t pick(const Game &g, int i, uint32_t salt) {
    uint32_t h = (g.cfg.seed ^ (salt * 0x9E3779B9u)) + uint32_t(i) * 40503u + uint32_t(g.clock) * 2654435761u;
    h ^= h >> 15, h *= 2246822519u, h ^= h >> 13, h *= 3266489917u;
    return h ^ (h >> 16);
}
bool has(const char *clip) { return Models::clipLength("worm", clip) > 0; }
bool control(const Game &g, int i) { return i == g.current && g.phase == Phase::Aim; }
bool playing(const Game &g, int i) { return i == g.current && g.phase != Phase::Settle && g.phase != Phase::GameOver; }  // its turn: no bystander role

void say(int team, V v) {
    static double free = 0;
    if (v == NO || GetTime() < free) return;
    Audio::voice(team, v);
    free = GetTime() + 0.9;  // one line at a time
}
void stop(Role &r) { r.tr = nullptr, r.prio = -1, r.gesture = r.emote = nullptr; }

void cast(const Game &g, int i, const Track &tr, int prio, bool active = false) {
    if (i < 0 || i >= (int)roles.size() || tr.empty() || !g.worms[i].alive) return;
    Role &r = roles[i];
    if ((r.tr && prio <= r.prio) || (playing(g, i) && !active)) return;
    r.tr = &tr, r.beat = 0, r.prio = prio, r.t = 0, r.active = active, r.gesture = r.emote = nullptr;
}
// Nearest living worm to p of `team` (friend) or of any other team, within maxD; -1 if none
int nearest(const Game &g, Vector3 p, int team, bool friendOf, float maxD, int except = -1) {
    int best = -1;
    for (int k = 0; k < (int)g.worms.size(); k++) {
        const Worm &w = g.worms[k];
        if (k == except || !w.alive || playing(g, k) || (w.team == team) != friendOf) continue;
        float d = Vector3Distance(w.pos, p);
        if (d < maxD) maxD = d, best = k;
    }
    return best;
}
void bystanders(const Game &g, int subject, const Scene &s, int prio, float maxD = 10) {
    const Worm &w = g.worms[subject];
    cast(g, nearest(g, w.pos, w.team, true, maxD, subject), s.friendT, prio - 1);  // a subject of the same trigger outranks them
    cast(g, nearest(g, w.pos, w.team, false, maxD, subject), s.foeT, prio - 1);
}
}  // namespace

void Acting::event(const Game &g, const GameEvent &e) {
    if (roles.size() != g.worms.size()) return;
    int team = e.worm >= 0 ? g.worms[e.worm].team : -1, cur = g.current < (int)g.worms.size() ? g.current : -1;
    int curTeam = cur >= 0 ? g.worms[cur].team : 0;
    switch (e.kind) {
    case GameEvent::TurnStart: turnNo++, fired = hurtFoe = hurtFriend = damageSaid = shortSaid = false, shooter = -1; break;
    case GameEvent::Fire: {
        if (e.worm < 0) break;  // sentry gun
        Kind k = WEAPONS[e.weapon].kind;
        if (k == Kind::SkipGo || k == Kind::Surrender) { cast(g, e.worm, SKIPGO, P_MISS, true); break; }
        if (utility(k) || k == Kind::Flood || k == Kind::Abduction) break;
        bool strike = k == Kind::Airstrike || k == Kind::Donkey, crit = true;
        fired = true, shooter = e.worm;
        for (int i = 0; i < (int)g.worms.size(); i++) {
            uint32_t h = pick(g, i, 1);
            if (g.worms[i].team != team) cast(g, i, strike ? (crit ? STRIKE_CRIT : STRIKE_FOE[h % 2]) : FIRED_FOE[h % 2], strike ? P_STRIKE : P_FIRED), crit = false;
            else if (strike && i != e.worm) cast(g, i, STRIKE_FRIEND, P_STRIKE);
        }
        break;
    }
    case GameEvent::Hurt: {
        for (const GameEvent &x : g.events) if (x.kind == GameEvent::TurnStart) return;  // poison tick
        if (cur < 0 || e.worm < 0) break;
        if (team == curTeam) {
            hurtFriend = true;
            if (e.worm != cur) cast(g, e.worm, DAMAGE.self, P_DAMAGE);
        } else {
            hurtFoe = true;
            if (!damageSaid) say(curTeam, V::Damage), damageSaid = true;
        }
        bystanders(g, e.worm, DAMAGE, P_DAMAGE, 8);
        break;
    }
    case GameEvent::Death:
        if (cur < 0 || e.worm < 0 || team == curTeam) break;
        say(curTeam, firstBlood ? V::EnemyDeath : V::FirstBlood);
        if (!firstBlood) cast(g, nearest(g, g.worms[cur].pos, curTeam, true, 10), CHEER, P_HIT);
        firstBlood = true;
        break;
    case GameEvent::Splash: if (e.worm >= 0) say(team, V::Drown); break;
    case GameEvent::Collect: if (e.worm >= 0) say(team, V::Collect); break;
    case GameEvent::CrateDrop: say(curTeam, V::CrateDrop); break;
    case GameEvent::GameOver:
        for (int i = 0; i < (int)g.worms.size(); i++)
            if (g.worms[i].team == g.winner) cast(g, i, VICTORY[pick(g, i, 2) % 3], P_WIN, true);
        break;
    default: break;
    }
}

void Acting::update(const Game &g, float dt, const std::vector<uint8_t> &busy) {
    size_t n = g.worms.size();
    if (roles.size() != n || g.clock < lastClock) {  // new match, or a replay rewound
        roles.assign(n, Role{});
        for (size_t i = 0; i < n; i++) roles[i].idleIn = 6 + i * 3.7f, roles[i].poison = g.worms[i].poison, roles[i].last = g.worms[i].pos;
        if (g.clock == 0) firstBlood = false;
        lastPhase = g.phase;
    }
    lastClock = g.clock;
    int cur = g.current < (int)n ? g.current : -1;

    // the turn's shot is over: a miss (no one hurt) or friendly fire only
    if (lastPhase != Phase::Settle && g.phase == Phase::Settle && fired && shooter >= 0 && g.worms[shooter].alive) {
        const Worm &s = g.worms[shooter];
        if (!hurtFoe && !hurtFriend) cast(g, nearest(g, s.pos, s.team, false, 1e9f), MISSED.foeT, P_MISS), cast(g, nearest(g, s.pos, s.team, true, 10, shooter), MISSED.friendT, P_MISS);
        else if (!hurtFoe) cast(g, shooter, MISTAKE.self, P_MISS, true), bystanders(g, shooter, MISTAKE, P_MISS);
        fired = false;
    }
    lastPhase = g.phase;

    for (size_t i = 0; i < n; i++) {
        const Worm &w = g.worms[i];
        Role &r = roles[i];
        if (!w.alive) { stop(r); continue; }
        if (r.tr && playing(g, i) && !r.active) stop(r);
        // the death queue reaches it: its last gesture (Death5, Death15 when poisoned) until the blast, friends mourn
        if (g.dying() == (int)i && !r.dead) {
            r.dead = true;
            cast(g, i, w.poison ? DEATH_SICK : DEATH[pick(g, i, 3) % 5], P_DEATH, true);
            cast(g, nearest(g, w.pos, w.team, true, 8, i), MOURN, P_MOURN);
        }
        if (w.poison > r.poison) cast(g, i, POISONED[pick(g, i, 4) % 2], P_HIT);
        r.poison = w.poison;
        // knocked flying sideways (Blasted10), heavy landing (WormBounce10)
        if (!w.grounded) {
            r.air = true, r.fall = fminf(r.fall, w.vel.y);
            if (w.vel.x * w.vel.x + w.vel.z * w.vel.z > 49 && !r.blasted) r.blasted = true, say(w.team, V::Nooo), bystanders(g, i, BLASTED, P_HIT, 15);
        } else if (r.air) {
            if (r.fall < -9) say(w.team, V::Bounce), bystanders(g, i, BOUNCE, P_HIT);
            r.air = r.blasted = false, r.fall = 0;
        }
        // lit fuse, armed mine or walking animal within its blast (TimedPayload*, Grenade*)
        float fuse = -1;
        bool grenade = false;
        for (const Projectile &s : g.shots) {
            const WeaponDef &d = WEAPONS[s.weapon];
            bool timed = d.kind == Kind::Shell && d.fuse > 0, animal = d.kind == Kind::Sheep || d.kind == Kind::SuperSheep || d.kind == Kind::OldWoman;
            if ((!timed && !animal) || (timed && !dropped(d) && Vector3Length(s.vel) > 2) || (int)i == cur) continue;
            if (Vector3Distance(s.pos, w.pos) < d.radius + 1) fuse = animal ? 2 : s.fuse, grenade = timed && !dropped(d);
        }
        for (const Object &o : g.objects)
            if (o.type == Object::Mine && o.fuse >= 0 && !o.dud && Vector3Distance(o.pos, w.pos) < 3) fuse = o.fuse;
        if (fuse >= 0 && !r.threat) {
            int sec = fuse > 4 ? 5 : fuse > 0 ? (int)ceilf(fuse) : 1;
            const Scene &s = sec == 5 ? PAYLOAD5[pick(g, i, 5) % 4] : PAYLOAD1_4[sec - 1];
            if (grenade) say(w.team, V::Grenade);
            cast(g, i, s.self, P_PAYLOAD);
            bystanders(g, i, s, P_PAYLOAD);
        }
        r.threat = fuse >= 0;
        // in the active worm's line of fire for half a second (Targeted*), once per turn; heading only, shots arc
        bool aimed = false;
        if (cur >= 0 && (int)i != cur && control(g, cur) && !g.hotSeat && !utility(WEAPONS[g.weapon].kind)) {
            const Worm &c = g.worms[cur];
            float dx = w.pos.x - c.pos.x, dz = w.pos.z - c.pos.z, len = sqrtf(dx * dx + dz * dz);
            aimed = len > 1 && len < 40 && (dx * sinf(c.yaw) + dz * cosf(c.yaw)) / len > 0.97f;
        }
        r.aimed = aimed ? r.aimed + dt : 0;
        if (r.aimed > 0.5f && r.targetTurn != turnNo)
            r.targetTurn = turnNo, cast(g, i, w.team == g.worms[cur].team ? TARGETED[2].self : TARGETED[pick(g, i, 6) % 2].self, P_TARGET);
        // the active worm walks up (Idle0: friends salute, Idle10: foes taunt)
        if (cur >= 0 && (int)i != cur && g.phase != Phase::Settle && r.nearTurn != turnNo && Vector3Distance(w.pos, g.worms[cur].pos) < 3) {
            r.nearTurn = turnNo;
            uint32_t h = pick(g, i, 7) % 3;
            cast(g, i, w.team == g.worms[cur].team ? NEAR_FRIEND[h] : NEAR_FOE[h], P_NEAR);
        }
        // active worm: idle 12 s (Waiting), 5 s left (ShortOnTime)
        r.still = Vector3Distance(w.pos, r.last) < 1e-3f ? r.still + dt : 0, r.last = w.pos;
        if ((int)i == cur && control(g, i) && !g.hotSeat) {
            if (r.still > 12 && r.waitTurn != turnNo) r.waitTurn = turnNo, say(w.team, V::Waiting);
            if (g.timer < 5 * 60 && !shortSaid) shortSaid = true, say(w.team, V::ShortOnTime);
        }
        // idle fidgets (Idle20, Bored0; Sick10 when poisoned)
        if (!r.tr && !busy[i] && w.grounded && !playing(g, i) && w.hp > 0 && (r.idleIn -= dt) <= 0) {
            uint32_t h = pick(g, i, 8);
            r.idleIn = 14 + h % 16;
            cast(g, i, w.poison ? SICK[h % 3] : IDLE[(h >> 8) % 9], P_IDLE);
        }
        // play the scene; gestures need the clip, emotes loop until replaced
        bool wait = i < busy.size() && busy[i];
        if (r.gesture && !wait && (r.gt += dt) >= Models::clipLength("worm", r.gesture)) r.gesture = nullptr;
        if (!r.tr || wait) continue;
        r.t += dt;
        while (r.tr && r.beat < r.tr->size() && (*r.tr)[r.beat].ms <= r.t * 1000) {
            const Beat &b = (*r.tr)[r.beat++];
            say(w.team, b.v);
            if (!b.clip) continue;
            if (!*b.clip) { stop(r); break; }
            bool emote = !strcmp(b.clip, "-");
            for (const char *e : EMOTES) emote |= !strcmp(b.clip, e);
            if (emote) r.emote = has(b.clip) ? b.clip : nullptr;
            else if (has(b.clip)) r.gesture = b.clip, r.gt = 0;
        }
    }
}

const char *Acting::clip(const Game &g, int i, float clock, float *t, bool *loop) {
    if (i < 0 || i >= (int)roles.size()) return nullptr;
    const Role &r = roles[i];
    if (r.gesture) return *t = r.gt, *loop = false, r.gesture;
    if (playing(g, i) && !r.active) return nullptr;
    const char *e = r.emote ? r.emote : g.worms[i].poison > 0 && g.worms[i].hp > 0 && has("Ill") ? "Ill" : nullptr;
    if (e) *t = clock + i * 1.3f, *loop = true;
    return e;
}
