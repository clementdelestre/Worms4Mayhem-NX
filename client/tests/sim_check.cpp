// Determinism check: two games fed the same seed and inputs must stay bit-identical.
// Each turn selects the next weapon in the table and uses it, so the whole arsenal gets exercised.
#define private public  // the cap test calls hurt() directly
#include "../src/sim.h"
#include "../src/navgrid.h"
#include "../src/controls.h"
#include "../src/ai.h"
#include "../src/ui.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

// Slow stick rates must survive the int8 rounding on average; a released stick sends 0 at once.
static void checkDiffuse() {
    float carry = 0;
    int sum = 0;
    for (int t = 0; t < 100; t++) sum += Controls::diffuse(0.3f, carry);
    assert(sum == 30);
    assert(Controls::diffuse(0, carry) == 0 && carry == 0);
    for (int t = 0; t < 10; t++) assert(Controls::diffuse(-300, carry) == -127);
}

static uint32_t run(std::vector<bool> &used) {
    Game g;
    g.start({42, 3, 2, "", 0}), g.hotSeat = 0;
    int turn = -1, tick = 0, want = 0;
    Phase prev = Phase::Settle;
    for (int t = 0; t < 60 * 600 && g.phase != Phase::GameOver; t++, tick++) {
        if (g.phase == Phase::Aim && prev != Phase::Aim) { turn++; tick = 0; want = turn % WEAPONS.size(); }
        prev = g.phase;
        if (tick == 220) want = 0;  // after a utility, finish the turn with the bazooka
        Input in;
        if (tick < 40) in.turn = 60;
        if (tick < 20) in.aim = turn % 3 ? 60 : -40;
        if (g.phase == Phase::Aim && g.weapon != want && !g.ammo[g.worms[g.current].team][want]) want = 0;
        bool selecting = g.phase == Phase::Aim && g.weapon != want;
        if (selecting) in.buttons = t % 2 ? Input::NEXT_WEAPON : 0;
        else if ((tick >= 60 && tick < 90) || (tick >= 100 && tick < 106) || (tick >= 260 && tick < 290)) in.buttons = Input::FIRE;
        if (tick >= 120 && tick < 200) in.walk = 127;
        if (tick == 210) in.buttons |= Input::JUMP;
        if (g.phase == Phase::Aim && !selecting && (tick == 60 || tick == 260)) used[g.weapon] = true;
        g.step(in);
    }
    int dead = 0;
    for (const Worm &w : g.worms) dead += !w.alive;
    printf("checksum %08x, %d turns, %d/%zu worms dead, phase %d\n", g.checksum(), turn + 1, dead, g.worms.size(), (int)g.phase);
    return g.checksum();
}

// Generic scripted run (turn, aim, fire, walk, jump, weapon-cycle) used to compare two runs of the same rule combo.
static uint32_t runRules(uint32_t rules, uint32_t seed, const Scheme &scheme = Scheme{}, uint32_t wormpot = 0) {
    Game g;
    GameConfig c{seed, 2, 2, "", rules};
    c.scheme = scheme;
    c.wormpot = wormpot;
    g.start(c), g.hotSeat = 0;
    for (int t = 0; t < 60 * 200 && g.phase != Phase::GameOver; t++) {
        int tick = t % 300;
        Input in;
        if (tick < 40) in.turn = 60;
        if (tick < 20) in.aim = 60;
        if (tick >= 60 && tick < 90) in.buttons = Input::FIRE;
        if (tick >= 120 && tick < 160) in.walk = 127;
        if (tick == 200) in.buttons |= Input::JUMP;
        if (tick == 0) in.buttons |= Input::NEXT_WEAPON;
        g.step(in);
    }
    return g.checksum();
}

static int clusterWeapon() {
    for (size_t i = 0; i < WEAPONS.size(); i++)
        if (WEAPONS[i].cradius > 0 && WEAPONS[i].cdamage > 0) return (int)i;
    return 0;
}

// Settle now, through its hp count and deaths, up to the next turn
static void endSettle(Game &g) {
    g.phase = Phase::Settle, g.timer = 1;
    for (int t = 0; t < 60 * 30 && g.phase == Phase::Settle; t++) g.step(Input{});
}

static void checkKing() {
    Game g;
    g.start({1, 2, 2, "", RULE_KING}), g.hotSeat = 0;
    g.worms[0].hp = 0;  // worm 0 of team 0 is the king
    std::vector<int> died;
    g.phase = Phase::Settle, g.timer = 1;
    for (int t = 0; t < 60 * 30 && g.phase == Phase::Settle; t++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death) died.push_back(e.worm);
    }
    // W4M Vital Worm (SurrenderTeamById 0x5abf26): the king dies, his team surrenders; its other worm stays, the match is over
    assert(died == std::vector<int>({0}) && g.surrendered[0] && g.worms[1].alive);
    assert(g.worms[2].alive && g.worms[3].alive && g.phase == Phase::GameOver && g.winner == 1);
}

// W4M death queue: dead worms of one count blow up one after another.
static void checkDeathQueue() {
    Game g;
    g.start({1, 3, 1, "", 0}), g.hotSeat = 0;
    for (int i = 0; i < 2; i++) g.worms[i].hp = 0, g.worms[i].pos = Vector3Add(g.worms[2].pos, {6.0f * i + 6, 0, 0});  // out of each other's blast
    std::vector<int> at;
    g.phase = Phase::Settle, g.timer = 1;
    for (int t = 0; t < 60 * 30 && g.phase == Phase::Settle; t++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death) at.push_back(t);
    }
    assert(at.size() == 2 && at[1] - at[0] == 1 + Game::COUNT_THROES);  // the next pops on the queue's next tick (0x4f9b30)
}

static void settle(Game &g);

// GameLogic.ApplyDamage: every display asks WormTrackCamera (0x5abeec) the same frame; the slot keeps the first (equal 3s) and is
// emptied at the next CMS update (0x51d5ad): one worm is visited, never the next. The first in clear view makes it 4: none.
static void checkCountCamera() {
    auto look = [](const Camera3D &c, Vector3 p) {
        Vector3 f = Vector3Normalize(Vector3Subtract(c.target, c.position)), to = Vector3Subtract(p, c.position);
        return Vector3DotProduct(f, to) > 0.9f * Vector3Length(to);
    };
    for (int clear = 0; clear < 2; clear++) {
        Game g;
        g.start({22, 2, 2, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        g.phase = Phase::Settle, g.countGroup = {1, 2}, g.countT = 0;
        Camera3D cam = clear ? Camera3D{Vector3Add(g.worms[1].pos, {0, 3, 8}), g.worms[1].pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE}
                             : Camera3D{{0, 60, 0}, {-10, 60, -10}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        bool one = false;
        for (int t = 0; t < 60; t++, g.countT++) {
            Controls::camera(cam, g, false, false, false, Game::DT);
            one |= look(cam, g.worms[1].pos);
            assert(!look(cam, g.worms[2].pos));
        }
        assert(clear || one);
    }
}


// W4M DrownFloat 0x5aa130: no hp count; the worm sinks, comes back up to 8 units under its feet, bobs 2000 ms with the camera
// on it, then blows up there on its own clock, the turn going on
static void checkDrownFloat() {
    Game g;
    g.start({1, 2, 2, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    int vi = (g.current + 2) % 4;  // a team mate of nobody playing: its team still stands
    Worm &d = g.worms[vi];
    d.pos = {d.pos.x, g.water + 0.6f, d.pos.z}, d.vel = {0, -8, 0}, d.grounded = false;
    const Vector3 at = d.pos;
    for (int k = 0; k < 1600 && g.terrain.solid({d.pos.x, d.pos.y - 1.5f, d.pos.z}); k++)  // open sea, the nearest along x or z
        d.pos = Vector3Add(at, Vector3Scale(k % 4 < 2 ? Vector3{1, 0, 0} : Vector3{0, 0, 1}, (k % 2 ? -0.1f : 0.1f) * (k / 4)));
    for (int t = 0; t < 30 && !d.drowned; t++) g.step(Input{});  // its feet 7 units under Water.Level (0x5ad640)
    assert(!d.alive && d.drowned && d.counted > 0 && g.dying() == vi);
    float low = d.pos.y;
    int died = -1, focus = 0, t = 0;
    for (; t < 60 * 20 && died < 0; t++) {
        g.step(Input{});
        low = fminf(low, d.pos.y), focus += g.dying() == vi;
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death && e.worm == vi) died = t;
    }
    assert(died >= Game::DROWN_FLOAT && focus >= Game::DROWN_FLOAT && low < g.water - 0.3f && g.phase == Phase::Aim);
    assert(fabsf(d.pos.y - Game::R - (g.water - 0.4f)) < 0.2f && d.counted == 0);  // popped at its float height, nothing left to draw
}

// W4M: each drowned worm blows up on its own DrownFloat clock (0x5aa130, set 2000 ms at its float height 0x5aa222), never
// through the death queue: two worms drowned by one blast pop one after the other, spaced as their surface arrivals
static void checkDrownPair() {
    Game g;
    g.start({1, 2, 2, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    int a = (g.current + 1) % 4, b = (g.current + 3) % 4;
    const Vector3 near = {g.worms[a].pos.x, g.water + 1.0f, g.worms[a].pos.z};
    Vector3 at = near;
    for (int k = 0; k < 4000 && (g.terrain.solid({at.x - 3, at.y - 3, at.z}) || g.terrain.solid({at.x + 3, at.y - 3, at.z})); k++)  // open sea
        at = Vector3Add(near, Vector3Scale(k % 4 < 2 ? Vector3{1, 0, 0} : Vector3{0, 0, 1}, (k % 2 ? -0.2f : 0.2f) * (k / 4)));
    g.worms[a].pos = Vector3Add(at, {-1.0f, 0, 0}), g.worms[b].pos = Vector3Add(at, {1.6f, 0, 0});
    for (int i : {a, b}) g.worms[i].vel = {}, g.worms[i].grounded = false;
    g.events.clear(), g.explode({at.x, at.y - 0.6f, at.z}, Game::MINE_BLAST);
    int up[2] = {-1, -1}, died[2] = {-1, -1};
    for (int t = 0; t < 60 * 20 && (died[0] < 0 || died[1] < 0); t++) {
        g.step(Input{});
        for (int j = 0; j < 2; j++) {
            const int i = j ? b : a;
            if (up[j] < 0 && g.worms[i].floatT > 0) up[j] = t;
            for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death && e.worm == i) died[j] = t;
        }
    }
    assert(up[0] >= 0 && up[1] >= 0 && up[0] != up[1]);
    for (int j = 0; j < 2; j++) assert(died[j] - up[j] == Game::DROWN_FLOAT - 1);
    Worm &s = g.worms[g.current];  // standing when the water reaches it: vy -0.03 units/ms (0x5ad91f), not its own
    assert(s.alive && s.grounded);
    g.water = s.pos.y - Game::R + 0.4f;
    g.step(Input{});
    assert(s.drowned && s.vel.y == -1.5f);
}

// W4M PostActivityTime: 2400 ms between the end of the settle and the next turn.
static void checkPostActivity() {
    GameConfig c{1, 2, 1, "", 0};
    c.scheme.crateChance = 0;  // DoPostActivity's DropRandomCrate would wait for the crate
    Game g;
    g.start(c), g.hotSeat = 0;
    settle(g);  // EndTurn: nothing active, StartPostActivity at once
    g.phase = Phase::Settle, g.timer = 1;
    int t = 0;
    for (; t < 600 && g.phase == Phase::Settle; t++) g.step(Input{});
    assert(t == 1 + Game::POST_ACTIVITY && g.phase == Phase::Aim && g.hotSeat == 10 * 60);
}

// Injects a cluster explosion directly into shots[] rather than going through use(), to isolate the damage rule.
static void checkKarma() {
    Game g;
    g.start({5, 2, 1, "", RULE_KARMA}), g.hotSeat = 0;
    int attacker = g.current, victim = 1 - attacker;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, clusterWeapon(), 0, true, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.worms[attacker].hp < 100);
    assert(g.worms[attacker].hp > g.worms[victim].hp);  // attacker's cut is smaller than the victim's hit
}

static void checkVampire() {
    Game g;
    g.start({6, 2, 1, "", RULE_VAMPIRE}), g.hotSeat = 0;
    int attacker = g.current, victim = 1 - attacker;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, clusterWeapon(), 0, true, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.worms[attacker].hp > 100);  // healed above full from damage dealt to an enemy
}

static void checkLowGravity() {
    Game gN, gL;
    gN.start({9, 2, 1, "", 0});
    gL.start({9, 2, 1, "", RULE_LOW_GRAVITY});
    Vector3 p = {5, (Terrain::NY - 1) * Terrain::VOX, 5};  // high corner, open air
    gN.worms[0].pos = gL.worms[0].pos = p;
    gN.worms[0].vel = gL.worms[0].vel = {0, 0, 0};
    gN.worms[0].grounded = gL.worms[0].grounded = false;
    gN.step(Input{});
    gL.step(Input{});
    assert(gL.worms[0].vel.y > gN.worms[0].vel.y);  // falls slower under low gravity
}

static void checkSuddenDeath() {
    Game g;
    GameConfig c{11, 2, 2, "", RULE_SUDDEN_DEATH};
    c.scheme.sdType = Scheme::SD_ONE_HP;
    g.start(c), g.hotSeat = 0;
    g.clock = g.cfg.scheme.roundTime * 3600;  // round time is up: the next turn flips sudden death
    float before = g.water;
    endSettle(g);
    assert(g.suddenDeath);
    for (const Worm &w : g.worms) if (w.alive) assert(w.hp == 1);
    assert(g.water > before);
}

static void checkRopeRace() {
    Game g;
    g.start({7, 2, 1, "", RULE_ROPE_RACE}), g.hotSeat = 0;
    int team = g.worms[g.current].team;
    g.worms[g.current].pos = g.raceFinish;
    g.step(Input{});
    assert(g.phase == Phase::GameOver && g.winner == team);
    g.step(Input{});
    assert(g.events.empty());  // GameOver (jingle, victory voice) is emitted once, not every tick
}

static void checkHighlander() {
    Game g;
    g.start({13, 2, 1, "", RULE_HIGHLANDER}), g.hotSeat = 0;
    int wi = clusterWeapon();
    g.ammo[0][wi] = 0, g.delays[0][wi] = 0;
    g.ammo[1][wi] = 2, g.delays[1][wi] = 0;
    g.shots = {{g.worms[1].pos, {0, 0, 0}, wi, 0, true, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});  // explode() tags lastHitTeam[1] with the attacker's (team 0) team
    g.worms[1].hp = 0;
    endSettle(g);
    assert(!g.worms[1].alive);
    assert(g.ammo[0][wi] == 1);  // killer's team inherits the victim's weapon
}

static void checkObjects() {
    Game g;
    g.start({17, 2, 1, "", 0}), g.hotSeat = 0;
    assert(!g.objects.empty());
    int wi = 0, team = g.worms[g.current].team;
    while (WEAPONS[wi].count < 0) wi++;  // a finite weapon, so the +1 shows
    int before = g.ammo[team][wi];
    Vector3 p = g.worms[g.current].pos;
    g.objects = {{Object::Crate, p, {0, 0, 0}, wi, -1, false, false}, {Object::Crate, p, {0, 0, 0}, -1, -1, false, false}};
    g.step(Input{});
    g.step(Input{});
    assert(g.objects.empty() && g.ammo[team][wi] == before + 1 && g.worms[g.current].hp == 125);

    Vector3 sky = {5, 50, 5};  // open air: nothing else in reach
    g.objects = {{Object::Barrel, sky, {0, 0, 0}, -1, -1, false, true}, {Object::Barrel, {7.5f, 50, 5}, {0, 0, 0}, -1, -1, false, false},
                 {Object::Barrel, {25, 50, 5}, {0, 0, 0}, -1, -1, false, false}};
    g.step(Input{});
    assert(g.objects.size() == 1);  // chain: the barrel in reach went off, the far one survives

    int victim = g.current, hp = g.worms[victim].hp;
    g.objects = {{Object::Mine, g.worms[victim].pos, {0, 0, 0}, -1, -1, false, false}};
    g.step(Input{});
    assert(g.objects.size() == 1 && g.objects[0].fuse > 0);
    for (int t = 0; t < 600 && !g.objects.empty(); t++) {
        if (g.objects[0].dud) g.objects[0] = {Object::Mine, g.worms[victim].pos, {0, 0, 0}, -1, 1, false, false};  // fizzled: lay another
        g.step(Input{});
    }
    assert(g.objects.empty() && g.worms[victim].hp < hp);
}

static int weaponNamed(const char *n) {
    for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].name == n) return (int)i;
    assert(!"weapon missing from weapons.json");
    return 0;
}

// One scripted turn per weapon (charge, release, steer, detonate): its Fire event must show up.
static uint32_t fireEach(int wi, bool &fired, const GameConfig *cfg = nullptr) {
    Game g;
    g.start(cfg ? *cfg : GameConfig{99u + wi, 2, 2, "", 0}), g.hotSeat = 0;
    g.ammo[g.worms[g.current].team][wi] = 1, g.delays[g.worms[g.current].team][wi] = 0;  // not about the scheme delays
    g.weapon = wi;
    fired = false;
    for (int t = 0; t < 60 * 30 && g.phase != Phase::GameOver; t++) {
        Input in;
        if (t < 20) in.aim = 50;
        if (t < 30 || t > 120) in.buttons = Input::FIRE;
        if (t > 60) in.turn = 40;
        if (t > 125) in.buttons = t % 20 < 10 ? Input::FIRE : 0;
        g.step(in);
        for (const GameEvent &e : g.events) fired = fired || (e.kind == GameEvent::Fire && e.weapon == wi);
    }
    return g.checksum();
}

static void settle(Game &g);

// W4M Poison Arrow (WEAPTWK): WormImpactDamage 25 is read by no code, ExplosionMessage radii 0, DetonatesOnWormImpact 1 / OnLandImpact 0,
// ArmOnImpact 1, PreDetonationTime 2000: a worm hit detonates at once into the gas cloud; land stops it, the cloud comes 2 s later. No damage, no knock.
static void checkPoison() {
    Game g;
    g.start({21, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.objects.clear();  // no mine or drum where the arrow lands
    int victim = 1 - g.current, hp = g.worms[victim].hp, wi = weaponNamed("Poison Arrow");
    Worm &v = g.worms[victim];
    Vector3 v0 = v.vel;
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{v.pos, {0, 0, 0}, wi, 0, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.shots.empty() && g.gas.size() == 1 && v.poison == (int)WEAPONS[wi].poison);  // the hit worm is in the cloud
    assert(v.hp == hp && Vector3Distance(v.vel, v0) < 1e-3f);                                // no direct damage, no knock-back
    g.gas.clear(), v.poison = 0;
    Vector3 hit, from = Vector3Add(v.pos, {12, 20, 0});  // land: stops (damping 0), then PreDetonationTime
    assert(g.terrain.raycast({from, {0, -1, 0}}, 80, &hit));
    g.shots = {{Vector3Add(hit, {0, 1, 0}), {0, -20, 0}, wi, 0, false, 1}};
    g.shots[0].touching = 0;
    bool armed = false;
    for (int t = 0; t < 60 && !(g.shots.empty() && !armed); t++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) armed = armed || e.kind == GameEvent::Arm;
        if (armed) break;
    }
    assert(armed && g.shots.size() == 1 && g.shots[0].stage == 1 && Vector3Length(g.shots[0].vel) == 0 && g.gas.empty());
    for (int t = 0; t < msTicks(2000) - 2; t++) g.step(Input{});
    assert(g.shots.size() == 1 && g.gas.empty());  // 2 s on, not before
    for (int t = 0; t < 4; t++) g.step(Input{});
    assert(g.shots.empty() && g.gas.size() == 1 && g.worms[victim].hp == hp);
    v.poison = 0;
    g.shots = {{v.pos, {0, 0, 0}, wi, 0, false, 1}};
    g.shots[0].touching = 0, g.gas.clear(), g.step(Input{});
    g.phase = Phase::Settle, g.timer = 300;
    while (g.phase == Phase::Settle) g.step(Input{});
    assert(g.worms[victim].hp == hp - (int)WEAPONS[wi].poison);  // DoPostActivity's ApplyPoison, before the next turn
}

static void settle(Game &g);

// Victim placed `ahead` m in front of the settled attacker, `up` m higher; returns the victim after the swing.
static Worm melee(const char *weapon, float ahead, float up = 0, uint32_t wormpot = 0) {
    Game g;
    GameConfig c{25, 2, 1, "", 0};
    c.wormpot = wormpot;
    g.start(c), g.hotSeat = 0;
    settle(g);
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    v.pos = Vector3Add(a.pos, {sinf(a.yaw) * ahead, up, cosf(a.yaw) * ahead});
    v.vel = {0, 0, 0};
    a.pitch = 0.3f;
    g.weapon = weaponNamed(weapon);
    Input in;
    in.buttons = Input::FIRE;
    g.step(in);
    assert(g.phase != Phase::Aim);  // the swing ends the attack
    return v;
}

static void checkMelee() {
    Worm v = melee("Fire Punch", 1);
    assert(v.hp == 100 - (int)WEAPONS[weaponNamed("Fire Punch")].damage && v.vel.y > 10);  // uppercut: launched up
    assert(melee("Fire Punch", 0.4f, -0.6f).hp < 100);  // touching, a bit lower on a slope
    assert(melee("Fire Punch", 1, 2).hp < 100);         // above: the punch leaps
    assert(melee("Fire Punch", 3).hp == 100 && melee("Fire Punch", -1).hp == 100);  // out of reach, behind
    v = melee("Baseball Bat", 1.6f);
    assert(v.hp < 100 && Vector3Length(v.vel) > 10);  // knocked away along the aim
    v = melee("Prod", 1);
    assert(v.hp == 100 && Vector3Length(v.vel) > 3 && Vector3Length(v.vel) < 10);  // small push, no damage (W4M 0)
}

// A worm hit by a gun takes the weapon's full damage, not a blast falloff.
static void checkShotgun(uint32_t wormpot = 0) {
    Game g;
    GameConfig c{25, 2, 1, "", 0};
    c.wormpot = wormpot;
    g.start(c), g.hotSeat = 0;
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    a.pos = {10, 55, 10}, v.pos = {10.4f, 55, 20};  // open sky, off-centre hit
    a.yaw = 0, a.pitch = 0;
    g.weapon = weaponNamed("Shotgun");
    Input in;
    in.buttons = Input::FIRE;
    g.step(in);
    assert(v.hp == 100 - (int)WEAPONS[g.weapon].damage * (g.wp(WP_SUPER_FIREARMS) ? 2 : 1));
}

// Homing missile: flies off along the aim, then dives onto the reticle point.
static void checkHoming() {
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    g.hotSeat = 0;
    a.pos = {20, 55, 20}, a.yaw = 0, a.pitch = 0, a.grounded = true;  // open sky: the reticle falls on the ground 30 m ahead
    g.weapon = weaponNamed("Homing Missile");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input in;
    in.buttons = Input::FIRE;
    for (Input i : {in, Input{}}) g.step(i), a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = true;  // the first press locks the aim ray's target
    for (int t = 0; t < 89; t++) g.step(in), a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = true;  // held standing: W4M CanFire
    v.pos = Vector3Add(g.target(), {0, Game::R + 0.05f, 0}), v.vel = {0, 0, 0};
    g.step(in);  // full charge fires
    assert(g.phase == Phase::Flying);
    while (!g.shots.empty()) g.step(Input{});
    assert(v.hp <= 100 - 40 || !v.alive);
}

// W4M: each team gets back the weapon it last had in hand, or the next one with ammo.
static void checkTeamWeapon() {  // the last weapon is only remembered (AI PreferVariety); each turn starts empty-handed
    Game g;
    g.start({39, 2, 1, "", 0}), g.hotSeat = 0;
    int t0 = g.worms[g.current].team, bat = weaponNamed("Baseball Bat"), sheep = weaponNamed("Sheep");
    auto endTurn = [&] { endSettle(g); };
    g.weapon = sheep;
    endTurn();
    assert(g.worms[g.current].team != t0 && g.weapon == -1 && g.picked[t0] == sheep);
    g.weapon = bat;
    endTurn();
    assert(g.worms[g.current].team == t0 && g.weapon == -1 && g.picked[1 - t0] == bat);
    uint32_t sum = g.checksum();
    g.picked[t0] = bat;
    assert(g.checksum() != sum);
}

// Walking into a crate (real input, no teleport) collects it: health heals, a weapon adds ammo.
static void checkCrateWalk() {
    for (int weapon : {-1, weaponNamed("Sheep")}) {
        GameConfig c{31, 2, 1, "", 0};
        c.scheme.crateChance = c.scheme.mines = c.scheme.barrels = 0;
        Game g;
        g.start(c), g.hotSeat = 0;
        settle(g);
        Worm &a = g.worms[g.current];
        Vector3 hit, p = Vector3Add(a.pos, {sinf(a.yaw) * 3, 4, cosf(a.yaw) * 3});
        assert(g.terrain.raycast({p, {0, -1, 0}}, 12, &hit));
        g.objects = {{Object::Crate, {hit.x, hit.y + 0.5f, hit.z}, {0, 0, 0}, weapon, -1, false, false}};
        int hp = a.hp, ammo = weapon < 0 ? 0 : g.ammo[a.team][weapon];
        bool got = false;
        Input in;
        in.walk = 127;
        for (int t = 0; t < 180 && !got; t++) {
            g.step(in);
            for (const GameEvent &e : g.events) got |= e.kind == GameEvent::Collect && e.worm == g.current && e.weapon == weapon;
        }
        assert(got && g.objects.empty());
        assert(weapon < 0 ? a.hp == hp + g.cfg.scheme.crateHealth : g.ammo[a.team][weapon] == ammo + 1);
    }
}

static void checkSniper() {
    Game g;
    g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    a.pos = {10, 55, 10}, v.pos = {10, 55, 30};  // open sky, 20 m apart
    a.yaw = 0, a.pitch = 0;
    g.weapon = weaponNamed("Sniper Rifle");
    Input in;
    in.buttons = Input::FIRE;
    g.step(in);
    assert(v.hp <= 100 - (int)WEAPONS[g.weapon].damage + 1);
    // W4M 0x55ea22: an ExplosionMessage whose push centre sits (2dx, 2, 2dz) units behind the worm: 0.1 u/ms x 1.2 x (40 - 2.83) / 40 = 5.58 m/s along (0, 1, 1) / sqrt 2
    assert(!v.grounded && fabsf(v.vel.z - 3.94f) < 0.15f && fabsf(v.vel.y - 3.94f) < 0.4f && fabsf(v.vel.x) < 0.01f);
    bool boom = false;
    for (const GameEvent &e : g.events) boom |= e.kind == GameEvent::Boom && e.weapon == g.weapon;  // drawn as WXP_ShotgunBlast (fx.cpp), not a BigBoom
    for (const GameEvent &e : g.events) assert(e.kind != GameEvent::BigBoom);
    assert(boom);
}

// W4M Challenge.EndlessGun (0x55efbf): the sniper keeps firing, one ammo spent, the turn goes on.
static void checkEndlessGun() {
    Game g;
    g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    a.pos = {10, 55, 10}, a.yaw = 0, a.pitch = 0;
    g.endlessGun = true, g.weapon = weaponNamed("Sniper Rifle");
    int &ammo = g.ammo[a.team][g.weapon];
    ammo = 3;
    for (int k = 0; k < 3; k++) {
        Input in;
        in.buttons = Input::FIRE;
        g.step(in), g.step(Input{});
    }
    assert(ammo == 2 && g.phase == Phase::Aim);
}

// W4M MineFactoryLogicEntity: a blast within LandDamageRadius + 40 units of pos + (0, 40, 0) blows it up 100 ms later (0x5cfbc0, 0x5cfc70).
static void checkFactoryBlast() {
    Game g;
    g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
    g.factoryCreate({20, 60, 20});
    assert(g.factory.on);
    g.shots = {{Vector3Add(g.factory.pos, {0, 4, 0}), {0, 0, 0}, weaponNamed("Dynamite"), 0.01f, false, 1}};
    int t = 0;
    while (!g.factory.damaged && t++ < 10) g.step(Input{});
    assert(g.factory.damaged && g.factory.on);
    for (int k = 0; k < 7; k++) g.step(Input{});
    assert(!g.factory.on);
}

// W4M gun hit on land (0x55d8c0 clears one voxel, 0x55e5da LandDamageRadius 0): only the hit cell changes, never a blast crater.
static void floorAndWall(Game &g, float wall);
static void checkGunLand() {
    for (const char *name : {"Shotgun", "Sniper Rifle"}) {
        Game g;
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        floorAndWall(g, 0);
        Worm &w = g.worms[g.current];
        w.pos = {20, 50.6f, 12}, w.vel = {}, w.yaw = PI / 2, w.pitch = -0.6f;
        g.weapon = weaponNamed(name);
        for (int t = 0; t < 30; t++) g.step(Input{});
        std::vector<signed char> before = g.terrain.d;
        Input fire;
        fire.buttons = Input::FIRE;
        g.step(fire);
        Vector3 hit = {};
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom) hit = e.pos;
        assert(hit.y > 49 && hit.y < 51.5f);
        int n = 0;
        float far = 0;
        for (size_t i = 0; i < before.size(); i++)
            if (before[i] != g.terrain.d[i]) {
                Vector3 p = {(float)(i % Terrain::NX) * Terrain::VOX, (float)(i / Terrain::NX % Terrain::NY) * Terrain::VOX, (float)(i / Terrain::NX / Terrain::NY) * Terrain::VOX};
                n++, far = fmaxf(far, Vector3Distance(p, hit));
            }
        printf("%s on land: %d voxels changed, farthest %.2f m from the hit\n", name, n, far);
        fflush(stdout);
        assert(n > 0 && n < 200 && far < 1.4f);  // the hit cell (ours: 0.5 m sphere), not the old 0.4 / 0.8 m craters' reach
    }
}

// Sniper at a worm just over a crest, aimed like a player: target moved to the scope camera's screen centre.
static void checkScopeCrest() {
    Game g;
    g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    a.pos = {10, 55, 10}, v.pos = {10, 55, 30};
    for (int z = 100; z <= 104; z++)  // lip at z 25..26 topping out 0.2 m under the worm-to-worm line
        for (int y = 0; y < Terrain::NY; y++)
            for (int x = 30; x <= 50; x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp((54.8f - y * Terrain::VOX) * Terrain::Q, -127, 127);
    for (int i = 0; i < 8; i++) {
        Vector3 d = Vector3Normalize(Vector3Subtract(v.pos, Controls::eye(g)));
        a.yaw = atan2f(d.x, d.z), a.pitch = asinf(d.y);
    }
    g.weapon = weaponNamed("Sniper Rifle");
    Input in;
    in.buttons = Input::FIRE;
    g.step(in);
    assert(v.hp <= 100 - (int)WEAPONS[g.weapon].damage + 1);
}

static void checkSentry() {
    Game g;
    g.start({27, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &w = g.worms[g.current];
    w.pos = {10, 55, 10};
    Object sentry = {Object::Sentry, {10, 55, 14}, {0, 0, 0}, weaponNamed("Sentry Gun"), -1, false, false, 1 - w.team};
    g.objects = {sentry};
    g.step(Input{});
    assert(w.hp < 100 && g.objects[0].fuse > 0);  // shot the enemy in range, now reloading
}

// W4M SheepChaseCamera: behind and above the sheep (it rises when the land hides it), never under it.
static void checkSheepCamera() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.weapon = weaponNamed("Sheep");
    Controls::reset();
    Camera3D cam = {{0, 30, 0}, g.worms[g.current].pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    Camera3D pip;
    auto view = [&]() -> const Camera3D & {  // the chase camera: the inset while the worm is active (0x51c000), then full screen
        float show, grow;
        return Controls::inset(pip, show, grow) ? pip : cam;
    };
    int checked = 0;
    for (int t = 0; t < 60 * 8 && g.phase == Phase::Flying; t++) {
        g.step(Input{});
        bool chase = !g.shots.empty();
        Controls::camera(cam, g, chase, false, false, Game::DT);
        if (chase && t > 30) assert(view().position.y > g.shots[0].pos.y), checked++;
    }
    assert(checked > 60);
    for (int t = 0; t < 60 * 40 && !g.shots.empty(); t++) g.step(Input{}), Controls::camera(cam, g, !g.shots.empty(), false, false, Game::DT);
    assert(g.shots.empty() && g.phase != Phase::Aim);
    Controls::camera(cam, g, false, false, false, Game::DT);
    Vector3 frozen = view().position;  // ChaseCam has no Finished (vtable slot 7 = 0x49b8f0): it stays until another event or the next turn
    const Vector3 worm = g.worms[g.current].pos;
    float first = -1;
    for (int t = 0; t < 30 && g.phase != Phase::Aim; t++) {  // the drawn view only settles onto it (0.1 an update), it does not go back to the worm
        const Vector3 was = view().position;
        g.step(Input{}), Controls::camera(cam, g, false, false, false, Game::DT);
        const float moved = Vector3Distance(view().position, was);
        first = first < 0 ? moved : first;
        assert(g.phase == Phase::Aim || (moved <= first + 1e-3f && Vector3Distance(view().position, worm) > Vector3Distance(frozen, worm) - 0.5f));
    }
}

// W4M event cameras (docs/camera.md): worm, crate and winner TrackCams cut next to their target, homing FlyCam stays behind
// the missile, the shoulder camera only zooms in when ~all its occlusion rays are blocked.
static void checkEventCameras() {
    auto away = [] { return Camera3D{{0, 60, 0}, {-10, 60, -10}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE}; };  // looks off the map
    auto inView = [](const Camera3D &c, Vector3 p) {
        Vector3 f = Vector3Normalize(Vector3Subtract(c.target, c.position)), to = Vector3Subtract(p, c.position);
        return Vector3DotProduct(f, to) > 0.8f * Vector3Length(to);
    };
    Camera3D pipV;
    auto shown = [&](const Camera3D &c) -> const Camera3D & {  // served while the worm is active, an event camera is the inset (0x51c000)
        float show, grow;
        return Controls::inset(pipV, show, grow) ? pipV : c;
    };
    {  // a worm knocked ballistic off screen: WormTrackCamera cut, then kept in view
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        g.phase = Phase::Retreat, g.timer = g.retreatTicks(WEAPONS[g.weapon]);
        Worm &w = g.worms[1 - g.current];
        w.vel = {6, 9, 0}, w.grounded = false;
        Camera3D cam = away();
        int seen = 0, n = 0;
        for (int t = 0; t < 60 * 3 && !w.grounded; t++, n++) {
            Controls::camera(cam, g, false, false, false, Game::DT);
            if (t == 6) assert(Vector3Distance(shown(cam).position, {0, 60, 0}) > 5 && inView(shown(cam), w.pos));  // 2 ViewPoints tried a frame
            seen += inView(shown(cam), w.pos);
            g.step(Input{});
        }
        assert(n > 20 && seen > n * 8 / 10);
    }
    {  // drawn up never rolls the view: no degenerate (view ∥ up) frame entering, in and leaving a straight-down Blimp, nor in a crate's fall
        Game g;
        g.start({43, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        g.weapon = weaponNamed("Airstrike");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        Camera3D cam = away();
        auto sane = [&] {
            Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position)), u = Controls::viewUp(cam);
            assert(cam.up.y >= -1e-3f && fabsf(Vector3DotProduct(u, f)) < 1e-3f && Vector3Length(u) > 0.99f);  // XCamera 0x6e1d6c: never degenerate
        };
        auto level = [&] {  // roll 0: the view's right is horizontal once the camera's up is (0, 1, 0)
            Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position)), r = Vector3CrossProduct(f, Controls::viewUp(cam));
            return fabsf(cam.up.x) < 1e-3f && fabsf(cam.up.z) < 1e-3f && fabsf(r.y) < 1e-3f;
        };
        Input in;
        in.buttons = Input::TARGET;
        for (int t = 0; t < 400; t++) {
            if (t == 200) in = Input{};
            if (t < 200) in.aim = -127;
            g.step(in), Controls::camera(cam, g, false, false, false, Game::DT), sane();
        }
        const Vector3 tilt = Vector3Normalize({0.5f, 0.8f, 0.3f});  // an up a TrackCam inherits (0x5337c0), eased back at UpSpeed (0x533b25)
        cam.up = tilt;
        g.objects.push_back({Object::Crate, Vector3Add(a.pos, {0, 40, 0}), {0, 0, 0}, -1, -1, true, false});
        for (int t = 0; t < 200; t++) {
            g.objects.back().pos.y -= 0.2f;
            Controls::focus(&g.objects.back().pos, 0, true), Controls::camera(cam, g, false, false, false, Game::DT), sane();
            if (t >= 30) assert(level());  // CrateTrackCamera UpSpeed 0.2
        }
        for (int t = 0; t < 60; t++) Controls::camera(cam, g, false, false, false, Game::DT), sane();
        cam = away(), cam.up = tilt;
        g.phase = Phase::Flying;
        g.shots.push_back({{a.pos.x, a.pos.y + 12, a.pos.z}, {25 * sinf(a.yaw), 6, 25 * cosf(a.yaw)}, weaponNamed("Bazooka"), 0, false, 1});
        g.shots.back().touching = 0;
        for (int t = 0; t < 60 * 5 && !g.shots.empty(); t++) {
            Controls::camera(cam, g, true, false, false, Game::DT), sane();
            if (t >= 30) assert(level());  // PayloadTrackCamera UpSpeed 0.8
            g.step(Input{});
        }
    }
    {  // CrateTrackCamera: starts off screen, cut to a crate ViewPoint (15-25 m); a crate drops with no worm active: full screen
        Game g;
        g.start({43, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        g.phase = Phase::Settle, g.timer = 1;
        Controls::reset();
        Worm &w = g.worms[g.current];
        g.objects.push_back({Object::Crate, Vector3Add(w.pos, {3, 12, 0}), {0, 0, 0}, -1, -1, true, false});
        Camera3D cam = away();
        for (int t = 0; t < 6; t++) Controls::focus(&g.objects.back().pos, 0, true), Controls::camera(cam, g, false, false, false, Game::DT);
        float d = Vector3Distance(cam.position, g.objects.back().pos);
        assert(d > 10 && d < 26 && inView(cam, g.objects.back().pos));
        Vector3 at = cam.position, f0 = Vector3Subtract(cam.target, cam.position);  // then fixed, ViewPoints round its landing point: only the pitch follows it down
        for (int t = 0; t < 120; t++) {
            g.objects.back().pos.y -= 0.06f;
            Controls::focus(&g.objects.back().pos, 0, true), Controls::camera(cam, g, false, false, false, Game::DT);
            Vector3 f = Vector3Subtract(cam.target, cam.position);
            assert(Vector3Distance(cam.position, at) < 0.3f && fabsf(remainderf(atan2f(f.x, f.z) - atan2f(f0.x, f0.z), 2 * PI)) < 0.05f);
        }
    }
    {  // PiP: a shot served while the worm is active is the inset, the worm standing still (a CPU shot; 0x51c000, 0x51c6e0);
       // GameLogic.EndTurn deactivates the worm and the inset grows full screen (0x51e382, FullScreenTime 500 ms)
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        Camera3D cam = away();
        for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        g.phase = Phase::Retreat, g.timer = 100;
        g.shots.push_back({{a.pos.x, a.pos.y + 12, a.pos.z}, {25 * sinf(a.yaw), 6, 25 * cosf(a.yaw)}, weaponNamed("Bazooka"), 0, false, 1});
        g.shots.back().touching = 0;
        int inset = 0, grew = 0, end = -1;
        for (int t = 0; t < 60 * 3; t++) {
            Controls::camera(cam, g, !g.shots.empty(), false, false, Game::DT);
            Camera3D v;
            float show, grow;
            bool in = Controls::inset(v, show, grow);
            if (end < 0 && g.phase == Phase::Settle) end = t;
            if (end < 0) {
                inset += in;
                if (t > 30) assert(in && grow == 0 && Vector3Length(a.vel) < 1e-3f && Vector3Distance(cam.target, a.pos) < 3);  // the main view stays the worm's
            } else if (t < end + 25) grew += in && grow > 0;
            else if (t > end + 40) assert(!in && Vector3Distance(cam.target, a.pos) > 3);
            g.step(Input{});
        }
        assert(end > 90 && inset > 60 && grew > 10);
    }
    {  // a camera served during the PiP's slide-out stays hidden after PiP.GoneOffScreen (0x5228c5) until the next serve
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        Camera3D cam = away();
        for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        g.phase = Phase::Retreat, g.timer = 60 * 60;
        g.shots.push_back({{a.pos.x, a.pos.y + 12, a.pos.z}, {25 * sinf(a.yaw), 6, 25 * cosf(a.yaw)}, weaponNamed("Bazooka"), 0, false, 1});
        g.shots.back().touching = 0;
        float last = 0;
        int t = 0, off = -1;
        Camera3D v;
        for (; t < 60 * 10 && off < 0; t++) {
            Controls::camera(cam, g, !g.shots.empty(), false, false, Game::DT);
            float show, grow;
            bool in = Controls::inset(v, show, grow);
            if (in && g.shots.empty() && show < last - 1e-4f) off = t;  // PiP.SlideOff after the TrackCam's RestTime
            last = in ? show : 0;
            if (off < 0) g.step(Input{});
        }
        assert(off > 0 && g.phase == Phase::Retreat);
        g.shots.push_back({{a.pos.x, a.pos.y + 15, a.pos.z}, {3 * sinf(a.yaw), 0, 3 * cosf(a.yaw)}, weaponNamed("Sheep"), 0, false, 1});
        for (int k = 0; k < 90; k++) {  // the ChaseCam serve during the slide-out: never an inset of its own, never the main view
            Controls::camera(cam, g, true, false, false, Game::DT);
            float show, grow;
            bool in = Controls::inset(v, show, grow);
            if (k > 35) assert(!in && Vector3Distance(cam.target, a.pos) < 3);
        }
        Vector3 crate = Vector3Add(a.pos, {3, 12, 0});
        int in = 0;
        for (int k = 0; k < 10; k++) {  // the next serve slides the PiP on again
            Controls::focus(&crate, 0, true), Controls::camera(cam, g, true, false, false, Game::DT);
            float show, grow;
            in += Controls::inset(v, show, grow);
        }
        assert(in > 5);
    }
    {  // DefaultCam activation 0x52da00 at turn start: the last view's heading (ResetYaw 0), searched round land (0x530c00, table 0x91ed78)
        for (bool wall : {false, true}) {
            Game g;
            g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
            settle(g);
            Worm &a = g.worms[g.current], &b = g.worms[1 - g.current];
            a.yaw = 0, b.yaw = PI / 2;
            Controls::reset();
            Camera3D cam = away();
            for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
            if (wall) g.terrain.weld(Vector3Add(b.pos, {0, 1, -4}), {2, 4, 0.3f});  // blocks the spot behind on heading 0, not the one at +pi/4
            g.current = 1 - g.current, cam = away();
            Controls::camera(cam, g, false, false, false, Game::DT);  // off screen: a cut onto the logical view
            Vector3 d = Vector3Subtract(cam.target, cam.position);
            assert(fabsf(remainderf(atan2f(d.x, d.z) - (wall ? PI / 4 : 0), 2 * PI)) < 0.1f);
        }
    }
    {  // no PiP once the turn is over, and no return to the shooter between the shot's camera and the count (W4M: straight to it)
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        Camera3D cam = away();
        g.phase = Phase::Flying;
        g.shots.push_back({{a.pos.x, a.pos.y + 12, a.pos.z}, {25 * sinf(a.yaw), 6, 25 * cosf(a.yaw)}, weaponNamed("Bazooka"), 0, false, 1});
        g.shots.back().touching = 0;
        for (int t = 0; t < 60 * 6 && !g.shots.empty(); t++) Controls::camera(cam, g, true, false, false, Game::DT), g.step(Input{});
        g.phase = Phase::Settle;
        for (int t = 0; t < 60 * 3; t++) {
            Controls::camera(cam, g, false, false, false, Game::DT);
            float show, full;
            Camera3D v;
            if (!Controls::inset(v, show, full)) assert(Vector3Distance(cam.target, a.pos) > 3);
        }
    }
    for (int k : {0, 1, 2}) {  // the ChaseCam serve 0x51d5e0 slides the PiP on as any serve: walking, jetting (PhysicsOverride bit 0 only sets +0x2c0) or roped
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        Camera3D cam = away();
        for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        g.phase = Phase::Retreat, g.timer = g.retreatTicks(WEAPONS[g.weapon]), g.jetting = k == 1, g.roped = k == 2;
        g.shots.push_back({{a.pos.x, a.pos.y + 12, a.pos.z}, {3 * sinf(a.yaw), 0, 3 * cosf(a.yaw)}, weaponNamed(k == 2 ? "Bazooka" : "Sheep"), 0, false, 1});
        int inset = 0;
        for (int t = 0; t < 60; t++) {
            a.vel = k == 2 ? Vector3{} : Vector3{1, 0, 0};
            Controls::camera(cam, g, true, false, false, Game::DT);
            Camera3D v;
            float show, grow;
            inset += Controls::inset(v, show, grow);
            if (t > 30) assert(Vector3Distance(cam.target, a.pos) < 3);  // the main view stays the worm's
        }
        assert(inset > 50);
    }
    for (const char *pet : {"Sheep", "Inflatable Scouser"}) {  // ChaseCam start: Sheep keeps the view's yaw, ResetYaw (Scouser) goes behind its heading
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        Camera3D cam = away();
        for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        float h = a.yaw + PI / 2;  // heading sideways to the thrower's (and the view's) yaw
        g.phase = Phase::Retreat, g.timer = 600;
        g.shots.push_back({{a.pos.x, a.pos.y + 15, a.pos.z}, {3 * sinf(h), 0, 3 * cosf(h)}, weaponNamed(pet), 0, false, 1});
        Controls::camera(cam, g, true, false, false, Game::DT);
        Camera3D v;
        float show, grow;
        assert(Controls::inset(v, show, grow));  // pipView is the chase camera as placed at the serve
        Vector3 d = Vector3Subtract(v.position, g.shots[0].pos);
        float want = pet[0] == 'S' ? a.yaw : h, yaw = atan2f(-d.x, -d.z);
        assert(fabsf(remainderf(yaw - want, 2 * PI)) < 0.15f);
    }
    {  // DonkeyCamera, a SimpleCam: never Finished, it holds its last view once the donkey is gone (until another camera or the next turn)
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        Camera3D cam = away();
        g.phase = Phase::Settle, g.timer = 1;
        g.shots.push_back({{a.pos.x + 10, a.pos.y + 20, a.pos.z}, {0, -5, 0}, weaponNamed("Concrete Donkey"), 0, false, 1 << 30});
        for (int t = 0; t < 120; t++) g.shots[0].pos.y -= 0.05f, Controls::camera(cam, g, true, false, false, Game::DT);
        Vector3 pos = cam.position, look = Vector3Add(g.shots[0].pos, {0, 5, 0});
        g.shots.clear();
        for (int t = 0; t < 60 * 4; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        assert(Vector3Distance(cam.position, pos) < 0.01f && Vector3Distance(cam.target, look) < 0.1f);
    }
    {  // a donkey dropped from the Blimp: served while the worm is active, so in the PiP (0x51c000), grown at once by the EndTurn of its
       // 0 ms retreat (0x51e382); DonkeyCamera fixed at spawn + (0, -500, 500) units, default lens, up (0, 1, 0), the donkey in frame
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        g.weapon = weaponNamed("Concrete Donkey");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        Camera3D cam = {Vector3Add(a.pos, {0, 5, -8}), a.pos, {0, 1, 0}, 30, CAMERA_PERSPECTIVE};  // a zoomed lens before
        Input in;
        in.buttons = Input::TARGET;
        for (int t = 0; t < 30; t++) g.step(in), Controls::camera(cam, g, false, false, false, Game::DT);
        in.buttons = Input::TARGET | Input::FIRE;
        for (int t = 0; t < 60 && (g.step(in), g.shots.empty()); t++) Controls::camera(cam, g, false, false, false, Game::DT);
        assert(!g.shots.empty() && g.phase != Phase::Aim);
        Vector3 spawn = g.shots[0].pos, eye = Vector3Add(spawn, {0, -Game::DONKEY_EXTRA, 25});
        int frames = 0, inset = 0, grown = 0, framed = 0, held = 0;
        for (int t = 0; t < 60 * 12 && g.phase != Phase::Aim; t++) {
            bool chase = !g.shots.empty();
            Controls::camera(cam, g, chase, false, false, Game::DT);
            float show, full;
            Camera3D v;
            bool in = Controls::inset(v, show, full);
            if (t == 0) assert(in && show < 0.1f && full == 0);  // slid on, then growing from where it is
            inset += in, grown += in && full > 0;
            if (!in && chase) {
                frames++;
                assert(Vector3Distance(cam.position, eye) < 0.01f && fabsf(cam.fovy - Controls::FOV0) < 0.01f && Controls::viewUp(cam).y > 0.3f);
                Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position)), to = Vector3Normalize(Vector3Subtract(g.shots[0].pos, cam.position));
                framed += Vector3DotProduct(f, to) > cosf(Controls::FOV0 / 2 * DEG2RAD);
            }
            if (!chase && !in && g.phase == Phase::Settle && Vector3Distance(cam.position, eye) < 0.01f) held++;
            g.step(Input{});
        }
        assert(inset >= 25 && inset <= 32 && grown >= inset - 1);  // FullScreenTime 500 ms
        assert(frames > 60 * 6 && framed > frames * 9 / 10 && held > 30);
    }
    {  // an event camera held to the next turn: the view eases back to the worm at PosUpdateSpeed 0.1 a frame (CMS 0x51b940), no cut
        Game g;
        g.start({43, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &w = g.worms[g.current];
        g.objects.push_back({Object::Crate, Vector3Add(w.pos, {3, 12, 0}), {0, 0, 0}, -1, -1, true, false});
        Camera3D cam = away();
        g.phase = Phase::Settle, g.timer = 1;
        for (int t = 0; t < 30; t++) Controls::focus(&g.objects.back().pos, 0, true), Controls::camera(cam, g, false, false, false, Game::DT);
        float gap = Vector3Distance(cam.target, w.pos), jump = 0;
        assert(gap > 3);
        g.phase = Phase::Aim;
        for (int t = 0; t < 90; t++) {
            Vector3 was = cam.target;
            Controls::camera(cam, g, false, false, false, Game::DT);
            jump = fmaxf(jump, Vector3Distance(was, cam.target));
        }
        assert(jump < 0.2f * gap && Vector3Distance(cam.target, w.pos) < 0.5f);
    }
    for (int lost : {0, 1}) {  // PayloadTrackCamera 0x532460: no cut while the shell stays in clear view; else ViewPoints around its predicted impact
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current];
        Camera3D cam = away();
        if (!lost) for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        float top = a.pos.y + 12;
        g.phase = Phase::Settle, g.timer = 1;  // no worm active: the TrackCam full screen
        Controls::camera(cam, g, false, false, false, Game::DT);  // the turn ended an update before the shot
        g.shots.push_back({{a.pos.x, top, a.pos.z}, {25 * sinf(a.yaw), 6, 25 * cosf(a.yaw)}, weaponNamed("Bazooka"), 0, false, 1});
        g.shots.back().touching = 0;
        Vector3 before = cam.position, cutAt{}, end{};
        Camera3D ev = cam;
        int cuts = 0, after = 0, inset = 0;
        bool wasIn = false;
        for (int t = 0; t < 60 * 9 && after < 90; t++) {  // the flight (into the sea: a skim, then the sinking), then RestTime 1.5 s
            Vector3 was = ev.position;
            Controls::camera(cam, g, !g.shots.empty(), false, false, Game::DT);
            float show, full;
            bool in = Controls::inset(ev, show, full);
            if (!in) ev = cam;
            inset += in;
            if (in == wasIn && Vector3Distance(was, ev.position) > 3) cuts++, cutAt = cutAt.y == 0 ? ev.position : cutAt;
            wasIn = in;
            if (!lost && t == 70) assert(Vector3Distance(ev.position, before) < 1.5f && inView(ev, g.shots[0].pos));
            if (g.shots.empty()) after++;
            else end = g.shots[0].pos;
            g.step(Input{});
            for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom) Controls::impact(e.pos);
        }
        assert(after == 90 && cuts <= 1);  // a typical shot: at most the one cut once lost, none at or after the blast
        if (lost) assert(cutAt.y > end.y && Vector3Distance(cutAt, end) < 22 && Vector3Distance(cutAt, {a.pos.x, top, a.pos.z}) > 30);
        assert(inset == 0);
    }
    {  // a drowned worm in view: no cut while it floats, at its blast or in the RestTime after (W4M 0x51d3b3 drop); the view keeps its yaw
        Game g;
        g.start({1, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &d = g.worms[0];
        d.pos = {d.pos.x, g.water - 0.5f, d.pos.z}, d.vel = {0, 0, 0}, d.grounded = false;
        for (int k = 0; k < 400 && g.terrain.solid({d.pos.x, d.pos.y - 1.5f, d.pos.z}); k++) d.pos.x += 0.1f;  // open sea
        g.step(Input{});
        g.phase = Phase::Settle, g.timer = 1;
        Controls::reset();
        Vector3 at = {d.pos.x, g.water + 0.6f, d.pos.z};
        Camera3D cam = {Vector3Add(at, {6, 3, 6}), at, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        for (int t = 0; t < 30; t++) Controls::camera(cam, g, false, false, false, Game::DT);  // settled view of it
        float yaw0 = atan2f(cam.position.x - at.x, cam.position.z - at.z);
        int boom = -1, cuts = 0;
        for (int t = 0; t < 60 * 10 && g.phase == Phase::Settle && (boom < 0 || t < boom + 120); t++) {
            g.step(Input{});
            for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) Controls::impact(e.pos), boom = t;
            Vector3 was = cam.position;
            Controls::camera(cam, g, false, false, false, Game::DT);
            if (boom >= 0 && t > boom + 85) continue;  // RestTime over: DefaultCam takes over with a cut
            cuts += Vector3Distance(was, cam.position) > 2;
            assert(fabsf(remainderf(atan2f(cam.position.x - at.x, cam.position.z - at.z) - yaw0, 2 * PI)) < 0.2f);
        }
        assert(boom > 0 && cuts == 0);
    }
    for (int drown : {1, 0}) {  // "Worm Dying" 0x5a7190 (drowning 0x5ad640, Worm.TimeToDie 0x5adbf0): out of view, WormTrackCamera on it through its blast
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        int k = (g.current + 1) % (int)g.worms.size();
        Worm &d = g.worms[k];
        if (drown) {
            d.pos = {d.pos.x, g.water - 0.5f, d.pos.z}, d.vel = {0, 0, 0}, d.grounded = false;
            for (int n = 0; n < 400 && g.terrain.solid({d.pos.x, d.pos.y - 1.5f, d.pos.z}); n++) d.pos.x += 0.1f;
        } else d.hp = 0, g.deathQueue = {k};
        g.phase = Phase::Settle, g.timer = 1;
        Camera3D cam = away();
        bool shown = false;
        for (int t = 0; t < 60 * 8 && !shown; t++) {
            g.step(Input{});
            Controls::camera(cam, g, false, false, false, Game::DT);
            for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death && e.worm == k) assert(inView(cam, e.pos)), shown = true;
        }
        assert(shown);
    }
    {  // game over: the winner from a worm ViewPoint, in clear view
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        g.phase = Phase::GameOver, g.winner = g.worms[g.current].team;
        Camera3D cam = away();
        for (int t = 0; t < 30; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        const Worm &w = g.worms[g.current];
        Vector3 to = Vector3Subtract(w.pos, cam.position), hit;
        assert(Vector3Length(to) > 3 && Vector3Length(to) < 17 && inView(cam, w.pos));
        assert(!g.terrain.raycast({cam.position, Vector3Normalize(to)}, Vector3Length(to) - 0.6f, &hit));
    }
    {  // homing missile: FlyCam behind it, once its position factor has eased in (PosRate 0.01: ~2 s); ~14 m back at MaxHomingSpeed
        Game g;
        g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &a = g.worms[g.current];
        g.hotSeat = 0;
        a.pos = {20, 55, 20}, a.yaw = 0, a.pitch = 0, a.grounded = true;
        g.weapon = weaponNamed("Homing Missile");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;  // not about the Standard delays
        Controls::reset();
        Input in;
        in.buttons = Input::FIRE;
        for (Input i : {in, Input{}}) g.step(i), a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = true;  // the first press locks the target
        for (int t = 0; t < 90; t++) g.step(in), a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = true;  // held as if standing: W4M CanFire needs state 0
        Camera3D cam = {{20, 58, 10}, a.pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        int checked = 0;
        for (int t = 0; t < 60 * 6 && g.phase == Phase::Flying && !g.shots.empty(); t++) {
            Controls::camera(cam, g, true, false, false, Game::DT);
            const Projectile &s = g.shots[0];
            if (t > 110) assert(Vector3Distance(cam.position, s.pos) < 18 && Vector3DotProduct(s.vel, Vector3Subtract(s.pos, cam.position)) > 0), checked++;
            g.step(Input{});
        }
        assert(checked > 20);
    }
    for (int wide : {0, 1}) {  // occlusion: a thin post on the centre ray leaves the camera out, a wall zooms it in at once
        Game g;
        g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &w = g.worms[g.current];
        w.pos = {20, 55, 20}, w.yaw = 0, w.grounded = true, w.vel = {0, 0, 0};
        for (int z = 60; z <= 62; z++)
            for (int y = 0; y < Terrain::NY; y++)
                for (int x = wide ? 40 : 79; x <= (wide ? 120 : 81); x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = 127;
        Controls::reset();
        Camera3D cam = {{20, 59, 11}, w.pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        for (int t = 0; t < 60; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        float d = Vector3Distance(cam.position, w.pos);
        assert(wide ? d < 5 : d > 8);
    }
    {  // a wall behind the worm, then gone: zoomed in, then back out at OccZoomOutSpeed through the blend and its retries (0x52e870)
        Game g;
        g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &w = g.worms[g.current];
        w.pos = {20, 55, 20}, w.yaw = 0, w.grounded = true, w.vel = {0, 0, 0};
        auto wall = [&](int v) {
            for (int z = 60; z <= 62; z++)
                for (int y = 0; y < Terrain::NY; y++)
                    for (int x = 40; x <= 120; x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = v;
        };
        Controls::reset();
        Camera3D cam = {{20, 59, 11}, w.pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        for (int t = 0; t < 120; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        wall(127);
        for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        assert(Vector3Distance(cam.position, w.pos) < 5);
        wall(0);
        for (int t = 0; t < 60 * 4; t++) Controls::camera(cam, g, false, false, false, Game::DT), assert(!g.terrain.solid(cam.position));
        assert(Vector3Distance(cam.position, w.pos) > 7.5f);
    }
    {  // the worm backs up against a wall: the ShoulderCamera zooms in to it at once (it never rises or turns: OccHeightSpeed /
       // OccYawSpeed 0), and the worm fades (WormOpaqueDist 50 / WormTransparencyDist 25 units)
        Game g;
        g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &w = g.worms[g.current];
        w.pos = {20, 55, 30}, w.yaw = 0, w.grounded = true, w.vel = {0, 0, 0};
        for (int z = 40; z < 78; z++)  // z 10..19.5 m, 2.5 m over the worm: the camera spot ends inside it
            for (int y = 0; y < 230; y++)
                for (int x = 40; x < 120; x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = 127;
        Controls::reset();
        Camera3D cam = {{20, 57, 21.5f}, w.pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        for (int t = 0; t < 60; t++) Controls::camera(cam, g, false, false, false, Game::DT);
        float top = cam.position.y - w.pos.y;
        for (int t = 0; t < 240; t++) {
            w.pos.z = fmaxf(20.2f, w.pos.z - 0.05f);
            Controls::camera(cam, g, false, false, false, Game::DT);
            assert(cam.position.y - w.pos.y < top + 0.3f && !g.terrain.solid(cam.position));
        }
        assert(Vector3Distance(cam.position, w.pos) < 2);  // Xray::opacity fades it by that distance
    }
    for (int inside : {0, 1}) {  // NinjaCamMkIII: a hill between never zooms it in; inside land it swings to the first clear yaw offset
        Game g;
        g.start({25, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &w = g.worms[g.current];
        w.pos = {40, 55, 40}, w.yaw = 0, w.grounded = false, w.vel = {0, 0, 0};
        g.ropeOn({40, 60, 40});
        for (int z = 120; z < 200; z++)  // side view at yaw + 1.57: the camera sits 20 m off in -x, at (20, 55, 40)
            for (int y = 0; y < Terrain::NY; y++)
                for (int x = inside ? 60 : 112; x < (inside ? 100 : 120); x++)
                    if (!inside || (z >= 140 && z < 180)) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = 127;
        Controls::reset();
        Camera3D cam = {{20, 55, 40}, w.pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT), w.pos = {40, 55, 40};
        assert(Vector3Distance(cam.position, w.pos) > 15 && !g.terrain.solid(cam.position));
        if (inside) assert(cam.position.z > 45.5f || cam.position.z < 34.5f);  // +-22.5 deg first, past the block's z edge
    }
    for (int sea : {0, 1}) {  // W4M CMS 0x51d408: the running priority only holds while the PiP is up; full screen, a thrown worm (ashore 3, at sea 5) takes the camera
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        g.phase = Phase::Settle, g.timer = 1;  // the count runs with no worm active: full screen
        Worm &w = g.worms[1 - g.current];
        Vector3 spot = g.worms[g.current].pos;
        if (sea) w.pos = {1, 30, 40}, w.vel = {-20, 4, 0};  // off the map's edge: no landing in the sweep
        else w.vel = {6, 9, 0};
        w.grounded = false;
        Camera3D cam = away();
        int seen = 0, n = 0;
        for (int t = 0; t < 60 * 2 && !w.grounded && w.alive; t++, n++) {
            Controls::focus(&spot, 2);
            Controls::camera(cam, g, false, false, false, Game::DT);
            seen += t > 20 && inView(cam, w.pos);
            g.step(Input{});
        }
        if (sea) assert(n > 20 && Vector3Distance(cam.target, spot) > 2);
        else assert(n > 30 && seen > (n - 20) * 7 / 10);
    }
    {  // AlienAbductionCamera 0x547490: worm + 10 + (0, 50, 50) units where the beam starts lifting it, then above the UFO once it is spat out; placed at each serve only (0x531f70)
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        Worm &a = g.worms[g.current], &w = g.worms[1 - g.current];
        g.hotSeat = 0, a.pos = {20, 55, 20}, a.yaw = a.pitch = 0;
        g.weapon = weaponNamed("Alien Abduction");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        w.pos = Vector3Add(g.target(), {0, Game::R + 0.05f, 0}), w.vel = {0, 0, 0};
        Input fire;
        fire.buttons = Input::FIRE;
        g.step(fire);
        while (g.ufo() && g.ufo()->stage == Game::ABD_ARRIVING) g.step(Input{});
        Camera3D cam = away();
        int close = 0, fixed = 0;
        float top = g.terrain.colTop.empty() ? 20 : g.terrain.colTop.back() * Terrain::VOX;
        Vector3 liftAt{};
        for (int t = 0; t < 60 * 40 && g.efmvActive(); t++) {
            Controls::camera(cam, g, false, false, false, Game::DT);
            Vector3 u = g.ufo()->pos;
            bool lifting = g.ufo()->stage == Game::ABD_LIFTING;
            if (lifting && liftAt.y == 0) liftAt = cam.position, assert(fabsf(Vector3Distance(cam.position, Vector3Add(w.pos, {0, 0.5f, 0})) - 3.536f) < 0.05f);
            close += lifting && Vector3Distance(cam.position, liftAt) < 0.01f;
            fixed += g.ufo()->stage == Game::ABD_SPITTING && !g.aboard(1 - g.current) && Vector3Distance(cam.position, {u.x, top, u.z + 10}) < 0.1f;
            g.step(Input{});
        }
        assert(close > 20 && fixed > 30);
    }
    {  // homing: FIRE takes the target on the worm's aim ray (W4M state 1, no launch, no TARGET input), then it is powered; it homes on that point
        Game g;
        g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &a = g.worms[g.current];
        g.hotSeat = 0;
        g.weapon = weaponNamed("Homing Missile");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        auto hold = [&] { a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = true; };  // standing: W4M CanFire
        hold(), g.step(Input{});
        assert(!g.locked && !g.blimp);
        Vector3 h = g.target();
        Input in;
        in.buttons = Input::FIRE;
        hold(), g.step(in), g.step(in);
        assert(g.locked && Vector3Distance(g.lockAt, h) < 0.01f && g.shots.empty() && g.power == 0 && !g.cursorOn);
        hold(), g.step(Input{});
        for (int t = 0; t < 30 && g.shots.empty(); t++) hold(), g.step(in);
        hold(), g.step(Input{});
        assert(g.shots.size() == 1 && Vector3Distance(g.shots[0].aim, g.lockAt) < 1e-3f);
    }
    {  // homing launch speed follows the charge: W4M BasePower + ShotPower * MaxPower, 17.5 m/s empty to 32.5 full
        float v[2];
        for (int full = 0; full < 2; full++) {
            Game g;
            g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
            Worm &a = g.worms[g.current];
            g.weapon = weaponNamed("Homing Missile");
            g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
            auto hold = [&] { a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = true; };
            Input in;
            in.buttons = Input::FIRE;
            hold(), g.step(Input{}), hold(), g.step(in), hold(), g.step(Input{});
            assert(g.locked);
            for (int t = 0; t < (full ? 200 : 1) && g.shots.empty(); t++) hold(), g.step(in);
            hold(), g.step(Input{});
            assert(g.shots.size() == 1);
            v[full] = Vector3Length(g.shots[0].vel);
        }
        assert(v[0] > 17 && v[0] < 19.5f && fabsf(v[1] - 32.5f) < 0.1f);
    }
    {  // Starburst (W4M Detonate 0x588dd0): one blast, then Worm.Vapourize kills the rider
        Game g;
        g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
        int sb = weaponNamed("Starburst"), hp = v.hp;
        a.pos = {20, 55, 20}, v.pos = {60, 55, 60}, v.vel = {0, 0, 0};
        a.grounded = true, a.yaw = 0, a.pitch = 0, g.weapon = sb;
        g.ammo[a.team][sb] = 1, g.delays[a.team][sb] = 0;
        Input fire;
        fire.buttons = Input::FIRE;
        g.step(fire), g.step(Input{});
        assert(g.shots.size() == 1 && g.shots[0].touching != 0 && !g.shots[0].child);  // launched beside the rider: no blast at the launch
        Vector3 at = g.shots[0].pos, feet = a.pos;
        for (int t = 2; t < 209 && !g.shots.empty(); t++) {  // the 3.5 s fuse (210 ticks): the rocket stays put, FIRE does nothing, the worm stays on its feet
            g.step(t % 2 ? fire : Input{});
            assert(g.shots.size() == 1 && Vector3Distance(g.shots[0].pos, at) < 1e-4f && Vector3Distance(a.pos, feet) < 1e-4f);
        }
        float sp = 0;
        for (int t = 0; t < 120 && !g.shots.empty(); t++) {  // launched: 25 m/s^2 up to MaxTerminalVelocity 22.5 m/s, the rider on it
            g.step(Input{});
            float now = Vector3Length(g.shots[0].vel);
            assert(now >= sp && now <= 22.5f + Game::STAR_ACCEL * Game::DT && (now - sp < Game::STAR_ACCEL * Game::DT + 1e-3f));
            sp = now;
        }
        assert(!g.shots.empty() && sp > 22.4f && Vector3Distance(a.pos, g.shots[0].pos) < 0.05f && !a.grounded && a.hp > 0);
        g.shots.clear(), g.step(Input{});
        g.shots.push_back({Vector3Add(v.pos, {0, Game::R + 1, 0}), {0, -3, 0}, sb, 30 - Game::STAR_FUSE, false, 1, {0, -1, 0}});
        int booms = 0, kids = 0, riderHurt = 0;
        for (int t = 0; t < 900 && !g.shots.empty(); t++) {
            g.step(Input{});
            for (const Projectile &s : g.shots) kids += s.child && s.weapon == sb;
            for (const GameEvent &e : g.events) booms += (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom), riderHurt += e.kind == GameEvent::Hurt && e.worm == g.current;
        }
        // blast first (the target is hurt), then vapourized: no knock, no hurt shown, gone, its turn over
        assert(g.shots.empty() && v.hp < hp && !a.alive && a.hp == 0 && Vector3Length(a.vel) == 0 && kids == 0 && booms == 1 && riderHurt == 0);
    }
    {  // homing from the Blimp: the entering press (Controls swallows FIRE) locks nothing; with TARGET, FIRE locks the cursor point, no launch
        Game g;
        g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &a = g.worms[g.current];
        g.hotSeat = 0;
        g.weapon = weaponNamed("Homing Missile");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        auto hold = [&] { a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = true; };
        Input t;
        t.buttons = Input::TARGET;
        hold(), g.step(t);
        assert(g.blimp && g.cursorOn && !g.locked && Controls::targetHeld(g));
        Vector3 h;
        assert(g.blimpHit(&h));
        t.buttons = Input::TARGET | Input::FIRE;
        hold(), g.step(t), g.step(t);
        assert(g.locked && Vector3Distance(g.lockAt, h) < 0.01f && g.shots.empty() && g.power == 0);
        hold(), g.step(Input{});  // back to the aim view: the cursor no longer steers target()
        assert(!g.blimp && Vector3Distance(g.target(), h) > 0.01f);
    }
    {  // a +-10 m box 5-10 m above the OrbitCam target stays framed; the fireworks' own box (Land.Center +-0.5 Radius, W4M 0x4ffa56) can leave the
       // frame when the orbit swings low (0x530e30), so it is not asserted
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        Controls::reset();
        g.phase = Phase::GameOver, g.winner = g.worms[g.current].team;
        Camera3D cam = away();
                int checked = 0;
        for (int t = 0; t < 60 * 12; t++) {
            Controls::camera(cam, g, false, false, false, Game::DT);
            if (t < 60 * 5 || t % 30) continue;
            auto framed = [&](Vector3 p) {
                Vector2 s = GetWorldToScreenEx(p, cam, 1280, 720);
                return Vector3DotProduct(Vector3Subtract(p, cam.position), Vector3Subtract(cam.target, cam.position)) > 0 && s.x > 0 && s.x < 1280 && s.y > 0 && s.y < 720;
            };
            for (float dx : {-1.0f, 1.0f})
                for (float dz : {-1.0f, 1.0f})
                    for (float k : {0.0f, 1.0f}) assert(framed(Vector3Add(cam.target, {10 * dx, 5 + 5 * k, 10 * dz})));
            checked++;
        }
        assert(checked > 10);
    }
}

static void settle(Game &g) {
    for (int t = 0; t < 120; t++) g.step(Input{});  // land
    g.hotSeat = 0;  // the first press would only cancel it
}

// W4M Worm.Death*: the dead worm's blast takes up to 35 hp off its neighbours, throws them, digs 1.75 m; they count it after.
static void checkDeathBlast() {
    const Blast &k = Game::DEATH_BLAST;
    assert(Game::blastDamage(k, {}, {0.8f, 0, 0}) == 35 && Game::blastDamage(k, {}, {2.5f, 0, 0}) == 14 && Game::blastDamage(k, {}, {3.6f, 0, 0}) == 0);
    Game g;
    g.start({24, 2, 2, "", 0}), g.hotSeat = 0;
    settle(g);
    Worm &d = g.worms[0], &n = g.worms[2];
    n.pos = Vector3Add(d.pos, {0.8f, 0, 0}), n.vel = {0, 0, 0};
    settle(g);
    Vector3 under = {d.pos.x, d.pos.y - Game::R - 0.4f, d.pos.z}, was = n.pos;
    assert(g.terrain.solid(under) && n.hp == 100);
    d.hp = 0;
    endSettle(g);
    // W4M waits for the thrown worm (Worm Falling is active): here it lands in the sea, drowned and counted before the next turn
    assert(!d.alive && n.hp <= 65 && n.counted == std::max(0, n.hp) && Vector3Distance(n.pos, was) > 3 && !g.terrain.solid(under));
}

static Vector3 facing(const Worm &w) { return {sinf(w.yaw), 0, cosf(w.yaw)}; }

// Concave corner (floor + two walls): walking into it or dropping against a wall leaves W4M's body, the 3 rods (Fits 0x59edf0), out of
// the rock; the 0.3 m mesh may dip into the walls between them, as in W4M (docs/w4m/physics.md §5 "corners")
static void checkWallClearance() {
    auto room = [](Game &g) {  // floor y 50, walls x < 10 and z < 10; density = signed distance, clamped like the map's
        for (int z = 24; z < 72; z++)
            for (int y = 180; y < 240; y++)
                for (int x = 24; x < 72; x++) {
                    float in = fmaxf(50 - y * Terrain::VOX, fmaxf(10 - x * Terrain::VOX, 10 - z * Terrain::VOX));
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(in * Terrain::Q, -127, 127);
                }
    };
    auto clear = [](const Game &g, const Worm &w) {
        for (Vector2 r : {Vector2{0.2f, -0.15f}, Vector2{-0.2f, -0.15f}, Vector2{0, 0.25f}})
            for (float h = 0; h <= 1; h += 0.05f)
                if (g.terrain.solid({w.pos.x + r.x, w.pos.y - Game::R + h, w.pos.z + r.y})) return false;
        return true;
    };
    Game g;
    g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
    room(g);
    Worm &w = g.worms[g.current];
    w.pos = {12, 50.5f, 12}, w.vel = {}, w.yaw = -3 * PI / 4;  // facing the corner
    g.hotSeat = 0;
    Input walk;
    walk.walk = 127;
    for (int t = 0; t < 120; t++) g.step(walk);
    assert(w.pos.x < 10.6f && w.pos.z < 10.6f && clear(g, w));  // reached the corner, body outside both walls

    Game f;
    f.start({33, 2, 1, "", 0}), f.hotSeat = 0;
    room(f);
    Worm &d = f.worms[f.current];
    d.pos = {10.5f, 53, 13}, d.vel = {-2, 0, 0};  // falls against the wall
    f.hotSeat = 0;
    for (int t = 0; t < 120; t++) f.step(Input{});
    assert(d.grounded && clear(f, d));
}

// Density clamped like the imported .vox maps (64 / Q): corridors and steps stay walkable, W4M vaults ledges up to the body.
static void checkWalkW4M() {
    auto walk = [](auto sdf, int ticks, float yaw = PI / 2, Vector3 from = {9, 50.5f, 12}) {
        Game g;
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        for (int z = 16; z < 80; z++)
            for (int y = 176; y < 248; y++)
                for (int x = 16; x < 176; x++)
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(sdf(Vector3Scale({(float)x, (float)y, (float)z}, Terrain::VOX)) * Terrain::Q, -64, 64);
        Worm &w = g.worms[g.current];
        w.pos = from, w.vel = {}, w.yaw = yaw;
        g.hotSeat = 0;
        Input in;
        in.walk = 127;
        for (int t = 0; t < ticks; t++) g.step(in);
        float worst = -1;  // deepest point of a 0.15 m ring at body height
        for (int k = 0; k < 16; k++) worst = fmaxf(worst, g.terrain.sample({w.pos.x + 0.15f * cosf(k * PI / 8), w.pos.y + 0.3f, w.pos.z + 0.15f * sinf(k * PI / 8)}));
        return std::make_pair(w.pos, worst);
    };
    auto step = [](Vector3 p, float h) { return fmaxf(50 - p.y, fminf(50 + h - p.y, p.x - 12)); };  // floor y 50, step up at x 12
    auto corridor = [&](Vector3 p) { return fmaxf(step(p, 0.3f), fmaxf(11.6f - p.z, p.z - 12.4f)); };  // 0.8 m wide
    Vector3 p = walk(corridor, 150).first;
    assert(p.x > 15 && p.y > 50.75f);
    p = walk([&](Vector3 q) { return step(q, 0.8f); }, 150).first;
    assert(p.x > 13 && p.y > 51.25f);  // vaulted
    p = walk([&](Vector3 q) { return step(q, 1.5f); }, 150).first;
    assert(p.x < 11.9f && p.y < 50.6f);  // a wall
    p = walk([](Vector3 q) { return fmaxf(50 - q.y, (50 + (q.x - 12) * tanf(50 * DEG2RAD) - q.y) * cosf(50 * DEG2RAD)); }, 120).first;
    assert(p.x > 14 && p.x < 9 + 3 * 2 + 0.2f);  // climbs a steep slope at walking speed, no vault hops
    auto [c, worst] = walk([](Vector3 q) { return fmaxf(50 - q.y, fmaxf(10 - q.x, 10 - q.z)); }, 120, -3 * PI / 4, {12, 50.5f, 12});
    assert(c.x < 10.6f && c.z < 10.6f && worst <= 0);  // in the corner, out of both walls
}

// Exact land as an imported map's cells: a box along unit horizontal eu from o (u), ev = eu turned 90 deg (v), y up; the arena cleared first
static void landBox(Game &g, Vector3 o, Vector3 eu, float u0, float u1, float y0, float y1, float v0, float v1) {
    Vector3 c[8];
    for (int k = 0; k < 8; k++) {
        float u = k & 1 ? u1 : u0, v = k & 4 ? v1 : v0;
        c[k] = {o.x + eu.x * u - eu.z * v, k & 2 ? y1 : y0, o.z + eu.z * u + eu.x * v};
    }
    g.terrain.addCell(c);
}
static void clearArena(Game &g, int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int z = z0; z < z1; z++)
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = -64;
}

// W4M UpdateWalking casts the 4 foot rays: the front foot finds a low ledge and the worm steps or vaults onto it; the walkable test reads
// the flat face beyond the lip (0x46a070). Exact cells off the voxel grid and across it alike.
static void checkLowLedges() {
    for (float h : {0.2f, 0.35f, 0.5f, 0.7f})
        for (float off : {0.0f, 0.07f, 0.13f})
            for (int diag = 0; diag < 2; diag++) {
                Game g;
                g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
                clearArena(g, 16, 176, 176, 248, 16, 80);
                landBox(g, {0, 0, 0}, {1, 0, 0}, 4, 44, 48, 50, 4, 20);  // floor y 50
                const Vector3 e = diag ? Vector3{0.7071f, 0, 0.7071f} : Vector3{1, 0, 0};  // the ledge edge across e at u = 12 + off (diag: x + z = 24)
                landBox(g, {0, 0, 0}, e, (diag ? 24 * 0.7071f : 12) + off, diag ? 46 : 44, 48, 50 + h, diag ? -29 : 4, diag ? 12 : 20);
                Worm &w = g.worms[g.current];
                w.pos = {10, 50.5f, diag ? 10.0f : 12.0f}, w.vel = {}, w.yaw = diag ? PI / 4 : PI / 2;
                g.hotSeat = 0;
                Input walk;
                walk.walk = 127;
                for (int t = 0; t < 120; t++) g.step(walk);
                assert(w.pos.y > 50.5f + h - 0.05f && !w.motion.slide);
            }
}

// W4M Vaulting 0x5aca80: a 16-unit ledge is climbed in 250 ms at 4 units per frame, not at once; the stick held along the start input
// keeps it going, letting go drops it back, jump is ignored and nothing in the way stops it.
static void checkVault() {
    auto arena = [](Game &g) {  // floor y 50, a 0.8 m ledge from x 12
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        for (int z = 16; z < 80; z++)
            for (int y = 176; y < 248; y++)
                for (int x = 16; x < 176; x++) {
                    Vector3 p = Vector3Scale({(float)x, (float)y, (float)z}, Terrain::VOX);
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(fmaxf(50 - p.y, fminf(50.8f - p.y, p.x - 12)) * Terrain::Q, -64, 64);
                }
        Worm &w = g.worms[g.current];
        w.pos = {11, 50.5f, 12}, w.vel = {}, w.yaw = PI / 2;
        g.hotSeat = 0;
    };
    Input walk;
    walk.walk = 127;
    auto reach = [&](Game &g) {  // walks until the vault starts; the start tick does not move the worm
        Vector3 at{};
        for (int t = 0; t < 120 && !g.vault.t; t++) at = g.worms[g.current].pos, g.step(walk);
        assert(g.vault.t == msTicks(250) && Vector3Equals(g.worms[g.current].pos, at) && g.vault.to.y - at.y > Game::STEP);
    };
    Game g;
    arena(g);
    reach(g);
    Worm &w = g.worms[g.current];
    Vector3 to = g.vault.to;
    uint32_t sum = g.checksum();
    int ticks = 0;
    for (Vector3 at = w.pos; g.vault.t; ticks++, at = w.pos) {
        Input in = walk;
        if (ticks == 3) in.buttons = Input::JUMP;
        g.step(in);
        assert(Vector3Distance(w.pos, at) <= Vector3Distance(at, to) * (1 - powf(0.8f, Game::DT / 0.02f)) + 1e-4f || !g.vault.t);  // 1/5 of the way per 20 ms
        assert(!g.jumpDelay && w.grounded);
    }
    assert(ticks == msTicks(250) && Vector3Equals(w.pos, to));
    Game f;  // W4M CanFire: a vault is not Ambulatory; only CanBeFiredWhenWormMoving weapons fire, carrying the walk velocity
    arena(f);
    reach(f);
    f.weapon = weaponNamed("Bazooka");
    assert(!f.fireable(f.worms[f.current]));
    f.weapon = weaponNamed("Dynamite");
    assert(f.fireable(f.worms[f.current]) && fabsf(Vector3Length(f.vault.vel) - Game::INPUT_IMPULSE) < 1e-4f);  // the last step's InputImpulse
    f.weapon = weaponNamed("Shotgun");
    assert(f.fireable(f.worms[f.current]));
    Game h;  // same run, same sums
    arena(h);
    reach(h);
    assert(h.checksum() == sum);
    h.vault.t--;
    assert(h.checksum() != sum);

    Game r;  // let go halfway: back where it started
    arena(r);
    reach(r);
    Vector3 from = r.vault.from;
    for (int t = 0; t < 4; t++) r.step(walk);
    assert(r.worms[r.current].pos.y > from.y + 0.3f);
    r.step(Input{});
    assert(!r.vault.t && Vector3Equals(r.worms[r.current].pos, from));

    Game b;  // land welded over the ledge mid-vault: W4M tests Fits once, at the start
    arena(b);
    reach(b);
    to = b.vault.to;
    b.step(walk);
    b.terrain.weld(Vector3Add(to, {0, 0.5f, 0}), {0.3f, 0.3f, 0.3f});
    for (int t = 0; t < 30 && b.vault.t; t++) b.step(walk);
    assert(!b.vault.t && Vector3Equals(b.worms[b.current].pos, to));
}

// W4M walk rules: Sliding takes no walk input (state 3), the uphill rule (0x5b1920), the vault's walkable test (0x5b11ca),
// and the stuck count that lands a worm whose fall does not Fit (0x5af821).
static void checkW4MWalkRules() {
    auto arena = [](Game &g, auto sdf, int y0 = 176, int y1 = 248) {
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        for (int z = 16; z < 80; z++)
            for (int y = y0; y < y1; y++)
                for (int x = 16; x < 176; x++)
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(sdf(Vector3Scale({(float)x, (float)y, (float)z}, Terrain::VOX)) * Terrain::Q, -64, 64);
        g.hotSeat = 0;
    };
    Input walk;
    walk.walk = 127;
    auto slope = [](float deg) { return [=](Vector3 p) { return fmaxf(50 - p.y, (50 + (p.x - 12) * tanf(deg * DEG2RAD) - p.y) * cosf(deg * DEG2RAD)); }; };
    {  // landed slowly on a 70 degree slope: Sliding finds the uphill foot over 5 units (0x5b002f) and lands; the stick changes nothing
        Game a, b;
        arena(a, slope(70)), arena(b, slope(70));
        for (Game *g : {&a, &b}) g->worms[g->current].pos = {12.6f, 50.5f + 0.6f * tanf(70 * DEG2RAD), 12}, g->worms[g->current].vel = {}, g->worms[g->current].yaw = PI / 2, g->worms[g->current].grounded = false;
        for (int t = 0; t < 40; t++) a.step(walk), b.step(Input{});
        assert(fabsf(a.worms[a.current].pos.x - b.worms[b.current].pos.x) < 1e-4f && !a.worms[a.current].motion.slide && fabsf(a.worms[a.current].pos.x - 12.6f) < 0.05f);
    }
    for (int k = 0; k < 20; k++) {  // walked into from the floor: a step onto it (cand on the slope, n.(cand - pos) ~ 0) Slides, never walks up it
        Game g;
        arena(g, slope(70));
        Worm &w = g.worms[g.current];
        w.pos = {11 + k * 0.0021f, 50.5f, 12}, w.vel = {}, w.yaw = PI / 2;
        for (int t = 0; t < 90; t++) g.step(walk), assert(w.motion.slide || w.pos.y < 50.5f + 0.4f);
        for (int t = 0; t < 120; t++) g.step(Input{});
        assert(w.pos.x < 12.1f && w.pos.y < 50.5f + 0.15f);  // slid back to the foot
    }
    {  // a 0.35 m step onto a 70 degree slope: a vault onto ground that is not walkable, refused
        Game g;
        arena(g, [](Vector3 p) { return fmaxf(50 - p.y, fminf(p.x - 12, (50.35f + (p.x - 12) * tanf(70 * DEG2RAD) - p.y) * cosf(70 * DEG2RAD))); });
        Worm &w = g.worms[g.current];
        w.pos = {11, 50.5f, 12}, w.vel = {}, w.yaw = PI / 2;
        for (int t = 0; t < 90; t++) g.step(walk), assert(!g.vault.t);
        assert(w.pos.x < 12.1f);
    }
    {  // the rods through a slab, the feet under it: the fall does not Fit, is undone (+2), Rebounds to a stop (0x5acea0: under
       // 0.01 units/ms, n up) into Sliding, whose Landed clears the count; then it stays put, Ambulatory
        Game g;
        arena(g, [](Vector3 p) { return 0.125f - fabsf(p.y - 50.25f); }, 190, 215);
        Worm &w = g.worms[g.current];
        w.pos = {12, 50.1f, 12}, w.vel = {}, w.grounded = false, w.motion = {};
        Vector3 at = w.pos;
        g.step(Input{});
        assert(w.motion.slide && w.motion.stuck == STUCK_UP && w.pos.y == at.y);
        for (int t = 0; t < 60; t++) g.step(Input{}), assert(t == 0 || (w.grounded && !w.motion.slide && w.motion.stuck == 0));
        assert(fabsf(w.pos.y - at.y) < 0.02f);
    }
}

// W4M Ballistic casts all 8 land probe points (0x59ec70): a foot of the tripod (+-4, -3) (0, 5) units lands on the lips of a slot
// narrower than the 8-unit stance, and walking takes the highest foot (UpdateWalking); a wider slot lets the Fits rods in.
static void checkNarrowSlot() {
    auto arena = [](Game &g, float w, bool diag) {  // floor y 50, a slot w wide and 1.5 m deep through (40, 40)
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        clearArena(g, 120, 200, 176, 210, 120, 200);
        const Vector3 o = {40, 0, 40}, e = diag ? Vector3{0.7071f, 0, -0.7071f} : Vector3{1, 0, 0};
        landBox(g, o, e, -10, -w / 2, 44, 50, -10, 10), landBox(g, o, e, w / 2, 10, 44, 50, -10, 10), landBox(g, o, e, -10, 10, 44, 48.5f, -10, 10);
        g.hotSeat = 0;
    };
    for (bool diag : {false, true})
        for (float off : {-0.1f, -0.05f, 0.0f, 0.03f, 0.08f}) {
            Game g;
            arena(g, 0.35f, diag);
            Worm &w = g.worms[g.current];
            w.pos = {40 + off, 51.5f, 40 + (diag ? -off : 0)}, w.vel = {}, w.grounded = false;
            for (int t = 0; t < 90; t++) g.step(Input{});
            assert(w.grounded && w.pos.y > 50.3f);  // on the lips
        }
    Game c;  // walked across it
    arena(c, 0.35f, false);
    Worm &w = c.worms[c.current];
    w.pos = {39, 50.5f, 40}, w.vel = {}, w.yaw = PI / 2;
    Input walk;
    walk.walk = 127;
    for (int t = 0; t < 40; t++) c.step(walk);
    assert(w.pos.x > 40.5f && w.pos.y > 50.3f);
    Game d;  // 0.5 m: the rods fit, it drops in
    arena(d, 0.5f, false);
    d.worms[d.current].pos = {40, 51.5f, 40}, d.worms[d.current].vel = {}, d.worms[d.current].grounded = false;
    for (int t = 0; t < 90; t++) d.step(Input{});
    assert(d.worms[d.current].pos.y < 49.6f);
}

static int8_t headingOf(float yaw) { return (int8_t)(lroundf(remainderf(yaw, 2 * PI) / PI * 128) & 0xff); }

// W4M 0x5b107c: walking sets the facing to the stick direction at once, a quarter, half or three-quarter turn alike.
static void checkHeading() {
    Game g;
    g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &w = g.worms[g.current];
    for (float q : {PI / 2, PI, 3 * PI / 2, -PI / 2}) {
        w.yaw = 0.3f;
        Input in;
        in.buttons = Input::HEADING, in.turn = headingOf(0.3f + q);
        g.step(in);
        assert(fabsf(remainderf(w.yaw - 0.3f - q, 2 * PI)) < PI / 128);  // one tick
    }
}

// Walked off a ledge onto a 76 degree face: the worm lands, then walks out. Head wedged under a sloping ceiling: W4M's walk stays blocked.
static void checkWallStuck() {
    auto arena = [](Game &g, auto sdf) {
        for (int z = 16; z < 80; z++)
            for (int y = 176; y < 248; y++)
                for (int x = 16; x < 176; x++)
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(sdf(Vector3Scale({(float)x, (float)y, (float)z}, Terrain::VOX)) * Terrain::Q, -64, 64);
        g.hotSeat = 0;
    };
    auto walk = [](Game &g, float yaw, int ticks) {
        Input in;
        in.buttons = Input::HEADING, in.turn = headingOf(yaw), in.walk = 127;
        for (int t = 0; t < ticks; t++) g.step(in);
    };
    auto head = [](const Game &g, Vector3 p) {  // deepest point of a 0.2 m ring at the head
        float worst = -1;
        for (int k = 0; k < 16; k++) worst = fmaxf(worst, g.terrain.sample({p.x + 0.2f * cosf(k * PI / 8), p.y + 0.45f, p.z + 0.2f * sinf(k * PI / 8)}));
        return worst;
    };
    Game g;  // ledge x < 12 at y 50, floor y 46, face rising from x 13.5 with normal (-0.97, 0.24)
    g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
    arena(g, [](Vector3 p) { return fmaxf(fmaxf(fminf(50 - p.y, 12 - p.x), 46 - p.y), 0.97f * (p.x - 13.5f) - 0.24f * (p.y - 46)); });
    Worm &w = g.worms[g.current];
    w.pos = {10.5f, 50.5f, 12}, w.vel = {}, w.yaw = 0;
    walk(g, PI / 2, 150);  // turns to the face at once, walks off and falls against it; pushed into it, it skids up and back (Sliding)
    for (int t = 0; t < 120; t++) g.step(Input{});
    assert(w.grounded && !w.motion.slide && w.pos.y < 47.5f);
    Vector3 at = w.pos;
    walk(g, -PI / 2, 30);
    assert(w.pos.x < at.x - 0.5f);

    Game c;  // ceiling sloping down from y 51.2 at x 12; the head starts 0.05 m in it (land appearing around it: no move leads there)
    c.start({33, 2, 1, "", 0}), c.hotSeat = 0;
    arena(c, [](Vector3 p) { return fmaxf(50 - p.y, fminf((p.y - 51.2f + (p.x - 12) * 0.6f) * 0.857f, 54 - p.y)); });
    Worm &u = c.worms[c.current];
    u.pos = {12.5f, 50.5f, 12}, u.vel = {}, u.yaw = PI / 2;
    assert(head(c, u.pos) > 0);
    walk(c, -PI / 2, 60);
    assert(u.pos.x == 12.5f && u.pos.y == 50.5f);  // its foot rays start in land: d = 20, the vault 20 units up does not Fit (0x5b1209)
}

// W4M StartJump 0x5acd40 tests only the button and Flags 0x1000: a worm pressed against a cliff jumps like anywhere else.
static void checkJumpAtWall() {
    const float yaws[4] = {PI / 2, -PI / 2, 0, PI};
    const Vector3 from[4] = {{10, 50.5f, 12}, {14, 50.5f, 12}, {12, 50.5f, 10}, {12, 50.5f, 14}};
    for (float lean : {0.0f, 0.1f})  // upright, or leaning 6 degrees over the worm: the body touches it on the way up
    for (int dir = 0; dir < 4; dir++)  // the cliff on each side: the foot probes are not symmetric
        for (int mode = 0; mode < 6; mode++) {  // facing it / away; tap, double tap, tap with the stick held into it
            Game g;
            g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
            for (int z = 16; z < 80; z++)  // floor y 50, cliff beyond x or z 12
                for (int y = 176; y < 248; y++)
                    for (int x = 16; x < 176; x++) {
                        Vector3 p = Vector3Scale({(float)x, (float)y, (float)z}, Terrain::VOX);
                        float c = (dir == 0 ? p.x - 12 : dir == 1 ? 12 - p.x : dir == 2 ? p.z - 12 : 12 - p.z) + lean * (p.y - 50);
                        g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(fmaxf(50 - p.y, c) * Terrain::Q, -64, 64);
                    }
            Worm &w = g.worms[g.current];
            w.pos = from[dir], w.vel = {}, w.yaw = yaws[dir];
            g.hotSeat = 0;
            Input walk;
            walk.walk = 127;
            for (int t = 0; t < 90; t++) g.step(walk);  // pressed against the cliff
            bool away = mode & 1, twice = mode / 2 == 1, push = mode / 2 == 2;
            if (away) w.yaw += PI;
            for (int t = 0; t < 10; t++) g.step(Input{});
            assert(w.grounded && !w.motion.slide && !g.vault.t && !g.jumpDelay);
            float y0 = w.pos.y, top = y0;
            for (int t = 0; t < 120; t++) {
                Input in = push && !away ? walk : Input{};
                if (t == 0 || (twice && t == 2)) in.buttons |= Input::JUMP;
                g.step(in), top = fmaxf(top, w.pos.y);
            }
            assert(top - y0 > 1.5f);
        }
}

// W4M launch 0x5a5d30 + exact Integrate 0x5a6e90, 20 units = 1 m: jump 50 units up, 80 along; backflip 80 up, 50.6 back.
static void checkJumpTrajectory() {
    auto jump = [](bool flip, uint32_t rules, float &h, float &d, int &air) {
        Game g;
        g.start({33, 2, 1, "", rules}), g.hotSeat = 0;
        for (int z = 16; z < 80; z++)  // flat floor at y 50
            for (int y = 176; y < 248; y++)
                for (int x = 16; x < 300; x++)
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp((50 - y * Terrain::VOX) * Terrain::Q, -64, 64);
        Worm &w = g.worms[g.current];
        w.pos = {20, 50.6f, 12}, w.vel = {}, w.yaw = flip ? -PI / 2 : PI / 2;
        g.hotSeat = 0;
        for (int t = 0; t < 30; t++) g.step(Input{});
        Vector3 p0 = w.pos;
        Input j;
        j.buttons = Input::JUMP;
        g.step(j);
        if (flip) g.step(Input{}), g.step(j);
        h = 0, air = 0;
        for (int t = 0; t < 400 && !(air && w.grounded); t++) g.step(Input{}), air += !w.grounded, h = fmaxf(h, w.pos.y - p0.y);
        d = fabsf(w.pos.x - p0.x);
        g.step(Input{});
        assert(w.grounded && Vector3Length(w.vel) == 0 && fabsf(w.pos.x - p0.x) == d);  // lands walking: no slide
    };
    float h, d;
    int air;
    jump(false, 0, h, d, air);
    printf("jump: %.3f m high, %.3f m long, %d ticks\n", h, d, air);
    assert(fabsf(h - 2.5f) < 0.06f && fabsf(d - 4) < 0.1f && abs(air - 76) <= 1);  // W4M air time 1265 ms
    jump(true, 0, h, d, air);
    printf("backflip: %.3f m high, %.3f m long, %d ticks\n", h, d, air);
    assert(fabsf(h - 4) < 0.06f && fabsf(d - 2.53f) < 0.1f && abs(air - 96) <= 1);  // 1600 ms
    jump(false, RULE_LOW_GRAVITY, h, d, air);
    printf("low gravity jump: %.3f m high, %.3f m long\n", h, d);
    assert(fabsf(h - 5) < 0.1f && fabsf(d - 8) < 0.15f);  // launch speeds stay those of full gravity
}

// Flat floor at y 50 over x 4..75 m, z 4..20 m; wall: a one-voxel slab at x = wall (m) when > 0.
static void floorAndWall(Game &g, float wall) {
    for (int z = 16; z < 80; z++)
        for (int y = 176; y < 248; y++)
            for (int x = 16; x < 300; x++)
                g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] =
                    x == (int)(wall / Terrain::VOX) && y > 200 ? 127 : (signed char)Clamp((50 - y * Terrain::VOX) * Terrain::Q, -64, 64);
}

// W4M launches from the eye (0x585a29): a worm against a thin wall blows its bazooka up on its own side, a shotgun hits the near face,
// dropped dynamite rests on the land just ahead (centre contact, mesh drawn its depth over it: 0x574e90, 0x5761f0).
// W4M LogicalLaunchZOffset (Landmine 10 / Dynamite 13 / Sheep 5 units): a grounded worm sets a dropped weapon down ahead along
// its facing, on the flat and up a slope, never behind it.
static void checkDroppedInFront() {
    for (int slope : {0, 1})
        for (const char *name : {"Landmine", "Dynamite"}) {
            Game g;
            g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
            floorAndWall(g, 0);
            if (slope)
                for (int z = 16; z < 80; z++)
                    for (int y = 176; y < 248; y++)
                        for (int x = 16; x < 300; x++)
                            g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] =
                                (signed char)Clamp((50 + 0.5f * (z * Terrain::VOX - 12) - y * Terrain::VOX) * Terrain::Q, -64, 64);
            Worm &w = g.worms[g.current];
            w.pos = {20, 50.6f, 12}, w.vel = {}, w.yaw = 0, w.pitch = 0;
            g.hotSeat = 0, g.weapon = weaponNamed(name);
            for (int t = 0; t < 30; t++) g.step(Input{});
            Vector3 at = w.pos;
            size_t no = g.objects.size(), ns = g.shots.size();
            Input fire;
            fire.buttons = Input::FIRE;
            float z = at.z;
            for (int t = 0; t < 200; t++) {
                g.step(t < 40 ? fire : Input{});
                if (g.objects.size() > no) z = g.objects.back().pos.z;
                else if (g.shots.size() > ns) z = g.shots.back().pos.z;
            }
            printf("%s on slope %d: rests at dz %.3f\n", name, slope, z - at.z);
            assert(z - at.z > 0.3f);
        }
}

static void checkLaunchAtWall() {
    for (int gun : {0, 1}) {
        Game g;
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        floorAndWall(g, 20.5f);
        Worm &w = g.worms[g.current];
        w.pos = {20, 50.6f, 12}, w.vel = {}, w.yaw = PI / 2, w.pitch = 0;
        g.hotSeat = 0, g.wind = 0, g.weapon = weaponNamed(gun ? "Shotgun" : "Bazooka");
        for (int t = 0; t < 30; t++) g.step(Input{});
        assert(w.grounded && w.pos.x < 20.3f);
        Input fire;
        fire.buttons = Input::FIRE;
        float boom = -1;
        for (int t = 0; t < 300 && boom < 0; t++) {
            g.step(t < 40 ? fire : Input{});
            for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) boom = e.pos.x;
        }
        printf("%s into a wall at 20.5 m: blast at x %.2f\n", gun ? "shotgun" : "bazooka", boom);
        assert(boom > 19.5f && boom < 20.5f);  // the worm's side of the slab
    }
    Game g;
    g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
    floorAndWall(g, 0);
    Worm &w = g.worms[g.current];
    w.pos = {20, 50.6f, 12}, w.vel = {}, w.yaw = PI / 2, w.pitch = 0;
    g.hotSeat = 0, g.weapon = weaponNamed("Dynamite");
    for (int t = 0; t < 30; t++) g.step(Input{});
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire), g.step(Input{});
    for (int t = 0; t < 120; t++) g.step(Input{});
    assert(g.shots.size() == 1);
    Vector3 p = g.shots[0].pos, drawn = restOn(g.terrain, p, 0.25f);
    printf("dynamite at rest: %.2f m ahead, centre %.3f m, mesh bottom %.3f m over the floor\n", p.x - w.pos.x, p.y - 50, drawn.y - 0.25f - 50);
    assert(p.y >= 50 && drawn.y - 0.25f > 49.97f && drawn.y - 0.25f < 50.05f);
    assert(p.x - w.pos.x > 0.6f && p.x - w.pos.x < 0.8f && fabsf(p.z - w.pos.z) < 0.05f);  // W4M: 13 units ahead, 5 up, 0.01 units/ms: 0.75 m
}

// W4M: a payload ignores what it touches on launch until it leaves it (0x582200, 0x581dc0), a gun skips its shooter (0x519dd0):
// fired down at point blank from the eye, the shot passes the worm's own body and the blast on the floor hurts it.
static void checkPointBlankDown() {
    for (int gun : {0, 1}) {
        Game g;
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        floorAndWall(g, 0);
        Worm &w = g.worms[g.current];
        w.pos = {20, 50.6f, 12}, w.vel = {}, w.yaw = PI / 2, w.pitch = -1.2f;
        g.hotSeat = 0, g.wind = 0, g.weapon = weaponNamed(gun ? "Shotgun" : "Bazooka");
        for (int t = 0; t < 30; t++) g.step(Input{});
        int hp = w.hp;
        Input fire;
        fire.buttons = Input::FIRE;
        Vector3 boom = {0, -1, 0};
        for (int t = 0; t < 300 && boom.y < 0; t++) {
            g.step(t < 40 ? fire : Input{});
            for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) boom = e.pos;
        }
        for (int t = 0; t < 600 && w.hp == hp; t++) g.step(Input{});
        printf("%s down at point blank: blast %.2f m below the eye, %.2f m out; shooter %d -> %d hp\n", gun ? "shotgun" : "bazooka", w.pos.y - Game::R + 0.75f - boom.y,
               Vector2Distance({boom.x, boom.z}, {w.pos.x, w.pos.z}), hp, w.hp);
        assert(boom.y > 49.8f && boom.y < 50.2f && Vector2Distance({boom.x, boom.z}, {w.pos.x, w.pos.z}) < 0.6f && w.hp < hp);
    }
}

// W4M payloads (0x57ea40): wind adds Wind.Speed to the acceleration; the homing missile has no gravity and homes only in stage 2.
static void checkPayloadForces() {
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    g.wind = 1, g.phase = Phase::Flying;
    int baz = weaponNamed("Bazooka"), hom = weaponNamed("Homing Missile");
    g.shots = {{{20, 70, 20}, {0, 0, 0}, baz, 0, false, 1}, {{30, 70, 20}, {0, 0, 10}, hom, 0, false, 1, {30, 70, -100}}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    for (int t = 0; t < 60; t++) g.step(Input{});
    assert(fabsf(g.shots[0].vel.x - Game::WIND_ACCEL) < 0.01f && fabsf(g.shots[0].vel.y + 12.5f * 0.6f) < 0.01f);  // 1 s: wind, Gravity.Slow
    assert(g.shots[1].vel.y == 0 && g.shots[1].vel.z == 10);  // stage 1: straight on
    for (int t = 0; t < 60; t++) g.step(Input{});
    assert(g.shots[1].vel.z < 0 && Vector3Length(g.shots[1].vel) <= Game::HOMING_MAX + 1e-3f);  // turned to its target
}

static void checkJumps() {
    Game g;
    g.start({31, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    Worm &w = g.worms[g.current];
    assert(w.grounded && g.phase == Phase::Aim);
    Input jump;
    jump.buttons = Input::JUMP;
    g.step(jump);
    assert(g.jumpDelay > 0 && w.grounded);  // waits for a possible second press
    for (int t = 0; t < Game::JUMP_WINDOW && w.grounded; t++) g.step(Input{});
    assert(!w.grounded && Vector3DotProduct(w.vel, facing(w)) > 2 && w.vel.y > 6 && w.vel.y < 8);  // forward jump

    // W4M DetectJump: every kind leaves when the 300 ms window ends; the stick at that moment picks the variant
    auto leave = [&](bool twice, bool held, int8_t walk, float &along) {
        Game f;
        f.start({31, 2, 1, "", 0}), f.hotSeat = 0;
        settle(f);
        Worm &b = f.worms[f.current];
        Input in;
        in.walk = walk;
        for (int t = 0; t <= Game::JUMP_WINDOW + 1 && b.grounded; t++) {
            in.buttons = t == 0 || (twice && t == 2) || (held && !twice) ? Input::JUMP : 0;
            f.step(in);
            assert(t >= Game::JUMP_WINDOW - 1 || b.grounded);  // nothing leaves before the window ends
        }
        along = Vector3DotProduct(b.vel, facing(b));
        return b.vel.y;
    };
    float along, up = leave(true, false, -127, along);
    assert(along < -1.5f && up > 9.5f);  // backflip: stick back
    up = leave(true, false, 127, along);
    assert(along > 1.5f && along < 1.7f && up > 9.5f);  // forward flip
    up = leave(true, false, 0, along);
    assert(along > 1.5f && along < 1.7f && up > 9.5f);  // no input: forward too (+0x159 set, no JumpBack key)
    up = leave(false, true, 0, along);
    assert(fabsf(along) < 0.01f && up > 9 && up < 9.4f);  // held: vertical jump
    up = leave(false, true, 127, along);
    assert(along > 3 && up < 8);  // held with the stick forward: a normal jump

    Game f;  // a double press: one forward flip, one Jump event, nothing re-armed on landing
    f.start({31, 2, 1, "", 0}), f.hotSeat = 0;
    settle(f);
    Worm &b = f.worms[f.current];
    int jumps = 0, launches = 0;
    bool was = true;
    for (int t = 0; t < 240; t++) {
        Input in;
        in.buttons = t == 0 || t == 2 ? Input::JUMP : 0;
        f.step(in);
        for (const GameEvent &e : f.events) jumps += e.kind == GameEvent::Jump;
        if (was && !b.grounded && b.vel.y > 5) launches++, assert(Vector3DotProduct(b.vel, facing(b)) > 1.5f);  // not a slide off a ledge
        was = b.grounded;
    }
    assert(jumps == 1 && launches == 1 && !f.jumpDelay);

    // Switch timing: press 1-6 ticks, gap 1-12, press 1-6 (frames of 1-3 ticks): always a flip
    for (int hold = 1; hold <= 6; hold++)
        for (int gap = 1; gap <= 12; gap++) {
            Game s;
            s.start({31, 2, 1, "", 0}), s.hotSeat = 0;
            settle(s);
            Worm &c = s.worms[s.current];
            for (int t = 0; t < Game::JUMP_WINDOW + 2 && c.grounded; t++) {
                Input in;
                in.buttons = t < hold || (t >= hold + gap && t < 2 * hold + gap) ? Input::JUMP : 0;
                s.step(in);
            }
            assert(!c.grounded && Vector3DotProduct(c.vel, facing(c)) > 1.5f && c.vel.y > 9.5f);
        }

    // the pad (HEADING): double tap while walking forward, or pulling back in the window; the worm does not turn in DetectJump
    for (int back = 0; back < 2; back++) {
        Game h;
        h.start({31, 2, 1, "", 0}), h.hotSeat = 0;
        settle(h);
        Worm &c = h.worms[h.current];
        float yaw = c.yaw;
        int ev = 0;
        for (int t = 0; t < 30 && !ev; t++) {
            Input in;
            in.buttons = Input::HEADING | (t == 10 || t == 12 ? Input::JUMP : 0), in.walk = 127;
            in.turn = (int8_t)lroundf(wrapPi(yaw + (back && t > 10 ? PI : 0)) / PI * 128);
            h.step(in);
            for (const GameEvent &e : h.events) if (e.kind == GameEvent::Jump) ev = e.weapon;
            if (t > 10) assert(fabsf(wrapPi(c.yaw - yaw)) < 0.05f);
        }
        float a = Vector3DotProduct(c.vel, facing(c));
        if (back) assert(ev == 4 && a < -1.5f && c.vel.y > 9.5f);  // W4M kWE 4 Backflip
        else assert(ev == 5 && a > 1.5f && a < 1.7f && c.vel.y > 9.5f);  // kWE 5 Fwdflip: FwdFlipVx 0.031623 (0x95fb88), vy 0.2
    }
}

// Dynamite: the worm walks away while the fuse burns, can't fire again, and the blast ends the turn.
static void checkDynamite() {
    Game g;
    g.start({37, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    g.objects.clear();  // no mine on the retreat path
    int first = g.current, dyn = weaponNamed("Dynamite");
    Worm &w = g.worms[first];
    g.weapon = dyn, g.ammo[w.team][dyn] = 2, g.delays[w.team][dyn] = 0;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    g.step(Input{});  // released: dropped
    // W4M Dynamite: PostLaunchDelay 0, RetreatTimeOverride 5000 ms, LifeTime 7000: the retreat ends before the blast, which the turn waits for
    assert(g.phase == Phase::Flying && g.shots.size() == 1 && g.ammo[w.team][dyn] == 1 && g.timer == msTicks(5000) - 1 && g.retreating());  // the fire tick counts
    Vector3 start = w.pos;
    int t = 0, settleAt = -1;
    for (; !g.shots.empty() && t < 60 * 20; t++) {
        Input in;
        in.walk = t < 150 ? -127 : 0;
        in.buttons = t % 2 ? Input::FIRE : 0;
        assert(g.phase == (settleAt < 0 ? Phase::Flying : Phase::Settle) && g.shots.size() == 1);
        g.step(in);
        if (settleAt < 0 && g.phase == Phase::Settle) settleAt = t + 1;
    }
    assert(settleAt == msTicks(5000) - 1 && std::abs(t - (int)(WEAPONS[dyn].fuse * 60)) <= 2 && g.ammo[w.team][dyn] == 1);
    assert(Vector3Distance(w.pos, start) > 3 && g.phase == Phase::Settle);
    for (t = 0; t < 60 * 20 && !(g.phase == Phase::Aim && g.current != first); t++) g.step(Input{});  // a crate may fall first
    assert(g.current != first);
}

// A shot leaving the map flies on (Flying, camera on it) until it falls into the sea with a splash.
static void checkOffMapShot() {
    Game g;
    g.start({41, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    const float W = Terrain::NX * Terrain::VOX;
    a.pos = {W - 2, 40, W / 2}, a.vel = {0, 0, 0}, a.yaw = PI / 2, a.pitch = 0.6f;  // edge, aiming out
    g.hotSeat = 0, g.wind = 0, g.weapon = weaponNamed("Bazooka");
    Input fire;
    fire.buttons = Input::FIRE;
    a.grounded = true;  // held standing: W4M CanFire needs state 0
    for (int t = 0; t < 120 && g.phase == Phase::Aim; t++) g.step(t < 45 ? fire : Input{}), a.pos = {W - 2, 40, W / 2}, a.vel = {0, 0, 0}, a.grounded = true;  // half power: Gravity.Slow carries a full one past W + 100
    assert(g.phase == Phase::Flying);
    float far = 0;
    bool splash = false;
    while (!g.shots.empty()) {  // the retreat may run out first: Settle waits for the shot
        far = fmaxf(far, g.shots[0].pos.x);
        g.step(Input{});
        for (const GameEvent &e : g.events) splash |= e.kind == GameEvent::Splash && e.worm < 0;
    }
    assert(far > W + 20 && splash && g.shots.empty());
}

// W4M: a dropped crate holds the turn (no clock, no control) until it lands.
// W4M: a crate falling mid-turn holds nothing: the turn clock runs and the worm walks (only EndTurn waits for its "Crate Spawn")
static void checkCrateHold() {
    Game g;
    g.start({43, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &w = g.worms[g.current];
    Object c = {Object::Crate, Vector3Add(w.pos, {3, 12, 0}), {0, 0, 0}, -1, -1, false, false};
    c.spawning = true;
    g.objects.push_back(c);
    int timer = g.timer;
    Vector3 p = w.pos;
    Input in;
    in.walk = 127;
    for (int t = 0; t < 30; t++) g.step(in);
    assert(g.active() && g.timer == timer - 30 && Vector3Distance(p, w.pos) > 0.5f);
}

static void checkCrateBetweenTurns() {
    GameConfig c{43, 2, 1, "", 0};
    c.scheme.crateChance = 100, c.scheme.mines = c.scheme.barrels = 0;
    Game g;
    g.start(c), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0, g.timer = 1;
    int first = g.current, landed = -1, t = 0;
    bool fell = false;
    for (; t < 60 * 40 && g.current == first; t++) {
        g.step(Input{});
        bool spawning = false;
        for (const Object &o : g.objects) spawning |= o.spawning;
        assert(g.phase != Phase::Aim || g.current == first || !spawning);
        if (g.phase == Phase::Settle && spawning) fell = true;
        else if (fell && landed < 0) landed = t;
    }
    assert(fell && landed >= 0 && g.current != first && t - landed >= Game::POST_ACTIVITY);
}

// W4M: the turn ends as soon as the active worm takes damage, without retreat time.
static void checkSelfHurtEndsTurn() {
    Game g;
    g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.shots = {{g.worms[g.current].pos, {0, 0, 0}, clusterWeapon(), 0, true, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.worms[g.current].hp < 100 && g.phase == Phase::Settle);
}

// The match ends once a team is wiped out, even with a shotgun shot still pending; both wiped = draw.
static void checkWipeEndsMatch() {
    for (int draw = 0; draw < 2; draw++) {
        Game g;
        g.start({39, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        int me = g.worms[g.current].team;
        g.weapon = weaponNamed("Shotgun"), g.shotsLeft = 1;
        for (Worm &w : g.worms) if (w.team != me || draw) w.hp = w.counted = 0, w.alive = false;  // died: stdvs Worm_Died ends the turn
        for (int t = 0; t < 60 * 20 && g.phase != Phase::GameOver; t++) g.step(Input{});
        assert(g.phase == Phase::GameOver && g.winner == (draw ? -1 : me));
    }
}

static void checkHotSeat() {
    Game g;
    g.start({35, 2, 1, "", 0});
    int timer = g.timer;
    assert(g.hotSeat == g.cfg.scheme.hotSeat * 60);
    g.step(Input{});
    assert(g.timer == timer && g.clock == 1);  // turn clock frozen; the round clock runs (TimerLogicEntity 0x50f17d)
    Input blimp;
    blimp.buttons = Input::TARGET;
    g.step(blimp);
    assert(g.hotSeat > 0);  // W4M: the CameraSelect group (Blimp view) sends no SomeInputFrom
    Input fire;
    fire.buttons = Input::FIRE;
    Game h = g;
    h.step(fire);
    assert(h.hotSeat == 0 && h.shots.empty() && h.ammo[h.worms[h.current].team][h.weapon] == g.ammo[g.worms[g.current].team][g.weapon]);  // consumed, no launch
    h.step(fire);
    assert(h.ammo[h.worms[h.current].team][h.weapon] == g.ammo[g.worms[g.current].team][g.weapon]);  // held: still no new press
    Input in;
    in.turn = 50;
    g.step(in);
    g.step(Input{});
    assert(!g.hotSeat && g.timer < timer && g.clock > 0);  // any input starts the turn
    Game c;
    c.start({35, 2, 1, "", 0});
    Input cam;
    cam.flags = Input::CAMERA;
    c.step(cam);
    assert(!c.hotSeat);  // W4M: the InGame group's camera keys send SomeInputFrom too
}

// W4M SkipgoUtilityLogicEntity: PostLaunchDelay 3 s, worm frozen, then a 0 s retreat ends the turn.
static void checkSkipGo() {
    Game g;
    g.start({35, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &w = g.worms[g.current];
    g.weapon = weaponNamed("Skip Go");
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    g.step(Input{});
    assert(g.phase == Phase::Retreat && !g.retreating());
    Vector3 p = w.pos;
    Input walk;
    walk.walk = 127;
    for (int t = 0; t < 170; t++) g.step(walk);
    assert(g.phase == Phase::Retreat && Vector3Distance(p, w.pos) < 0.01f);
    for (int t = 0; t < 20; t++) g.step(Input{});
    assert(g.phase == Phase::Settle);
}

static void checkScheme() {
    GameConfig c{37, 2, 2, "", 0};
    c.scheme.health = 150;
    c.scheme.mines = c.scheme.barrels = 0;
    c.scheme.crateChance = 0;
    c.scheme.weapons = Scheme::SET_BNG;
    c.scheme.turnTime = 20;
    Game g;
    g.start(c), g.hotSeat = 0;
    for (const Worm &w : g.worms) assert(w.hp == 150);
    assert(g.objects.empty() && g.timer == 20 * 60);
    for (size_t i = 0; i < WEAPONS.size(); i++) {
        Kind k = WEAPONS[i].kind;
        if (k == Kind::Rope || k == Kind::Sheep) assert(g.ammo[0][i] == 0);
        if (WEAPONS[i].name == "Bazooka") assert(g.ammo[0][i] == -1);
    }
    c.scheme.fallDamage = 0;
    g.start(c), g.hotSeat = 0;
    Worm &w = g.worms[0];
    w.pos.y += 15;  // long drop
    w.vel = {0, 0, 0};
    w.grounded = false;
    for (int t = 0; t < 200; t++) g.step(Input{});
    assert(w.hp == 150 || !w.alive);
}

static int hitHp(uint32_t wormpot, int weapon = -1) {  // victim hp after a cluster bomblet lands on it
    Game g;
    GameConfig c{6, 2, 1, "", 0};
    c.wormpot = wormpot;
    g.start(c), g.hotSeat = 0;
    int victim = 1 - g.current;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, weapon < 0 ? clusterWeapon() : weapon, 0, true, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    return g.worms[victim].hp;
}

static float walked(uint32_t wormpot) {
    Game g;
    GameConfig c{31, 2, 1, "", 0};
    c.wormpot = wormpot;
    g.start(c), g.hotSeat = 0;
    settle(g);
    Vector3 p = g.worms[g.current].pos;
    Input in;
    in.walk = 127;
    for (int t = 0; t < 30; t++) g.step(in);
    return Vector3Distance(p, g.worms[g.current].pos);
}

static void checkWormpot() {
    int normal = hitHp(0);
    assert(normal < 100 && 100 - hitHp(WP_DOUBLE_DAMAGE) >= 2 * (100 - normal) - 1);
    assert(hitHp(WP_WORMS_DROWN) == 100);
    assert(walked(WP_QUICK_WALK) > walked(0) * 1.5f);

    GameConfig c{41, 2, 3, "", 0};
    c.wormpot = WP_CRATE_DROPS | WP_GOLIATH << 8 | WP_VITAL_WORM << 16;
    Game g;
    g.start(c), g.hotSeat = 0;
    assert(g.worms[0].hp == 200 && g.worms[1].hp == 50 && (g.cfg.rules & RULE_KING));
    for (size_t i = 0; i < WEAPONS.size(); i++)
        if (WEAPONS[i].kind != Kind::SkipGo && WEAPONS[i].kind != Kind::Surrender) assert(g.ammo[0][i] == 0);
    c.wormpot = WP_ONE_SHOT;
    g.start(c), g.hotSeat = 0;
    for (const Worm &w : g.worms) assert(w.hp == 1);

    c.wormpot = WP_NO_JUMPING;
    g.start(c), g.hotSeat = 0;
    settle(g);
    Input jump;
    jump.buttons = Input::JUMP;
    g.step(jump);
    assert(!g.jumpDelay);
    c.wormpot = WP_LOW_GRAVITY << 16;
    g.start(c);
    assert(g.cfg.rules & RULE_LOW_GRAVITY);
    for (int m = WP_SUPER_EXPLOSIVES; m < WP_MODES; m++) assert(runRules(0, 44, Scheme{}, m) == runRules(0, 44, Scheme{}, m));
    assert(runRules(0, 44, Scheme{}, WP_DOUBLE_DAMAGE) != runRules(0, 44, Scheme{}));  // the wormpot is part of the checksum
}

// The modes Wormpot.lub and WormpotService set up, one assert each (W4M ids, docs/w4m/turn.md Wormpot).
static void checkWormpotModes() {
    const int cg = weaponNamed("Cluster Grenade");
    int normal = 100 - hitHp(0, cg);
    assert(100 - hitHp(WP_SUPER_CLUSTERS, cg) >= 2 * normal - 1 && hitHp(WP_SUPER_EXPLOSIVES, cg) == 100 - normal);  // kWeaponClusterBomb is a cluster container
    Worm bat = melee("Baseball Bat", 1.6f), hard = melee("Baseball Bat", 1.6f, 0, WP_SUPER_MELEE);
    assert(100 - hard.hp == 2 * (100 - bat.hp) && Vector3Length(hard.vel) > 1.9f * Vector3Length(bat.vel));
    checkShotgun(WP_SUPER_FIREARMS);

    Game g;
    GameConfig c{41, 2, 2, "", 0};
    c.wormpot = WP_NO_COWARDS | WP_ENERGY << 8;
    g.start(c);
    int sur = -1, dyn = -1;
    for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == Kind::Surrender) sur = (int)i; else if (WEAPONS[i].name == "Dynamite") dyn = (int)i;
    assert(g.cfg.scheme.retreatTime == 0 && g.retreatTicks(WEAPONS[dyn]) == msTicks(5000) && g.ammo[0][sur] == 0);  // RetreatTimeOverride stays
    for (const Worm &w : g.worms) assert(w.poison == Game::POISON_DEFAULT);

    for (uint32_t seed = 1; seed < 6; seed++) {  // Super Secret Weapons: one weapon per team, from its inventory, ids below 30
        c = GameConfig{seed, 2, 1, "", 0};
        c.wormpot = WP_SECRET_WEAPON;
        g.start(c);
        for (int t = 0; t < 2; t++) {
            int id = g.superWeapon[t], held = 0;
            for (size_t i = 0; i < WEAPONS.size(); i++) held += g.ammo[t][i] && g.containerOf((int)i, false) == id;
            assert(id > 0 && id < 30 && held);
        }
    }
    int t = g.worms[g.current].team;
    g.superWeapon[t] = Game::W4M_FACTORY;
    assert(g.superScale(Game::W4M_FACTORY).x == 2 && g.superScale(Game::W4M_FACTORY).y == 1 && g.superScale(1).x == 1);
    Worm &v = g.worms[g.current];
    v.hp = 300;
    for (int k = 0; k < 3; k++) g.hurt(v, 60, false, 4);
    assert(v.hp == 300 - 150);  // 0x5abaf0: the type-4 cap is 150 for the Factory super weapon's team

    c = GameConfig{31, 2, 1, "", 0};
    for (int on = 0; on < 2; on++) {
        c.wormpot = on ? WP_WIND_WORMS : 0;
        g.start(c), g.hotSeat = 0;
        g.wind = 1, g.windZ = 0;
        Worm &w = g.worms[g.current];
        w.pos = {20, 70, 20}, w.vel = {0, 0, 0}, w.grounded = false;
        for (int k = 0; k < 30; k++) g.step(Input{});
        assert(on ? w.pos.x > 20.2f : fabsf(w.pos.x - 20) < 0.01f);  // Wind.Speed x WindScale 0.5 on a flying worm
    }

    auto fresh = [&](uint32_t wp, uint32_t seed = 31, int per = 1) -> Game & {
        c = GameConfig{seed, 2, per, "", 0};
        c.wormpot = wp;
        g.start(c), g.hotSeat = 0;
        settle(g);
        return g;
    };
    Input walk, jump, target;
    walk.walk = 127, jump.buttons = Input::JUMP, target.buttons = Input::TARGET;
    for (uint32_t wp : {(uint32_t)WP_TUG_O_WORMS, (uint32_t)WP_JUMPING_ONLY}) {  // ArtilleryMode: no walk, no jump, no jetpack; VelocityScale 0: jumps only
        fresh(wp);
        Vector3 p = g.worms[g.current].pos;
        for (int k = 0; k < 30; k++) g.step(walk);
        assert(Vector3Distance(p, g.worms[g.current].pos) < 0.05f);
        g.step(jump);
        assert((g.jumpDelay > 0) == (wp == WP_JUMPING_ONLY));
        assert(g.allowed(g.worms[g.current].team, weaponNamed("Jetpack")) == (wp == WP_JUMPING_ONLY));
    }
    fresh(WP_NO_BLIMP).weapon = weaponNamed("Airstrike");  // Camera.Disable "Blimp": TARGET is ignored
    g.step(target);
    assert(!g.cursorOn && !g.blimp);

    fresh(WP_MINE_RESPAWN);  // Land.Indestructable, the mine back 500 ms later where it went off, never a dud
    {
        Worm &w = g.worms[g.current];
        std::vector<signed char> land = g.terrain.d;
        Object m = {Object::Mine, {w.pos.x + 6, w.pos.y, w.pos.z}, {0, 0, 0}, -1, 0.05f, false, false};
        m.fizzle = true, m.delay = 0;
        g.objects = {m};
        for (int k = 0; k < 10 && !g.objects.empty(); k++) g.step(Input{});
        assert(g.objects.empty() && g.terrain.d == land && g.respawns.size() == 1);
        for (int k = 0; k < 40; k++) g.step(Input{});
        assert(g.objects.size() == 1 && g.objects[0].type == Object::Mine && !g.objects[0].fizzle);
    }

    for (int on = 0; on < 2; on++) {  // Dim-Mak: the Prod takes all the energy left, no push
        Worm v = melee("Prod", 1, 0, on ? WP_DIM_MAK : 0);
        assert(on ? v.hp <= 0 && fabsf(v.vel.x) + fabsf(v.vel.z) < 0.01f : v.hp == 100);  // only gravity moves it
    }

    fresh(WP_SPECIALIST, 31, 4);  // 4 worms: classes 5 6 3 4 (Bazooka / Grenade / Shotgun / Rope sets)
    assert(g.special[0] == 5 && g.special[1] == 6 && g.special[2] == 3 && g.special[3] == 4);
    assert(g.ammo[0][weaponNamed("Bazooka")] == -1 && g.ammo[0][weaponNamed("Cluster Grenade")] == 3 && g.ammo[0][weaponNamed("Ninja Rope")] == 5);
    assert(g.delays[1][weaponNamed("Airstrike")] == 5 && g.delays[1][weaponNamed("Homing Missile")] == 1);
    {
        int t = g.worms[g.current].team, cls = g.special[g.current];
        assert(g.allowed(t, weaponNamed("Skip Go")) && g.allowed(t, weaponNamed("Bazooka")) == (cls == 5) && g.allowed(t, weaponNamed("Shotgun")) == (cls == 3));
    }

    fresh(WP_WIND_GUNS).wind = 1, g.windZ = 0;  // gun wobble x (1 + 2.5 Wind.Speed / MaxSpeed), sampled when the gun is wielded
    g.weapon = weaponNamed("Shotgun");
    for (int k = 0; k < 60; k++) g.step(Input{});
    assert(g.wobble.weapon == g.weapon && fabsf(g.wobble.f - 3.5f) < 1e-4f && (g.wobble.at.x != 0 || g.wobble.at.y != 0));
    fresh(0).weapon = weaponNamed("Shotgun");
    g.step(Input{}), g.step(Input{});
    assert(g.wobble.f == 1);
}

// Mystery crates (CreateRandomCrate 0x4fa4b0, CrateLogicEntity 0x5ca1f0): drawn at spawn by MysteryChance, opened on collection.
static void checkMystery() {
    Game g;
    GameConfig c{57, 2, 2, "", 0};
    c.scheme.healthShare = c.scheme.weaponShare = c.scheme.utilityShare = 0, c.scheme.mysteryShare = 100;
    g.start(c), g.hotSeat = 0;
    settle(g);
    assert(g.addObject(Object::Crate, 0) && g.objects.back().mystery >= 0 && g.objects.back().weapon < 0);
    Worm &w = g.worms[g.current];
    int team = w.team, hp = w.hp;
    g.openMystery(MY_HEALTH, w), assert(w.hp == hp + 25);
    g.openMystery(MY_SUPER_HEALTH, w), assert(w.hp == hp + 125);
    g.openMystery(MY_DAMAGE, w), assert(w.hp == hp + 100 && w.counted == w.hp);
    g.openMystery(MY_GOOD_POISON, w);
    for (const Worm &x : g.worms) assert((x.poison == Game::POISON_DEFAULT) == (x.team != team));
    g.openMystery(MY_BAD_POISON, w), assert(w.poison == Game::POISON_DEFAULT);
    float grav = g.gravity();
    g.openMystery(MY_LOW_GRAVITY, w), g.openMystery(MY_QUICK_WALK, w);
    assert(g.gravity() == grav / 2 && g.walkScale() == 2);
    int t0 = g.timer;
    g.openMystery(MY_DOUBLE_TIME, w), assert(g.timer == std::min(2 * t0, 99 * 60));
    int big = 0;
    for (const char *n : {"Concrete Donkey", "Holy Hand Grenade", "Super Airstrike"}) big += g.ammo[team][weaponNamed(n)];
    g.openMystery(MY_SPECIAL_WEAPON, w);
    int after = 0;
    for (const char *n : {"Concrete Donkey", "Holy Hand Grenade", "Super Airstrike"}) after += g.ammo[team][weaponNamed(n)];
    assert(after == big + 1);
    int total = 0, left = 0;
    for (int a : g.ammo[team]) total += a > 0 ? a : 0;
    g.openMystery(MY_DISARM, w);
    for (int a : g.ammo[team]) left += a > 0 ? a : 0;
    assert(left == total - 1);
    size_t objs = g.objects.size();
    g.openMystery(MY_MINE_LAYER, w), assert(g.objects.size() == objs + 5);
    float sea = g.water;
    g.openMystery(MY_FLOOD, w), assert(g.water > sea);
    Vector3 at = w.pos;
    g.openMystery(MY_TELEPORT, w), assert(Vector3Distance(at, w.pos) > 1);
    g.beginTurn(team);  // turn end: Low.Gravity.GameDefault, Worm.VelocityScale 1
    assert(g.gravity() == grav && g.walkScale() == 1);
}

// Weapon Factory: custom weapons append to the table at start(), survive a save/load, and fire.
static void checkCustomWeapons() {
    size_t base = WEAPONS.size();
    WeaponDef w = {"Test Lobber", Kind::Shell, 4, 70, 25, 2, 0.5f, 1.5f, 20, 2, 3, 1, false, 4};
    w.model = "holy", w.icon = "Secret Weapon";
    WeaponDef a = w;
    a.name = "Test Strike", a.kind = Kind::Airstrike, a.clusters = 3;
    assert(saveCustomWeapons("custom_weapons_test.json", {w, a}));
    std::vector<WeaponDef> list;
    assert(loadCustomWeapons("custom_weapons_test.json", list));
    remove("custom_weapons_test.json");
    {  // HomingAvoidLand survives the JSON round trip
        WeaponDef h = a;
        h.kind = Kind::Homing, h.avoid = true;
        std::vector<WeaponDef> hl;
        assert(saveCustomWeapons("custom_weapons_test.json", {h}) && loadCustomWeapons("custom_weapons_test.json", hl) && hl.size() == 1 && hl[0].avoid);
        remove("custom_weapons_test.json");
    }
    assert(list.size() == 2 && list[0].name == w.name && list[0].fuse == w.fuse && list[0].bounce == w.bounce && list[0].model == "holy" && list[1].kind == Kind::Airstrike);
    GameConfig c{51, 2, 2, "", 0};
    c.custom = list;
    Game g;
    g.start(c), g.hotSeat = 0;
    assert(WEAPONS.size() == base + 2 && WEAPONS[base].name == "Test Lobber" && g.ammo[0][base] == 2);
    assert(customWeapon((int)base) && !customWeapon((int)base - 1));
    assert(WEAPONS[base].postLaunch == 500 && WEAPONS[base + 1].postLaunch == 500 && WEAPONS[base].retreat == -1);  // kWeaponFactoryWeapon
    c.custom[1].kind = Kind::Homing;
    g.start(c), g.hotSeat = 0;
    assert(WEAPONS[base + 1].postLaunch == 0);  // kWeaponFactoryHoming
    {  // HomingAvoidLand (0x5611b0): heading at the ground 3 m away, the forward probe hits and the missile is pushed up, within MaxHomingSpeed 0.25
        float vy[2];
        for (int avoid = 0; avoid < 2; avoid++) {
            c.custom[1].kind = Kind::Homing, c.custom[1].avoid = avoid, c.custom[1].fuse = 0, c.custom[1].speed = 12;
            g.start(c), g.hotSeat = 0;
            Vector3 hit, p = g.worms[0].pos, tgt = Vector3Add(p, {30, 0, 0});
            assert(g.terrain.raycast({Vector3Add(p, {0, 20, 0}), {0, -1, 0}}, 40, &hit));
            g.shots = {{Vector3Add(hit, {0, 3, 0}), {0, -10, 0}, (int)base + 1, 2.0f, false, 1, tgt}};
            g.step(Input{});
            vy[avoid] = g.shots.empty() ? -99 : g.shots[0].vel.y;
            if (avoid) assert(!g.shots.empty() && Vector3Length(g.shots[0].vel) <= 12.5f + 0.01f);
        }
        assert(vy[1] > vy[0] + 5);
    }
    c.custom[1].kind = Kind::Airstrike, c.custom[1].avoid = false;
    g.start(c), g.hotSeat = 0;
    uint32_t with = g.checksum();
    for (size_t i = base; i < WEAPONS.size(); i++) {
        bool fired, again;
        c.seed = 99u + (int)i;
        uint32_t x = fireEach((int)i, fired, &c), y = fireEach((int)i, again, &c);
        assert(fired && again && x == y);
    }
    c.custom.clear();
    c.seed = 51;
    g.start(c), g.hotSeat = 0;
    assert(WEAPONS.size() == base && g.checksum() != with);
}

// W4M FuseUp: the grenade family's fuse is set on the d-pad (1..5 s) and is exact to the tick; other weapons ignore it.
static int fuseTicks(int presses, uint8_t key, const char *weapon, int *choir = nullptr) {
    Game g;
    g.start({28, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);  // the worms drop onto their nodes first
    Worm &a = g.worms[g.current];
    g.weapon = weaponNamed(weapon);
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    for (int i = 0; i < presses; i++) { Input in; in.buttons = key; g.step(in); g.step(Input{}); }
    a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.yaw = 0, a.pitch = 0.5f, a.grounded = true;  // standing: W4M CanFire
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    for (int n = 1; n < 60 * 10; n++) {  // n: ticks since the throw, up to its blast
        g.step(Input{});
        for (const GameEvent &e : g.events) {
            if (e.kind == GameEvent::Hallelujah && choir) *choir = n;
            if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) return n;
        }
    }
    return -1;
}

static void checkFuse() {
    assert(fuseTicks(0, 0, "Grenade") == 3 * 60);
    assert(fuseTicks(5, Input::FUSE_DOWN, "Grenade") == 60);
    assert(fuseTicks(9, Input::FUSE_UP, "Banana Bomb") == 5 * 60);
    assert(fuseTicks(1, Input::FUSE_UP, "Cluster Grenade") == 4 * 60);
    int choir = -1, holy = fuseTicks(3, Input::FUSE_DOWN, "Holy Hand Grenade", &choir);
    assert(choir > 0 && holy - choir + 1 == 2 * 60);  // W4M: comes to rest, Hallelujah, blast 2 s later; no fuse to set
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    g.hotSeat = 0;
    g.weapon = weaponNamed("Bazooka");
    Input in;
    in.buttons = Input::FUSE_UP;
    g.step(in);
    assert(g.fuses[g.worms[g.current].team] == 3);
}

// W4M "Will auto activate if selected": a long fall with the parachute in hand opens it (0x579720, under -0.225 units/ms); open
// (0x5792a0) it glides 0.06 units/ms along the facing plus 70 ms of the opening's wind and gravity, so it lands without damage.
static void checkParachute() {
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    std::fill(g.terrain.d.begin(), g.terrain.d.end(), (signed char)-127);
    for (int z = 0; z < Terrain::NZ; z++)  // a floor at 45 m
        for (int x = 0; x < Terrain::NX; x++) g.terrain.d[((size_t)z * Terrain::NY + 179) * Terrain::NX + x] = 127;
    g.objects.clear(), g.timer = 100000;
    Worm &a = g.worms[g.current];
    for (Worm &o : g.worms) if (&o != &a) o.pos = {5, 46, 5};
    g.hotSeat = 0, g.wind = 1, g.windZ = 0;
    int chute = weaponNamed("Parachute");
    g.weapon = chute, g.ammo[a.team][chute] = 1, g.delays[a.team][chute] = 0;
    a.pos = {20, 58, 20}, a.vel = {0, 0, 0}, a.grounded = false, a.yaw = 0;
    float x = a.pos.x, vy = 0;
    for (int t = 0; t < 120 && !g.chute; t++) vy = a.vel.y, g.step(Input{});
    assert(g.chute && g.ammo[a.team][chute] == 0 && vy >= -11.25f - 0.25f && vy < -11.25f + 0.25f);
    for (int t = 0; t < 60; t++) g.step(Input{});
    Vector3 want = {Game::WIND_ACCEL * 0.07f, -12.5f * 0.07f, 3};  // the 0.06 units/ms glide + 70 x (Wind.Speed, Gravity)
    assert(Vector3Distance(a.vel, want) < 0.02f);
    for (int t = 0; t < 60 * 30 && !a.grounded; t++) g.step(Input{});
    assert(a.grounded && a.hp == 100 && a.pos.x > x + 1 && !g.chute);  // landing closes it (0x57967e)
    // 0x578db0 on FIRE: refused on the ground, opens in the air (ammo spent), closes when open
    g.ammo[a.team][chute] = 2;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire), g.step(Input{});
    assert(!g.chute && g.ammo[a.team][chute] == 2);
    a.pos.y += 8, a.vel = {0, 0, 0}, a.grounded = false;
    g.step(Input{}), g.step(fire);
    assert(g.chute && g.ammo[a.team][chute] == 1 && g.chuteAng == 0);
    g.step(Input{}), g.step(fire);
    assert(!g.chute);
    // 0x578a40: no stick, the canopy hangs still and keeps the yaw; the stick to the right turns it 0.02 rad a step and sways it
    g.step(Input{}), g.step(fire), g.step(Input{});
    float yaw = a.yaw;
    assert(g.chute && g.chuteAng == 0 && a.yaw == yaw);
    Input right;
    right.buttons = Input::HEADING, right.turn = (int8_t)(lroundf((yaw + PI / 2) / PI * 128) & 0xff), right.walk = 127;
    const float K = Game::DT / 0.02f;
    g.step(right);
    assert(fabsf(a.yaw - (yaw + 0.02f * K)) < 1e-5f && g.chuteSpin < 0 && g.chuteAng < 0);
    Vector3 at = g.chuteAt;
    for (int t = 0; t < 30; t++) g.step(right);
    // hung 2 m under the canopy, 2 sin(angle) to the side
    Vector3 f = flat(a.yaw), off = Vector3Subtract(a.pos, g.chuteAt);
    assert(g.chute && fabsf(off.y + 2 * cosf(g.chuteAng)) < 0.05f && fabsf(off.x - 2 * sinf(g.chuteAng) * f.z) < 0.05f);
    assert(Vector3Distance(at, g.chuteAt) > 0.5f);
}

// W4M PayloadWeapon 0x5833a0 -> 0x549bb0: PostLaunchDelay after the launch, movement back and StartRetreatTimer, the shell still
// flying; Timer_RetreatTimedOut ends the turn, which waits for the shell. Its TrackCam is served at launch (0x577530), the worm still: full screen.
// PayloadTrackCamera (docs/camera-w4m.md 11.6): a shot whose first contact is more than 1 s away is served full screen from its
// launch and followed, from the shoulder view or the first-person aim, on a turn that follows an event camera
static void checkPayloadFollow() {
    for (const char *name : {"Grenade", "Cluster Grenade", "Banana Bomb", "Holy Hand Grenade", "Bazooka"}) for (int aim : {0, 1}) {
        Game g;
        g.start({23, 2, 2, "", 0}), g.hotSeat = 0;
        settle(g);
        g.wind = 0;
        Controls::reset();
        Camera3D cam = {{0, 60, 0}, g.worms[g.current].pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
        for (int turn = 0; turn < 2; turn++) {
            Worm &a = g.worms[g.current];
            g.weapon = weaponNamed(name);
            g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0, a.pitch = 0.7f;
            Controls::forceAim = aim;
            for (int t = 0; t < 120; t++) Controls::camera(cam, g, false, false, true, Game::DT), g.step(Controls::read(g, 0, true, Game::DT));
            for (int t = 0; t < 120 && g.shots.empty(); t++) {
                Input in = Controls::read(g, 0, true, Game::DT);
                in.buttons |= Input::FIRE;
                g.step(in);
                Controls::camera(cam, g, !g.shots.empty() && g.phase != Phase::Aim, false, true, Game::DT);
            }
            assert(!g.shots.empty());
            float tail = 1e9f;
            for (int t = 0; t < 60 * 6 && !g.shots.empty() && g.phase == Phase::Flying; t++) {
                Controls::camera(cam, g, true, false, true, Game::DT);
                Camera3D v;
                float show, grow;
                const Camera3D &seen = Controls::inset(v, show, grow) ? v : cam;  // a TrackCam served in the turn is the inset (0x51c000)
                if (t > 60) tail = fminf(tail, Vector3Distance(seen.target, g.shots[0].pos));
                g.step(Controls::read(g, 0, true, Game::DT));
            }
            assert(tail < 6);  // the shot's view looks at the shot, not at the worm
            Controls::forceAim = 0;
            for (int t = 0; t < 30; t++) g.step(Input{});
            g.phase = Phase::Aim, g.shots.clear(), g.power = 0, g.timer = 2000;
        }
    }
}

static void checkRetreatInFlight() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0, g.wind = 0;
    Worm &a = g.worms[g.current];
    int first = g.current;
    g.weapon = weaponNamed("Bazooka");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    a.pitch = 0.7f;
    Controls::reset();
    Camera3D cam = {{0, 60, 0}, a.pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    for (int t = 0; t < 90; t++) Controls::camera(cam, g, false, false, false, Game::DT);
    Input fire;
    fire.buttons = Input::FIRE;
    for (int t = 0; t < 40; t++) g.step(fire);
    g.step(Input{});
    assert(g.phase == Phase::Flying && g.shots.size() == 1 && !g.retreating());
    const int lock = msTicks(WEAPONS[g.weapon].postLaunch), end = lock + g.retreatTicks(WEAPONS[g.weapon]);
    assert(lock == 30 && g.timer == end - 1);
    Vector3 p0 = a.pos;
    float flown = 0;
    int t = 1, settleAt = -1, inset = 0;
    for (; t < 60 * 20 && !(g.phase == Phase::Aim && g.current != first); t++) {
        bool chase = g.phase != Phase::Aim && !g.shots.empty();
        Controls::camera(cam, g, chase, false, false, Game::DT);
        Camera3D v;
        float show, grow;
        inset += Controls::inset(v, show, grow) && !g.shots.empty() && g.phase == Phase::Flying;
        Input walk;
        walk.walk = t < 120 ? 127 : 0;
        Vector3 was = a.pos;
        g.step(walk);
        if (t < lock) assert(Vector3Distance(a.pos, was) < 1e-4f);  // Worm.WeaponDisableMovement until PostLaunchDelay ends
        if (!g.shots.empty() && g.phase == Phase::Flying) flown += Vector3Distance(a.pos, was);
        if (settleAt < 0 && g.phase == Phase::Settle) settleAt = t;
    }
    assert(flown > 1 && Vector3Distance(a.pos, p0) > 1 && inset > 0);  // the TrackCam served at launch during the worm's turn: the inset (0x51c000)
    assert(settleAt == end - 1 && g.current != first);
}

// Rope and jetpack: what the hand holds goes off without leaving the tool, which keeps working through the retreat.
static void checkToolWeapons() {
    for (const char *tool : {"Ninja Rope", "Jetpack"}) {
        Game g;
        g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
        Worm &a = g.worms[g.current];
        g.hotSeat = 0;
        bool rope = tool[0] == 'N';
        a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = false;
        if (rope) g.ropeMax = 25, g.ropeOn({20, 58, 20});
        else g.jetting = g.jetUsed = true, g.fuel = 6, g.thrust = 20;
        g.weapon = weaponNamed(tool), g.secondary = weaponNamed("Dynamite");  // W4M m_eSecondaryWeapon beside the tool
        int dyn = g.secondary;
        g.ammo[a.team][dyn] = 1, g.delays[a.team][dyn] = 0;
        Input fire;
        fire.buttons = rope ? Input::FIRE : Input::JUMP;  // W4M Fire.Second drops from the jetpack, FIRE thrusts
        g.step(fire);
        assert(g.phase != Phase::Aim && (rope ? g.roped : g.jetting) && g.ammo[a.team][dyn] == 0 && g.secondary < 0 && g.weapon == weaponNamed(tool));
        assert((g.phase == Phase::Flying || g.phase == Phase::Retreat) && (rope ? g.roped : g.jetting));
        if (!rope) {  // the dynamite still burning
            Input thrust;
            thrust.buttons = Input::FIRE;
            float vy = a.vel.y;
            g.step(thrust);
            assert(a.vel.y > vy);  // still thrusts
        }
        while (g.phase == Phase::Flying || g.phase == Phase::Retreat) g.step(Input{});
        assert(!g.roped && !g.jetting);
    }
}

// W4M JetpackUtilityLogicEntity 0x562810: FIRE takes off and thrusts 20 m/s² x sin((1 - h / 100 m) pi / 2), the fuel burns only
// while thrusting and runs dry at 20 ms; no button ends it, landing does; the ammo goes once, a landed jetpack takes off again.
static void checkJetpack() {
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.hotSeat = 0, g.wind = 0;
    const float DT = Game::DT, G = 12.5f, n = DT / 0.02f;
    int jp = weaponNamed("Jetpack");
    g.weapon = jp, g.ammo[a.team][jp] = 1, g.delays[a.team][jp] = 0;
    a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = false, a.yaw = 0;
    Input fire, none, jump;
    fire.buttons = Input::FIRE, jump.buttons = Input::JUMP;
    g.step(fire);
    assert(g.jetting && g.jetUsed && g.ammo[a.team][jp] == 0 && g.fuel == WEAPONS[jp].fuse && WEAPONS[jp].speed == 20);
    float vy = a.vel.y, h = a.pos.y - g.water, fuel = g.fuel;
    g.step(fire);
    assert(fabsf(a.vel.y - vy - (20 * sinf((1 - h / 100) * PI / 2) - G) * DT) < 1e-4f && fabsf(g.fuel - (fuel - DT)) < 1e-6f);
    fuel = g.fuel;
    for (int t = 0; t < 30; t++) g.step(none);
    g.step(jump);
    assert(g.jetting && g.fuel == fuel);  // coasting burns nothing, JUMP does not switch it off
    float yaw = a.yaw;
    Input turn;
    turn.turn = 127;
    for (int t = 0; t < 60; t++) g.step(turn);
    assert(fabsf(a.yaw - yaw - Game::JET_TURN) < 1e-3f);  // 2 x TurnRotationSpeed 0.0092 per 20 ms
    a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, yaw = a.yaw;
    Input head;
    head.buttons = Input::HEADING, head.turn = headingOf(yaw + 2);
    for (int t = 0; t < 60; t++) g.step(head);
    assert(g.jetting && fabsf(a.yaw - yaw - Game::JET_TURN) < 1e-3f);  // a stick heading turns it at the same rate
    a.pos = {20, 55, 20}, a.vel = {5, 0, 0}, a.yaw = 0;
    g.step(none);
    assert(fabsf(a.vel.x - 5 * powf(0.95f, n)) < 1e-4f);  // XZWindResNoThrust
    a.pos = {20, 55, 20}, a.vel = {5, 0, 0}, a.yaw = PI / 2;
    Input glide;
    glide.walk = 127;
    g.step(glide);
    assert(fabsf(a.vel.x - 5 * powf(0.95f, n)) < 1e-4f);  // D-pad forward alone: InputImpulse 0, so XZWindResNoThrust
    a.pos = {20, 55, 20}, a.vel = {5, 0, 0}, a.yaw = PI / 2;
    glide.buttons = Input::HEADING, glide.turn = headingOf(PI / 2);
    g.step(glide);
    assert(fabsf(a.vel.x - 5 * powf(0.999f, n)) < 1e-4f);  // XZWindResThrust: the stick leans along the motion
    a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.yaw = 0;
    g.boost = 0;
    Input ahead = fire;
    ahead.walk = 127;
    g.step(ahead);
    h = 55 - g.water;
    float t = 20 * DT * sinf((1 - h / 100) * PI / 2);
    assert(fabsf(a.vel.z - t * sinf(0.3f)) < 1e-4f && fabsf(a.vel.y - (t * cosf(0.3f) - G * DT)) < 1e-4f);  // FwdThrustRotation 0.3
    a.pos = {20, 55, 20}, a.vel = {0, -10, 0};
    g.step(fire);
    assert(g.boost > 0);  // SuperThrust: falling past 0.15 units/ms
    float water = g.water;
    g.water = -50, a.pos = {20, 55, 20}, a.vel = {0, 1, 0}, g.boost = 0;  // 105 m over the water: past MaxAltitude 2000 units
    g.step(fire);
    assert(fabsf(a.vel.y - (1 - G * DT)) < 1e-4f);
    g.water = water;
    g.fuel = 0.03f, a.pos = {20, 55, 20}, a.vel = {0, 0, 0};
    g.step(fire);
    g.step(fire);
    assert(!g.jetting && g.jetUsed && g.ammo[a.team][jp] == 0);  // dry: 0x562990, it falls
    Vector3 ground;
    assert(g.terrain.raycast({{20, 60, 20}, {0, -1, 0}}, 60, &ground));
    a.pos = Vector3Add(ground, {0, Game::R + 0.1f, 0}), a.vel = {0, 0, 0};
    int hp = a.hp;
    for (int k = 0; k < 600 && !a.grounded; k++) g.step(none);
    assert(a.grounded && g.phase == Phase::Aim);
    g.fuel = 5;
    g.step(fire);
    assert(g.jetting && g.ammo[a.team][jp] == 0);  // a landed jetpack takes off again on its own fuel
    a.vel = {0, -25, 0};
    for (int k = 0; k < 600 && g.jetting; k++) g.step(none);
    for (int k = 0; k < 30 && !a.grounded; k++) g.step(none);  // the pack lands it Ballistic with its tangential speed (0x5ae17a)
    assert(!g.jetting && a.grounded && a.hp == hp);  // landing ends it; no fall damage under the jetpack
    {  // W4M 0x562f72: a land contact under the feet lands the pack minus the normal speed, so no FallDamage; without the pack it hurts
        int loss[2];
        for (int jet = 1; jet >= 0; jet--) {
            Game h = g;
            Worm &b = h.worms[h.current];
            h.fuel = 5, h.jetting = jet, b.pos = Vector3Add(ground, {0, 12, 0}), b.vel = {0, -30, 0}, b.grounded = false, b.motion = {};
            int was = b.hp;
            for (int k = 0; k < 600 && !b.grounded; k++) h.step(none);
            assert(b.grounded && b.pos.y < ground.y + 2);  // landed, not bounced at 0.8 (walls and ceilings only)
            loss[jet] = was - b.hp;
        }
        assert(loss[1] == 0 && loss[0] > 0);
    }
    {  // W4M 0x562f72 / 0x5630dc: a foot against a wall lands the pack (v minus its normal part), a head alone bounces it at 1.8
        Terrain t = g.terrain;
        const float fy = 55 - Game::R;
        t.weld({40, fy + 0.5f, 20}, {0.5f, 1, 3});  // a face from the feet to above the heads
        Vector3 p = {38, 55, 20}, v = {10, 0, 0};
        bool landed = false;
        for (int k = 0; k < 40 && !landed; k++) landed = jetBody(t, p, v, 0);
        assert(landed && fabsf(v.x) < 1e-3f);  // foot and head hit the same face: the foot wins the tie
        Terrain u = g.terrain;
        u.weld({40, fy + 2.0f, 20}, {0.5f, 1.4f, 3});  // from 0.6 m over the feet: only the heads meet it
        p = {38, 55, 20}, v = {10, 0, 0};
        for (int k = 0; k < 40 && v.x > 0; k++) assert(!jetBody(u, p, v, 0));
        assert(v.x < -7.5f && v.x > -8.5f);  // 1.8 x (v.n) n: 10 -> -8
    }
    // with it out, the hand only takes what it drops (W4M 0x565d30 case 0)
    for (const char *w : {"Bazooka", "Shotgun", "Dynamite", "Landmine", "Sheep", "Teleport"}) g.ammo[a.team][weaponNamed(w)] = 1;
    a.pos.y += 10, a.vel = {0, 0, 0}, a.grounded = false;  // off the steep slope it landed on: a foot contact lands the pack again
    g.step(fire);
    assert(g.jetting);
    for (int k = 0; k < 2 * (int)WEAPONS.size(); k++) {
        Input next;
        next.buttons = k % 2 ? 0 : Input::NEXT_WEAPON;
        next.buttons |= Input::FIRE;
        g.step(next);
        assert(g.jetting && g.weapon == jp);  // the jetpack stays in hand
        assert(g.secondary < 0 || WEAPONS[g.secondary].name == "Dynamite" || WEAPONS[g.secondary].name == "Landmine" || WEAPONS[g.secondary].name == "Sheep");
    }
    Game c = g;
    c.fuel += 1;
    assert(c.checksum() != g.checksum());
    Ai cpu;  // the CPU has no switch-off either: in flight off its plan it just stops thrusting
    for (int k = 0; k < 5; k++) assert(!(cpu.think(g).buttons & (Input::JUMP | Input::FIRE)));
}

// End to end, the Switch path: take off, step to the dynamite (it becomes the secondary), thrust on, ZL (JUMP) lays it, still flying;
// a secondary left when the tool ends becomes the weapon in hand (W4M 0x565920).
static void checkJetpackSecondary() {
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.hotSeat = 0, g.wind = 0;
    int jp = weaponNamed("Jetpack"), dyn = weaponNamed("Dynamite");
    g.weapon = jp, g.ammo[a.team][jp] = 1, g.ammo[a.team][dyn] = 2;
    g.delays[a.team][jp] = g.delays[a.team][dyn] = 0;
    a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = false;
    Input fire, next, drop;
    fire.buttons = Input::FIRE, next.buttons = Input::FIRE | Input::NEXT_WEAPON, drop.buttons = Input::FIRE | Input::JUMP;
    g.step(fire);
    assert(g.jetting);
    for (int k = 0; k < 4 * (int)WEAPONS.size() && g.secondary != dyn; k++) g.step(k % 2 ? fire : next);
    assert(g.secondary == dyn && g.weapon == jp && g.jetting && g.held() == dyn);
    float vy = a.vel.y, fuel = g.fuel;
    g.step(fire);
    assert(a.vel.y > vy - 12.5f * Game::DT + 1e-3f && g.fuel < fuel);  // still thrusts
    size_t shots = g.shots.size();
    const Game h0 = g;
    g.cfg.scheme.retreatTime = 0;  // the jetpack has no own RetreatTime: it takes the scheme's
    g.step(drop);
    assert(g.shots.size() == shots + 1 && WEAPONS[g.shots.back().weapon].name == "Dynamite" && g.ammo[a.team][dyn] == 1);
    {  // W4M 0x585a58 / 0x585bc5: dropped off its feet, it starts 30 units (1.5 m) ahead along the worm's velocity and inherits it
        Game f = h0;
        Worm &c = f.worms[f.current];
        c.vel = {3, 0, 4};
        const Vector3 v0 = c.vel;
        f.step(drop);
        const auto &sh = f.shots.back();
        const Vector3 off = Vector3Subtract(sh.pos, launchPoint(WEAPONS[dyn], c.pos, c.yaw));
        const Vector3 want = Vector3Add(v0, Vector3Scale({sinf(c.yaw), 0, cosf(c.yaw)}, WEAPONS[dyn].speed));
        assert(fabsf(Vector3Length(off) - 1.5f) < 0.05f && Vector3Distance(sh.vel, want) < 0.3f);  // 1.5 m, velocity inherited + BasePower along the facing
    }
    assert(g.jetting && g.weapon == jp && g.secondary < 0 && g.phase != Phase::Aim);  // the attack is made, the flight goes on
    assert(g.timer >= g.retreatTicks(WEAPONS[dyn]) - 1);  // the dynamite's retreat, not the jetpack's (scheme retreat 0 ended the turn at once)
    {  // landed during the retreat, the jetpack takes off again while fuel lasts (W4M, observed)
        Game l = g;
        l.step(Input{});
        l.jetting = false, l.worms[l.current].grounded = true;
        assert(l.jetLanded() && (l.phase == Phase::Flying || l.phase == Phase::Retreat));
        l.step(fire);
        assert(l.jetting);
    }
    Game h;  // dry with the dynamite still held: it comes to hand
    h.start({29, 2, 1, "", 0}), h.hotSeat = 0;
    Worm &b = h.worms[h.current];
    h.hotSeat = 0, h.weapon = jp, h.ammo[b.team][jp] = 1, h.ammo[b.team][dyn] = 1, h.delays[b.team][jp] = h.delays[b.team][dyn] = 0;
    b.pos = {20, 55, 20}, b.vel = {0, 0, 0}, b.grounded = false;
    h.step(fire);
    h.secondary = dyn, h.fuel = 0.01f;
    h.step(fire);
    assert(!h.jetting && h.weapon == dyn && h.secondary < 0);
}

// W4M: a jetpack worm drowns from Override with its Velocity (0x5ad640); Worm.Damaged.Current runs Lua EndTurn in that frame, which
// kills the jetpack (Jetpack.Kill); the float (0x5aa130) and the dropped dynamite keep their own clocks, the next turn waits for both
static void checkJetpackDrown() {
    const int jp = weaponNamed("Jetpack"), dyn = weaponNamed("Dynamite");
    for (int mode = 0; mode < 3; mode++) {  // 0: dynamite dropped, 1: no secondary, 2: dynamite dropped, retreat over before the fall
        Game g;
        g.start({1, 2, 2, "", 0}), g.hotSeat = 0;
        settle(g);
        const int cur = g.current;
        Worm &a = g.worms[cur];
        g.weapon = jp, g.ammo[a.team][jp] = 1, g.ammo[a.team][dyn] = 1, g.delays[a.team][jp] = g.delays[a.team][dyn] = 0;
        Input fire, drop;
        fire.buttons = Input::FIRE, drop.buttons = Input::FIRE | Input::JUMP;
        g.step(fire);
        assert(g.jetting);
        if (mode != 1) {
            g.secondary = dyn, g.step(drop);
            assert(g.shots.size() == 1 && g.phase == Phase::Flying && g.jetting);
        }
        const Vector3 near = {a.pos.x, g.water + 1.5f, a.pos.z};
        Vector3 at = near;
        for (int k = 0; k < 4000 && g.terrain.solid({at.x, at.y - 3, at.z}); k++)  // open sea
            at = Vector3Add(near, Vector3Scale(k % 4 < 2 ? Vector3{1, 0, 0} : Vector3{0, 0, 1}, (k % 2 ? -0.2f : 0.2f) * (k / 4)));
        a.pos = at, a.vel = {};
        if (mode == 2) g.timer = 1;
        int drownT = -1, upT = -1, deathT = -1, boomT = mode == 1 ? 0 : -1, aimT = -1;
        for (int t = 0; t < 60 * 30 && aimT < 0; t++) {
            g.step(Input{});
            if (mode == 2 && t == 0) assert(g.phase == Phase::Settle && !g.jetting && a.alive);  // EndTurn: Jetpack.Kill, the worm falls
            if (drownT < 0 && a.drowned) {
                drownT = t;
                assert(!g.jetting && g.phase == Phase::Settle && g.dying() == cur && !a.alive);  // jetpack gone, turn over, "Worm Dying"
            }
            if (upT < 0 && a.floatT > 0) upT = t;
            for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death && e.worm == cur) deathT = t;
            if (boomT < 0 && g.shots.empty()) boomT = t;  // the dynamite's blast, on its own fuse
            if (drownT >= 0 && g.phase == Phase::Aim) aimT = t;  // the next turn
        }
        assert(drownT >= 0 && upT > drownT && deathT - upT == Game::DROWN_FLOAT - 1 && boomT >= 0);
        assert(aimT >= std::max(deathT, boomT) + Game::POST_ACTIVITY && (mode == 1 || deathT < boomT));  // the float waits for no dynamite
    }
}

// Tool gaps closed against W4M: landing keeps the secondary (0x562f72), the panel keeps UtilityFire (0x602ea0), D-pad
// forward (Jetpack.Forward), the HeadCam zoom (0x54b6c0), Wormpot No Bombing (0x5662f6), the girder legs and probes.
static void checkToolGaps() {
    auto pr = [](uint8_t b) { Input in; in.buttons = b; return in; };
    int jp = weaponNamed("Jetpack"), dyn = weaponNamed("Dynamite");
    Input fire = pr(Input::FIRE), none, drop = pr(Input::FIRE | Input::JUMP);
    auto fly = [&](Game &g, uint32_t wp) -> Worm & {
        GameConfig c{29, 2, 1, "", 0};
        c.wormpot = wp;
        g.start(c), g.hotSeat = 0;
        Worm &a = g.worms[g.current];
        g.hotSeat = 0, g.wind = 0, g.weapon = jp, g.ammo[a.team][jp] = 1, g.ammo[a.team][dyn] = 2;
        g.delays[a.team][jp] = g.delays[a.team][dyn] = 0;
        a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = false, a.yaw = 0;
        g.step(fire);
        assert(g.jetting);
        return a;
    };
    {  // 1. landed with fuel: the jetpack stays in hand with its secondary; it takes off again and drops it
        Game g;
        Worm &a = fly(g, 0);
        g.secondary = dyn;
        Vector3 ground;
        assert(g.terrain.raycast({{20, 60, 20}, {0, -1, 0}}, 60, &ground));
        a.pos = Vector3Add(ground, {0, Game::R + 0.3f, 0}), a.vel = {0, -3, 0};
        for (int k = 0; k < 300 && g.jetting; k++) g.step(none);
        assert(!g.jetting && a.grounded && g.weapon == jp && g.secondary == dyn);
        Game h = g;
        g.step(fire);
        assert(g.jetting && g.secondary == dyn);
        g.step(drop);
        assert(g.secondary < 0 && g.ammo[a.team][dyn] == 1 && g.weapon == jp);
        size_t shots = h.shots.size();  // on the ground Fire.Second (PITCH, no TARGET) lays it: UtilityFire stays enabled
        h.step(pr(Input::PITCH));
        assert(h.secondary < 0 && h.shots.size() == shots + 1 && h.ammo[h.worms[h.current].team][dyn] == 1 && h.weapon == jp && !h.jetting);
        Game k;  // landed: a toolDrop() still becomes the secondary, anything else replaces the jetpack (0x565d30, +0x8d kept)
        Worm &c = fly(k, 0);
        c.pos = Vector3Add(ground, {0, Game::R + 0.3f, 0}), c.vel = {0, -3, 0};
        for (int n = 0; n < 300 && k.jetting; n++) k.step(none);
        assert(k.jetLanded());
        for (int n = 0; n < 4 * (int)WEAPONS.size() && k.weapon == jp; n++) {
            k.step(n % 2 ? none : pr(Input::NEXT_WEAPON));
            assert(k.secondary < 0 || (k.weapon == jp && toolDrop(WEAPONS[k.secondary])));
        }
        assert(k.weapon != jp && !toolDrop(WEAPONS[k.weapon]) && k.secondary < 0);
    }
    {  // 2. panel open in flight: move and jump cut (WormMoving), thrust kept; B closes the panel so it never drops
        Game g;
        fly(g, 0);
        Ui::Hud hud;
        Input in;
        hud.input(g, in, true, 0, 1);  // a release clears the swallow
        hud.open = true;
        in.buttons = Input::FIRE | Input::JUMP | Input::HEADING, in.walk = 127, in.turn = 40;
        hud.input(g, in, true, 0, 2);
        assert(in.buttons == Input::FIRE && !in.walk && !in.turn);
    }
    {  // 3. D-pad up (Jetpack.Forward) sends walk without a heading: forward thrust, yaw kept
        Game g;
        Worm &a = fly(g, 0);
        Input fwd = fire;
        fwd.walk = 127;
        float yaw = a.yaw;
        a.vel = {0, 0, 0};
        g.step(fwd);
        assert(a.yaw == yaw && a.vel.z > 0.01f);  // tilted by FwdThrustRotation along the facing
    }
    {  // 4. HeadCam zoom: dips to 0.3 at 2 s, 0.40 at 2.7 s, back to 1 by 4 s, then relaxes x 1.06 / 20 ms
        Game g;
        g.scout.t = -1;
        assert(fabsf(g.scoutZoom(0.5f, 0.02f) - 0.53f) < 1e-5f && g.scoutZoom(0.99f, 0.02f) == 1);
        g.scout.t = 120;
        assert(fabsf(g.scoutZoom(1, Game::DT) - 0.3f) < 1e-4f);
        g.scout.t = 161;
        float z = g.scoutZoom(1, Game::DT);
        assert(fabsf(z - 0.4f) < 0.01f);  // 0.3 + 0.7 (1 - sin(pi 2683 / 4000))
        for (g.scout.t = 162; g.scout.t < Game::SCOUT_TICKS; g.scout.t++) z = g.scoutZoom(z, Game::DT);
        assert(z > 0.98f && z <= 1);
    }
    {  // 5. No Bombing: picking a payload under a tool ends the tool and wields it; the mode is on all three reels
        Game g;
        Worm &a = fly(g, WP_NO_BOMBING);
        assert(g.selectable(a.team, dyn) && !g.selectable(a.team, weaponNamed("Bazooka")));
        for (int k = 0; k < 4 * (int)WEAPONS.size() && g.jetting; k++) g.step(k % 2 ? fire : pr(Input::FIRE | Input::NEXT_WEAPON));
        assert(!g.jetting && !g.jetUsed && toolDrop(WEAPONS[g.weapon]) && g.secondary < 0);  // 0x565650 ends it, the payload in hand
        assert(std::string(WORMPOT_MODES[WP_NO_BOMBING].name) == "No Bombing");
        for (int r = 0; r < 3; r++) assert(std::count(WORMPOT_REEL[r].begin(), WORMPOT_REEL[r].end(), (int)WP_NO_BOMBING) == 1);  // on every W4M reel
        uint32_t wp = wormpotSet(wormpotSet(0, 0, WP_NO_BOMBING), 2, WP_NO_BOMBING);
        assert(wormpotModes(wp) == 1ull << WP_NO_BOMBING && wormpotReel(wp, 0) == WP_NO_BOMBING && wormpotReel(wp, 1) == 0);
        assert(wormpotModes(wormpotSet(wp, 0, WP_EMPTY)) == 1ull << WP_NO_BOMBING);
    }
    {  // 7. direct pick (W4M 0x565d30): NEXT_WEAPON + aim = index + 1, aim unused that tick; aim 0 still steps
        Game g;
        Worm &a = fly(g, 0);
        int baz = weaponNamed("Bazooka");
        g.ammo[a.team][baz] = 1, g.delays[a.team][baz] = 0;
        Input p = Input::pick(dyn);
        p.buttons |= Input::FIRE;
        float pitch = a.pitch;
        g.step(p);
        assert(g.jetting && g.weapon == jp && g.secondary == dyn && a.pitch == pitch);  // a payload: the secondary
        g.step(Input::pick(dyn));
        assert(g.secondary == dyn);  // level-triggered, idempotent
        g.step(Input::pick(baz));
        assert(!g.jetting && g.weapon == baz && g.secondary < 0);  // anything else ends the tool (0x565650)
        assert(Controls::tick(Input::pick(baz)).aim == Input::pick(baz).aim);  // the client keeps the pick in the aim byte
        Game h;
        h.start({29, 2, 1, "", 0}), h.hotSeat = 0;
        int was = h.weapon;
        h.step(pr(Input::NEXT_WEAPON));
        assert(h.weapon != was);  // old inputs: a plain step
    }
    for (const char *tool : {"Ninja Rope", "Parachute"})  // rope and open parachute: a Sheep pick is the secondary, a Bazooka pick ends the tool
        for (const char *pk : {"Sheep", "Bazooka"}) {
            Game g;
            g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
            Worm &a = g.worms[g.current];
            bool rope = tool[0] == 'N';
            a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = false;
            if (rope) g.ropeMax = 25, g.ropeOn({20, 58, 20});
            else g.chuteOpen(a);
            int pw = weaponNamed(pk);
            g.weapon = weaponNamed(tool), g.ammo[a.team][pw] = 1, g.delays[a.team][pw] = 0;
            g.step(Input::pick(pw));
            if (toolDrop(WEAPONS[pw])) assert((rope ? g.roped : g.chute) && g.weapon == weaponNamed(tool) && g.secondary == pw);
            else assert(!g.roped && !g.chute && g.weapon == pw && g.secondary < 0);
        }
    {  // 6. girder: legs along x at the z ends (BitArray3D 3 + x + 4 z), steps every 80 ms, the active worm counts in the probes
        static_assert(Game::GIRDER_TICKS == (80 * 60 + 500) / 1000, "4 updates of 20 ms");
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        g.hotSeat = 0;
        Worm &a = g.worms[g.current];
        g.weapon = weaponNamed("Girder"), g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        g.step(Input{});
        assert((g.girderFits(Vector3Add(a.pos, {0, 1, 0})) & 2));
        Vector3 at = a.pos;
        a.pos.y += 40;
        assert(!(g.girderFits(Vector3Add(at, {0, 1, 0})) & 2));
        a.pos = at;
        g.girder = Vector3Add(a.pos, {sinf(a.yaw) * 7, 6, cosf(a.yaw) * 7});
        g.step(pr(Input::TARGET));
        assert(!g.girderFits(g.girder));
        Vector3 c = g.girder;
        g.step(Input{});
        g.step(pr(Input::FIRE));
        assert(g.girders == 1);
        for (float x : {-1.5f, 0.0f, 1.5f}) assert(g.terrain.solid(Vector3Add(c, {x, -0.5f, 1.5f})) && g.terrain.solid(Vector3Add(c, {x, -0.5f, -1.5f})));
        assert(!g.terrain.solid(Vector3Add(c, {1.5f, -0.5f, 0})) && !g.terrain.solid(Vector3Add(c, {-1.5f, -0.5f, 0})));
    }
}

// W4M Weapon.Create 0x565770: a turn starts on the team's last weapon if still usable (0x50d900), else on the FIRST usable one
// of the list, Skip Go and Surrender skipped (0x5657bb).
// W4M gun mask 0x1c3f: a bullet stops on a mine (payload flag 8) or an oil drum (0x10) in its way, not only on worms and crates.
static void checkGunObjects() {
    Game g;
    g.start({29, 2, 1, "", 0});
    g.objects.clear();
    g.worms[g.current].pos.y = g.landTop() + 5;  // clear of the land
    const Worm &a = g.worms[g.current];
    int b = g.current == 0 ? 1 : 0;
    g.worms[b].pos = Vector3Add(a.pos, {0, 0, 6});
    Ray r = {a.pos, {0, 0, 1}};
    assert(g.gunRay(r, a).worm == b);
    for (Object::Type t : {Object::Mine, Object::Barrel}) {
        g.objects = {Object{t, Vector3Add(a.pos, {0, 0, 3}), {0, 0, 0}, -1, -1, false, false}};
        Game::GunHit h = g.gunRay(r, a);
        assert(h.worm == -1 && !h.land && fabsf(h.dist - 3) < 0.01f);
    }
}

// W4M stdvs Initialise: SchemeData MineFactoryOn creates the factory at a random spot; each DoOncePerTurnFunctions counts it down.
static void checkSchemeFactory() {
    GameConfig c{29, 2, 1, "", 0};
    Game g;
    g.start(c);
    assert(!g.factory.on);
    c.scheme.mineFactory = 1;
    g.start(c);
    assert(g.factory.on && g.factory.wait == g.factoryData.inactive);
    endSettle(g);
    assert(g.factory.wait == g.factoryData.inactive - 1);
}

// W4M GameLogic.Turn.Started (0x566d57): every turn starts with the empty hand (kWeaponUndefined); FIRE does nothing.
static void checkEmptyHand() {
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    int t = g.worms[g.current].team, other = 1 - t, skip = weaponNamed("Skip Go");
    assert(g.weapon == -1);
    g.weapon = skip, g.ammo[t][skip] = -1, g.delays[t][skip] = 0;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    for (int k = 0; k < 60 * 60 && g.worms[g.current].team == t; k++) g.step(Input{});
    assert(g.worms[g.current].team == other && g.weapon == -1 && g.picked[t] == skip);
    std::vector<int> before = g.ammo[other];
    g.hotSeat = 0;
    for (int k = 0; k < 30; k++) g.step(k % 2 ? Input{} : fire);
    assert(g.phase == Phase::Aim && g.ammo[other] == before && g.shots.empty());
    Input next;
    next.buttons = Input::NEXT_WEAPON;
    g.step(next);
    assert(g.weapon >= 0 && g.usable(other, g.weapon));
}

// W4M AlienAbductionLogicEntity: the UFO lifts every worm in reach, nearest first, holds them, then spits them out 2.1 s apart at half health.
static void checkAbduction() {
    Game g;
    g.start({29, 2, 2, "", 0}), g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    std::vector<int> foes;
    for (int i = 0; i < (int)g.worms.size(); i++) if (g.worms[i].team != a.team) foes.push_back(i); else if (i != g.current) g.worms[i].pos = {70, 40, 70};
    g.hotSeat = 0;
    a.pos = {20, 55, 20}, a.yaw = 0, a.pitch = 0, a.grounded = true;  // standing: W4M CanFire
    g.weapon = weaponNamed("Alien Abduction");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Vector3 tgt = g.target(), hit;
    for (int k = 0; k < 2; k++) {
        Worm &v = g.worms[foes[k]];
        Vector3 top = Vector3Add(tgt, {k * 1.5f, 6, 0});
        assert(g.terrain.raycast({top, {0, -1, 0}}, 20, &hit));
        v.pos = Vector3Add(hit, {0, Game::R + 0.05f, 0}), v.vel = {0, 0, 0}, v.hp = 90 - 10 * k;
    }
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    const Projectile *u = g.ufo();
    assert(u && u->stage == Game::ABD_ARRIVING && g.abductees.size() == 2);  // AbductStart; 0x5488e0 sorts by distance to the saucer
    assert(Vector3Distance(g.worms[g.abductees[0].worm].pos, u->pos) <= Vector3Distance(g.worms[g.abductees[1].worm].pos, u->pos));
    int first = g.abductees[0].worm, second = g.abductees[1].worm, hp1 = g.worms[first].hp, hp2 = g.worms[second].hp, t = 0;
    for (; t < 60 * 10 && g.ufo()->stage == Game::ABD_ARRIVING; t++) g.step(Input{});
    assert(fabsf(t - Game::ABD_ARRIVE * 60) < 3 && g.abdCam == first && g.worms[first].hp == hp1);  // no damage until it is spat out
    float y0 = g.worms[first].pos.y;
    g.step(Input{});
    assert(fabsf(g.worms[first].pos.y - y0 - 4 * Game::DT) < 0.01f);  // NormalSpeed 1.6 units per 20 ms: the saucer is under AverageHeight
    for (t = 0; t < 60 * 20 && g.ufo()->stage == Game::ABD_LIFTING; t++) g.step(Input{});
    assert(g.ufo()->stage == Game::ABD_HOLDING && g.aboard(first) && g.aboard(second));
    for (t = 0; g.ufo()->stage == Game::ABD_HOLDING; t++) g.step(Input{});
    assert(fabsf(t - Game::ABD_HOLD * 60) < 3);  // AbductCloseBeam + AbductViolate + AbductOpenDoors - 250 ms
    g.step(Input{});
    const Worm &w1 = g.worms[first];
    Vector3 dir = Vector3Normalize(Vector3Subtract(tgt, g.ufo()->pos));
    assert(!g.aboard(first) && g.aboard(second) && w1.hp == hp1 - hp1 / 2 && w1.abducted);  // SpitOutWorm: half its health
    assert(fabsf(Vector3Length(w1.vel) - Game::ABD_OUT) < 0.5f && Vector3DotProduct(Vector3Normalize(w1.vel), dir) > 0.9f);  // back where it was taken
    for (t = 0; g.aboard(second); t++) g.step(Input{});
    assert(fabsf(t - Game::ABD_SPIT * 60) < 2 && g.worms[second].hp == hp2 - hp2 / 2);
    for (t = 0; g.efmvActive(); t++) g.step(Input{});
    assert(fabsf(t - (Game::ABD_SPIT + Game::ABD_LEAVE) * 60) < 3 && g.abductees.empty());  // the next SpitOutWorm finds nobody: AbductEnd
    // an abductee unhurt between two turn starts gets random health; a Zap while it moves (UpdateAbductee)
    Game h;
    h.start({29, 2, 1, "", 0}), h.hotSeat = 0;
    Worm &v = h.worms[1 - h.current];
    v.abducted = true, v.zap = Game::ZAP_FIRST;
    int turns = 0, hp = v.hp;
    for (int k = 0; k < 60 * 400 && turns < 2; k++) {
        h.step(Input{});
        for (const GameEvent &e : h.events) turns += e.kind == GameEvent::TurnStart;
    }
    assert(turns == 2 && v.calm == v.hp && v.hp != hp && v.hp < 100);
    {  // ApplyPoison 0x5ac060 runs in DoPostActivity before the damage is applied: rand % 100 == 0 leaves 0 hp and kills (no minimum)
        Game z0;
        z0.start({29, 2, 1, "", 0});
        int vi = 1 - z0.current;
        z0.worms[vi].abducted = true, z0.worms[vi].calm = z0.worms[vi].hp;
        settle(z0);
        z0.phase = Phase::Settle, z0.timer = -1;  // PostActivityTime's last tick: ApplyDamage, then DoPostActivity's ApplyPoison
        Game z;
        for (uint32_t s = 1; s < 5000; s++) {  // find a seed whose roll is 0
            z = z0, z.rng = s;
            z.step(Input{});
            if (z.worms[vi].hp == 0) break;
        }
        Worm &x = z.worms[vi];
        assert(z.crated && x.hp == 0 && x.alive);  // rolled first, then killed by the death queue
        for (int k = 0; k < 60 * 30 && z.phase != Phase::GameOver; k++) z.step(Input{});
        assert(!x.alive && z.phase == Phase::GameOver);
    }
    v.zap = 1, v.vel = {0.5f, 0, 0}, v.grounded = false, v.zapFound = true, v.zapSpot = Vector3Add(v.pos, {1, 0, 0});  // a spot already found (0x5a9cad)
    Vector3 was = v.pos;
    h.step(Input{});
    bool zap = false;
    for (const GameEvent &e : h.events) zap |= e.kind == GameEvent::Zap;
    assert(zap && Vector3Distance(was, v.pos) > 0.1f && v.zap >= Game::ZAP_MIN);
    // nobody in reach: AbductFail, then it leaves
    Game f;
    f.start({29, 2, 1, "", 0}), f.hotSeat = 0;
    Worm &b = f.worms[f.current];
    f.hotSeat = 0, b.pos = {20, 55, 20}, b.yaw = 0, b.pitch = 0, b.grounded = true;  // standing: W4M CanFire
    f.worms[1 - f.current].pos = {70, 40, 70};
    f.weapon = weaponNamed("Alien Abduction");
    f.ammo[b.team][f.weapon] = 1, f.delays[b.team][f.weapon] = 0;
    f.step(fire);
    assert(f.ufo() && f.ufo()->stage == Game::ABD_FAILING && f.abductees.empty());
    for (t = 0; f.efmvActive(); t++) f.step(Input{});
    assert(fabsf(t - Game::ABD_FAIL * 60) < 3);
    // W4M 0x5488e0 skips a worm with flag 0x20 (nailed) or 0x8 (in a bubble): the same victim, nailed, is out of reach
    for (int nailed = 0; nailed < 2; nailed++) {
        Game n;
        n.start({29, 2, 1, "", 0}), n.hotSeat = 0;
        Worm &c = n.worms[n.current], &vv = n.worms[1 - n.current];
        n.hotSeat = 0, c.pos = {20, 55, 20}, c.yaw = 0, c.pitch = 0, c.grounded = true;  // standing: W4M CanFire
        n.weapon = weaponNamed("Alien Abduction");
        n.ammo[c.team][n.weapon] = 1, n.delays[c.team][n.weapon] = 0;
        Vector3 at = n.target(), top;
        assert(n.terrain.raycast({Vector3Add(at, {0, 6, 0}), {0, -1, 0}}, 20, &top));
        vv.pos = Vector3Add(top, {0, Game::R + 0.05f, 0}), vv.vel = {0, 0, 0}, vv.nailed = nailed;
        n.step(fire);
        assert(n.ufo() && (n.ufo()->stage == Game::ABD_FAILING) == (nailed == 1));
    }
}

// W4M Super Sheep: walks, FIRE takes off (25 s flight), FIRE again blows it up.
static void checkSuperSheep() {
    Game g;
    g.start({26, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.weapon = weaponNamed("Super Sheep");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    for (int t = 0; t < 30; t++) g.step(Input{});
    assert(g.shots.size() == 1 && g.shots[0].stage == 0 && Vector3Length(g.shots[0].vel) < WEAPONS[g.weapon].speed);
    g.step(fire);
    assert(g.shots[0].stage == 1 && g.shots[0].vel.y > 0 && g.shots[0].fuse > WEAPONS[g.weapon].fuse - 0.1f && !g.retreating());  // FlyCam: no WormMoving
    Input steer;  // the left stick through Controls::tick: yaw and pitch change the heading
    steer.turn = 100, steer.aim = 100;
    Vector3 v0 = g.shots[0].vel;
    for (int t = 0; t < 10; t++) g.step(steer);
    assert(fabsf(atan2f(g.shots[0].vel.x, g.shots[0].vel.z) - atan2f(v0.x, v0.z)) > 0.1f && g.shots[0].vel.y > v0.y + 0.1f);
    for (int t = 0; t < 60 * 6 && g.phase == Phase::Flying; t++) g.step(Input{});
    assert(g.phase == Phase::Settle && g.shots.size() == 1);  // the retreat ran out mid-flight: the turn waits, still steered
    g.step(fire);
    assert(g.shots.empty());
}

// W4M Old Woman: steered, FIRE explodes, each enemy she bumps loses 1-8 of a weapon to her team.
static void checkOldWoman() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    int wi = weaponNamed("Old Woman"), baz = weaponNamed("Bazooka");
    for (int &n : g.ammo[v.team]) n = 0;
    g.ammo[v.team][baz] = 8, g.delays[v.team][baz] = 0, g.ammo[a.team][baz] = 0, g.delays[a.team][baz] = 0;
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{Vector3Add(v.pos, {0.1f, 0, 0}), {WEAPONS[wi].speed, 0, 0}, wi, 5, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    Input turn;
    turn.turn = 127;
    g.step(turn);
    int took = g.ammo[a.team][baz];
    assert(took >= 1 && took <= 8 && g.ammo[v.team][baz] == 8 - took && g.shots[0].prey == 1 - g.current);
    assert(fabsf(g.shots[0].vel.z) > 0);  // steered off its line
    g.step(Input{});
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    assert(g.shots.empty());
}

// W4M Concrete Donkey: starts max(75 m, Land.MaxHeight + 25 m) up, smashes on every landing (the blast Radius below the sphere centre,
// 80 damage out of the WEAPTWK blast) every 1.5 s + 85 ms until its 8 s LifeTime, whose Detonate blasts at the entity.
static void checkDonkey() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    int wi = weaponNamed("Concrete Donkey");
    const WeaponDef &wd = WEAPONS[wi];
    assert(wd.damage == 80 && wd.lift == 3.6f && wd.clusters == 0);
    Blast b = blastOf(wd, false);
    assert(g.blastDamage(b, {0, 0, 0}, {0, 0, 0}) == 80 && g.blastDamage(b, {0, 0, 0}, {b.reach + 1, 0, 0}) == 0);
    g.worms[0].pos = {5, 40, 5};
    g.phase = Phase::Flying, g.timer = 1200;
    Vector3 tgt = {40, g.terrain.raycast({{40, 200, 40}, {0, -1, 0}}, 400, &tgt) ? tgt.y : 0, 40};
    g.shots = {{{40, tgt.y + fmaxf(Game::DONKEY_MIN_HEIGHT, g.landTop() + Game::DONKEY_EXTRA), 40}, {0, -wd.speed, 0}, wi, 0, false, 1 << 30, {0, tgt.y + 100, 0}}};
    g.shots[0].aim = {0, g.shots[0].pos.y, 0};
    std::vector<float> at;
    float lastY = 0;
    for (int t = 0; t < (int)(Game::DONKEY_LIFE * 60) + 2 && !g.shots.empty(); t++) {
        g.step(Input{});
        for (const GameEvent &e : g.events)
            if ((e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) && e.weapon == wi) {  // a death blast has none
                at.push_back(t / 60.f), lastY = e.pos.y;
                if (at.size() == 1) assert(!g.shots.empty() && fabsf(e.pos.y - (g.shots[0].pos.y - wd.lift)) < 1e-3f);  // Explode at the centre - Radius
            }
    }
    (void)lastY;
    assert(g.shots.empty());
    assert(at.size() >= 5 && at.size() <= 7);  // the fall (1.6 s), a smash per 1.6 to 2 s, then the LifeTime blast
    for (size_t k = 1; k + 1 < at.size(); k++) assert(at[k] - at[k - 1] > 1.58f && at[k] - at[k - 1] < 2.2f);  // 1.585 s, plus the fall into the crater of the last smash
    assert(fabsf(at.back() - Game::DONKEY_LIFE) < 0.1f);
}

// W4M Mine.DudProbability: about one CreateMine mine in ten fizzles and stays inert; a laid mine never does.
static void checkMineDuds() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    g.objects.clear();
    int duds = 0;
    for (int k = 0; k < 400; k++) {
        g.objects = {g.newMine({5, 60, 5})};  // CreateMine rolls the dud
        g.objects[0].fuse = 0.01f;
        g.step(Input{});
        duds += !g.objects.empty() && g.objects[0].dud;
    }
    assert(duds > 15 && duds < 70);
    for (int k = 0; k < 50; k++) {  // a laid mine has no dud roll (only CreateMine sets the flag)
        g.objects = {{Object::Mine, {5, 60, 5}, {0, 0, 0}, -1, 0.01f, false, false}};
        g.step(Input{});
        assert(g.objects.empty() || !g.objects[0].dud);
    }
    g.objects = {g.newMine({5, 60, 5})};
    g.objects[0].dud = true, g.objects[0].fuse = -1;
    g.worms[0].pos = g.objects[0].pos;
    g.step(Input{});
    assert(g.objects[0].fuse < 0);  // a dud never re-arms
}

// W4M payload Explosion handler: a blast only pushes a mine, up and away; it does not arm it.
static void checkMineBlast() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    Vector3 at = g.worms[0].pos;
    for (Worm &w : g.worms) w.pos.x += 30;
    g.objects = {{Object::Mine, Vector3Add(at, {1.5f, -0.4f, 0}), {0, 0, 0}, -1, -1, false, false}};
    g.shots = {{{at.x, at.y - Game::R - 0.3f, at.z}, {0, 0, 0}, weaponNamed("Bazooka"), 0, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.shots.empty() && g.objects.size() == 1 && g.objects[0].fuse < 0 && g.objects[0].vel.y > 1 && g.objects[0].vel.x > 0);
}

// W4M Detonate 0x580f10 / Explode 0x57f140 for a landmine, Mine.DetonationType forced: BigPush keeps 0.3 of the reach (a worm 2.5 m
// off is only pushed, harder), Fire swaps the FX, Clusters blasts as kWeaponLandmineCluster (kind 3) then lays 5 bomblets 20 ms apart.
static void checkMineTypes() {
    struct Out { int hp; float kick; std::string fx; Game g; };
    auto blast = [](int type) {
        Out o;
        Game &g = o.g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0, g.mineDet = type;
        Worm &v = g.worms[1 - g.current];
        g.objects = {{Object::Mine, {5, 60, 5}, {0, 0, 0}, -1, 0.01f, false, false}};
        v.pos = {7.5f, 60, 5}, v.vel = {0, 0, 0};
        int hp = v.hp;
        g.shots.clear(), g.step(Input{});
        o.hp = hp - v.hp, o.kick = v.vel.x;
        for (const GameEvent &e : g.events) if ((e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) && e.fx) o.fx = e.fx;
        return o;
    };
    Out normal = blast(Game::DT_NORMAL), push = blast(Game::DT_BIGPUSH), fire = blast(Game::DT_FIRE), cl = blast(Game::DT_CLUSTERS);
    assert(normal.hp > 0 && normal.fx == "WXP_Explosion_Mine" && fire.hp == normal.hp && fire.fx == "WXP_Napalm");
    assert(push.hp == 0 && push.kick > normal.kick && push.fx == "WXP_Explosion_Mine");
    assert(cl.hp > 0 && cl.hp < normal.hp && cl.fx == "WXP_ExplosionX_Med" && cl.g.shots.size() == 5);
    const int stages[] = {0, 1, 2, 4, 5};
    for (size_t k = 0; k < 5; k++) {
        const Projectile &b = cl.g.shots[k];
        float v = Vector3Length(b.vel);
        assert(b.child && b.weapon == weaponNamed("Landmine") && b.stage == stages[k] && Vector3Distance(b.pos, {5, 60, 5}) < 0.1f);
        assert(v >= 5 - 1e-3f && v <= 12 + 1e-3f && acosf(b.vel.y / v) <= 0.27f + 1e-4f);
    }
}

// W4M ArmingRadius 45 units: a worm blown past a mine 2 m off arms it; a laid mine ignores worms for ArmingCourtesyTime.
static void checkMineFlyby() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    Worm &v = g.worms[1 - g.current];
    g.objects = {{Object::Mine, {5, 60, 5}, {0, 0, 0}, -1, -1, false, false}};
    v.pos = {1, 61, 6.8f}, v.vel = {20, 0, 0}, v.grounded = false;
    for (int t = 0; t < 30 && g.objects[0].fuse < 0; t++) g.step(Input{});
    assert(g.objects[0].fuse >= 0);
    g.objects = {{Object::Mine, v.pos, {0, 0, 0}, -1, -1, false, false, -1, -1, false, Game::MINE_COURTESY}};
    v.vel = {0, 0, 0};
    for (int t = 0; t < Game::MINE_COURTESY - 1; t++) g.step(Input{}), v.pos = g.objects[0].pos;
    assert(g.objects[0].fuse < 0);
    g.step(Input{}), g.step(Input{});
    assert(g.objects[0].fuse >= 0);
}

// open air over a floor at 45 m (voxel row 179), the active worm standing on it at (20, 45.5, 20) facing +z, the rope in hand
static Worm &ropeFloor(Game &g) {
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    std::fill(g.terrain.d.begin(), g.terrain.d.end(), (signed char)-127);
    for (int z = 0; z < Terrain::NZ; z++)
        for (int x = 0; x < Terrain::NX; x++) g.terrain.d[((size_t)z * Terrain::NY + 179) * Terrain::NX + x] = 127;
    g.objects.clear(), g.timer = 100000;
    Worm &a = g.worms[g.current];
    for (Worm &o : g.worms) if (&o != &a) o.pos = {5, 45.5f, 5};
    a.pos = {20, 45.5f, 20}, a.yaw = 0, a.pitch = 0, a.vel = {0, 0, 0};
    for (int t = 0; t < 30; t++) g.step(Input{});
    g.weapon = weaponNamed("Ninja Rope");
    g.ammo[a.team][g.weapon] = 5, g.delays[a.team][g.weapon] = 0;
    return a;
}

// W4M rope mode 2 (0x572800 / 0x573d00): the hook flies 1 unit/ms from the eye; a standing worm's hook catches a crate
// (0x571d90, mask 0x1e) without spending a shot; reeled in and out; jump lets it go.
static void checkRopeShots() {
    Game g;
    Worm &a = ropeFloor(g);
    assert(g.ambulatory(a));
    g.objects = {{Object::Crate, {20, 45.5f, 26}, {0, 0, 0}, -1, -1, false, false}};
    int ammo = g.ammo[a.team][g.weapon] = 3;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    assert(g.grapple.on && !g.objects[0].hooked);
    int t = 1;
    for (; t < 20 && !g.objects[0].hooked; t++) g.step(Input{});
    // the crate's sphere (0.25 + 0.5 m) first holds the point 5.83 m out, the 8th tick's; about the feet, by the point 10 units under its centre
    assert(t == 8 && g.ropeShots == 0 && !g.grapple.on && !g.roped && g.objects[0].hooked && fabsf(g.rope.len[0] - 6) < 0.1f && g.rope.n == 1);
    auto hang = [&] { return Vector3Distance({g.objects[0].pos.x, g.objects[0].pos.y - 0.5f, g.objects[0].pos.z}, g.rope.pt[0]); };
    Input reel;
    reel.aim = 127;  // stick up: shorter
    for (int k = 0; k < 14; k++) g.step(reel);  // 10 m/s (W4M 0x5713c0), a step refused while the floor cuts the stretch: out of reach
    assert(g.rope.len[0] < 4.1f && g.objects.size() == 1 && fabsf(hang() - g.rope.len[0]) < 0.05f);
    reel.aim = -127;
    for (int k = 0; k < 10; k++) g.step(reel);
    assert(g.rope.len[0] > 4.5f && g.objects[0].hooked && fabsf(hang() - g.rope.len[0]) < 0.05f);
    Input jump;
    jump.buttons = Input::JUMP;
    g.step(jump);
    assert(!g.objects[0].hooked && g.ammo[a.team][g.weapon] == ammo);  // the ammo goes at the rope's cleanup (0x5727b0)
    int rope = g.weapon;
    int bz = weaponNamed("Bazooka");
    g.ammo[a.team][bz] = 1, g.delays[a.team][bz] = 0;
    g.step(Input::pick(bz));
    assert(g.ammo[a.team][rope] == ammo - 1);
}

// 0x573d00: land within a tick's flight hooks (a NumShots shot), past MaxLength from the feet it retracts (free), FIRE in flight
// retracts; 0x570650: hooked since it last stood, the hook goes up, 45 deg toward the motion at 10 m/s
static void checkGrapple() {
    {
        Game g;
        Worm &a = ropeFloor(g);
        a.pitch = 1.45f;  // nothing above: a miss
        Input fire;
        fire.buttons = Input::FIRE;
        g.step(fire);
        int t = 1;
        for (; t < 60 && g.grapple.on; t++) g.step(Input{});
        assert(t == 27 && !g.roped && g.ropeShots == 0 && g.ropeUsed < 0);  // 22.5 m from the feet at 0.833 m a tick
        g.step(fire), g.step(Input{}), g.step(fire);  // launched, then retracted
        assert(!g.grapple.on && !g.roped);
        a.pitch = 0;
        for (int z = 118; z < 122; z++)  // a wall 9.5 m ahead, z 29.5 to 30.5 m
            for (int y = 180; y < 240; y++)
                for (int x = 0; x < Terrain::NX; x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = 127;
        g.step(Input{}), g.step(fire);
        for (t = 1; t < 30 && g.grapple.on; t++) g.step(Input{});
        assert(g.roped && g.ropeShots == 1 && g.ropeUsed == g.weapon && t == 12 && fabsf(g.rope.pt[0].z - 29.5f) < 0.3f);
        g.ropeShots = Game::ROPE_SHOTS, g.roped = false;
        g.step(Input{}), g.step(fire);
        assert(!g.grapple.on);  // NumShots spent
    }
    const float up = Game::HOOK_SPEED;
    Hook h = Game::grappleFire({0, 0, 0}, 0, 0.3f, {0, 0, 20}, false);  // not swung: the aim
    assert(h.on && fabsf(h.at.y - 0.75f) < 1e-6f && Vector3Distance(h.vel, Vector3Scale(dirOf(0, 0.3f), up)) < 1e-4f);
    h = Game::grappleFire({0, 0, 0}, 0, 0.3f, {0, -5, 0}, true);  // no horizontal motion: straight up
    assert(fabsf(h.vel.y - up) < 1e-4f);
    h = Game::grappleFire({0, 0, 0}, 0, 0.3f, {0, 0, 20}, true);  // forward past 10 m/s: 45 deg forward
    assert(fabsf(h.vel.y - h.vel.z) < 1e-3f && h.vel.z > 0);
    h = Game::grappleFire({0, 0, 0}, 0, 0.3f, {0, 0, -5}, true);  // backward at 5 m/s: 22.5 deg behind the vertical
    assert(fabsf(atan2f(-h.vel.z, h.vel.y) - PI / 8) < 1e-3f);
}

// 0x56fcb0: a worm's rope (mask 0x19) bounces off drums and mines, not crates; a hooked object's (0x3f) off crates too
static void checkRopeColliders() {
    Game g;
    Worm &a = ropeFloor(g);
    Vector3 f = {20, 47, 20};
    g.objects = {{Object::Crate, {20, 47, 20.8f}, {0, 0, 0}, -1, -1, false, false}};
    assert(!g.ropeBlocked(f, g.current));
    g.objects.push_back({Object::Crate, {20, 47, 21.5f}, {0, 0, 0}, -1, -1, false, false});
    assert(g.ropeBlocked(g.objects[0].pos - Vector3{0, 0.5f, 0}, -1, 0));  // the crate on the rope meets the other one
    g.objects = {{Object::Barrel, {20, 47.45f + 0.6f, 20}, {0, 0, 0}, -1, -1, false, false}};
    assert(g.ropeBlocked(f, g.current));  // drum: 9 units at its Position, 0.6 m off
    g.objects = {{Object::Mine, {20, 47, 20.35f}, {0, 0, 0}, -1, -1, false, false}};
    assert(g.ropeBlocked(f, g.current) && !g.ropeBlocked({20, 47, 19.55f}, g.current));  // 3 + 5 units
    (void)a;
}

// W4M Inflatable Scouser: swallows a worm, floats it up, pops and drops it.
static void checkScouser() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    Worm &v = g.worms[1 - g.current];
    int wi = weaponNamed("Inflatable Scouser"), hp = v.hp;
    float y = v.pos.y;
    g.phase = Phase::Flying, g.timer = 1200;
    g.shots = {{Vector3Add(v.pos, {0.1f, 0, 0}), {WEAPONS[wi].speed, 0, 0}, wi, WEAPONS[wi].fuse, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.shots[0].stage == 1 && g.shots[0].prey == 1 - g.current);
    for (int t = 0; t < 120; t++) g.step(Input{});
    assert(v.pos.y > y + 1);
    while (!g.shots.empty()) g.step(Input{});
    assert(v.hp <= hp - (int)WEAPONS[wi].damage);
}

// W4M WXP_GasCloud: the canister leaves an 8 s cloud that poisons every worm within 5 m.
static void checkGasCloud() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    Worm &v = g.worms[1 - g.current];
    int wi = weaponNamed("Gas Canister");
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{Vector3Add(v.pos, {0, 0.5f, 0}), {0, 0, 0}, wi, Game::DT, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.gas.size() == 1 && v.poison > 0);
    v.poison = 0, v.pos = Vector3Add(g.gas[0].pos, {4, 0, 0});
    g.step(Input{});
    assert(v.poison == (int)WEAPONS[wi].poison);
    for (int t = 0; t < 8 * 60; t++) g.step(Input{});
    assert(g.gas.empty());
}

// W4M Bovine Blitz (IsControlledBomber, NumStrikeBombs 3): a steered plane, FIRE drops a cow, 0.8 s apart.
static void checkBomber() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.weapon = weaponNamed("Super Airstrike");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input fire, turn;
    fire.buttons = Input::FIRE, turn.turn = 127;
    g.step(fire);
    Vector3 at = g.shots[0].pos;
    float fuse = g.shots[0].fuse;
    for (int t = 1; t < Game::STRIKE_LEAD; t++) g.step(turn);
    assert(Vector3Distance(g.shots[0].pos, at) < 1e-6f && g.shots[0].fuse == fuse);  // held while bombrun_start plays
    assert(g.shots.size() == 1 && !g.shots[0].child);
    Vector3 v = g.shots[0].vel;
    g.step(turn);
    assert(Vector3Distance(g.shots[0].vel, v) > 0);  // banks
    int cows = 0;
    for (int t = 0; t < 4 * 60; t++) {
        g.step(t % 2 ? Input{} : fire);
        if (t == 2) assert(g.shots.size() == 2);  // one cow, the next waits 0.8 s
        cows = 0;
        for (const Projectile &s : g.shots) cows += s.child;
    }
    for (const Projectile &s : g.shots) assert(s.child);  // three dropped: the plane has gone
    assert(cows <= WEAPONS[g.weapon].clusters);
}

// W4M Bomber: the plane drops its NumBombs one after another (BlitzDuration / NumBombs apart), never all at once.
static void checkAirstrike() {
    Game g;
    g.start({31, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    a.yaw = atan2f(40 - a.pos.x, 40 - a.pos.z);  // the start yaw is random: aim inland
    g.objects.clear();  // no barrel or crate chain blasts
    g.weapon = weaponNamed("Airstrike");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input fire;
    fire.buttons = Input::FIRE;
    Vector3 tgt = g.target(), sum{};
    g.step(fire);
    Vector3 at = g.shots[0].pos;
    for (int t = 1; t < Game::STRIKE_LEAD; t++) g.step(Input{});
    assert(g.shots.size() == 1 && g.shots[0].prey == 0 && Vector3Distance(g.shots[0].pos, at) < 1e-6f);  // W4M: held while bombrun_start plays
    g.step(Input{});
    assert(g.shots.size() == 2 && !g.shots[0].child && g.shots[0].prey == 1);  // the first bomb, 4 s on
    assert(fabsf(Vector2Length({g.shots[1].vel.x, g.shots[1].vel.z}) - Game::BOMBER_SPEED) < 1e-3f);  // with the plane's speed
    int booms = 0, last = -100;
    for (int t = 1; t < 10 * 60 && !g.shots.empty(); t++) {
        g.step(Input{});
        if (!g.shots.empty() && !g.shots[0].child) assert(g.shots[0].prey == 1 + t / Game::strikeTicks(WEAPONS[g.weapon]));
        int now = 0;
        for (const GameEvent &e : g.events) now += e.kind == GameEvent::Boom;
        assert(now <= 1);  // one blast, one sound per tick
        if (now) assert(t - last >= Game::strikeTicks(WEAPONS[g.weapon]) / 2), last = t, booms++;
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom) sum = Vector3Add(sum, e.pos);
    }
    assert(booms >= 2 && g.shots.empty());
    sum = Vector3Scale(sum, 1.0f / booms);
    assert(Vector2Distance({sum.x, sum.z}, {tgt.x, tgt.z}) < 3);  // released early: the run still lands on the target
}

// W4M Blimp targeting: TARGET inputs yaw and move the camera focus (not the worm); the strike lands at the view's centre.
static void checkTargetCursor() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.weapon = weaponNamed("Airstrike");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;  // W4M WeaponDelays: no strike in the first turns
    Vector3 p = a.pos;
    float yaw = a.yaw;
    Input in;
    in.buttons = Input::TARGET;
    g.step(in);
    Vector3 f0 = g.cursor, t0 = g.target();
    assert(g.cursorOn && f0.y > p.y && Vector2Distance({t0.x, t0.z}, {p.x, p.z}) < 3);  // entry: the worm under the reticle
    in.walk = 127, in.aim = -127, in.turn = 127;
    for (int t = 0; t < 30; t++) g.step(in);
    assert(a.yaw == yaw && fabsf(a.pos.x - p.x) < 0.01f && fabsf(a.pos.z - p.z) < 0.01f && g.cursor.y == f0.y);
    assert(fabsf(g.cursorYaw - yaw - Game::BLIMP_TURN / 2) < 1e-3f && Vector3Distance(g.cursor, f0) > 15);
    Vector3 t = g.target(), e = g.blimpEye(g.cursor, g.cursorYaw), d = Vector3Normalize(Vector3Subtract(g.cursor, e));
    assert(Vector3Length(Vector3CrossProduct(d, Vector3Normalize(Vector3Subtract(t, e)))) < 1e-3f);  // on the centre ray
    Vector3 f1 = g.cursor;
    in = Input{}, in.buttons = Input::TARGET | Input::PITCH, in.aim = 127;  // tilt up: the focus stays, the pitch drops to 0
    for (int k = 0; k < 200; k++) g.step(in);
    assert(g.cursorPitch == 0 && Vector3Distance(g.cursor, f1) < 1e-4f);
    in.aim = -127;
    for (int k = 0; k < 60; k++) g.step(in);
    assert(fabsf(g.cursorPitch - Game::BLIMP_TILT * 60 / 127 * 127 * Game::DT) < 1e-3f);
    in = Input{}, in.buttons = Input::TARGET, in.walk = 127;  // the focus stays within BLIMP_RANGE of the land centre
    for (int k = 0; k < 60 * 30; k++) g.step(in);
    assert(Vector3Distance(g.cursor, g.landCenter()) <= Game::BLIMP_RANGE + 1e-3f);
    in = Input{}, in.buttons = Input::TARGET | Input::PITCH, in.aim = -127;  // back looking down on the map
    for (int k = 0; k < 200; k++) g.step(in);
    g.cursor = f1, g.cursorPitch = Game::BLIMP_PITCH;
    in = Input{}, in.buttons = Input::TARGET | Input::FIRE;
    g.step(in);
    Vector3 s = g.strikeDir();
    assert(!g.shots.empty() && fabsf(Vector3DotProduct(Vector3Normalize({g.shots[0].vel.x, 0, g.shots[0].vel.z}), s) - 1) < 1e-3f);
}

// Every weapon, at each step of a turn (aim mode, FIRE held, after the shot): only Controls::reticle() decides what is on
// screen. An aim reticle only for aimed() weapons, the Blimp cursor only for targeted ones, nothing once the shot is away.
static void checkReticles() {
    int bad = 0;
    for (size_t wi = 0; wi < WEAPONS.size(); wi++) {
        const WeaponDef &wd = WEAPONS[wi];
        Game g;
        g.start({23, 2, 1, "", (uint32_t)RULE_NO_DELAYS}), g.hotSeat = 0;
        settle(g);
        g.hotSeat = 0;
        g.weapon = (int)wi, g.ammo[g.worms[g.current].team][wi] = 9;
        Controls::reset(), Controls::forceAim = 1;  // ZL held
        Controls::read(g, 0, true, Game::DT);
        Controls::Reticle want = blimped(wd.kind) ? Controls::Reticle::Blimp : Controls::aimed(wd) ? Controls::Reticle::Aim : Controls::Reticle::None;
        Controls::Reticle r = Controls::reticle(g, false);
        if (r != want) printf("reticle: %s in aim mode shows %d, want %d\n", wd.name.c_str(), (int)r, (int)want), bad++;
        Controls::reset(), Controls::forceAim = 0;
        Controls::read(g, 0, true, Game::DT);  // leaves the Blimp
        Input fire;
        fire.buttons = Input::FIRE;
        for (int t = 0; t < 120 && g.phase == Phase::Aim; t++) {  // FIRE held, then released after 0.5 s
            g.step(t < 30 ? fire : Input{});
            Controls::read(g, 0, true, Game::DT);
            r = Controls::reticle(g, g.phase == Phase::Flying && !g.shots.empty());
            bool ok = r == Controls::Reticle::None || (g.phase == Phase::Aim && (r == Controls::Reticle::Aim ? Controls::aimed(wd) : r == Controls::Reticle::Lock));
            if (!ok) printf("reticle: %s tick %d phase %d shows %d\n", wd.name.c_str(), t, (int)g.phase, (int)r), bad++;
        }
    }
    fflush(stdout);
    assert(!bad);
}

// RULE_NO_DELAYS (test): a preset's SchemeData weapon delays are dropped; without it they apply.
static void checkNoDelays() {
    for (const SchemePreset &p : SCHEMES) {
        if (!*p.delays) continue;
        for (uint32_t rules : {0u, (uint32_t)RULE_NO_DELAYS}) {
            Game g;
            GameConfig c{5, 2, 1, "", rules};
            c.scheme = p.s;
            g.start(c), g.hotSeat = 0;
            int n = 0;
            for (const auto &d : g.delays) for (int v : d) n += v > 0;
            assert(rules ? n == 0 : n > 0);
        }
        return;
    }
    assert(!"no preset with weapon delays");
}

// W4M Fatkins is the Bomber's one bomb (0x54ddf0): dropped like an airstrike bomb onto the target, then 0x554e90 bounces it on every
// contact with a blast whose radii shrink x 1, 0.8, 0.6, 0.4, and the 4th contact (or a dead stop) rests it: Detonate, then that blast
static void checkFatkins() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.objects.clear();
    g.weapon = weaponNamed("Fatkins Strike");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input in;
    in.buttons = Input::TARGET;
    g.step(in);
    Vector3 t = g.target(), d = g.strikeDir();
    in.buttons |= Input::FIRE;
    g.step(in);
    assert(g.shots.size() == 1 && fabsf(Vector3DotProduct(g.shots[0].vel, d) - Game::BOMBER_SPEED) < 0.01f && g.shots[0].vel.y == 0);
    assert(fabsf(g.shots[0].pos.y - (g.landTop() + Game::STRIKE_EXTRA)) < 0.01f);  // the bomber's height
    std::vector<GameEvent> booms;
    int bounces = 0;
    for (int k = 0; k < 1800 && !g.shots.empty(); k++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) {
            if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) booms.push_back(e);
            bounces += e.kind == GameEvent::Bounce && e.weapon == g.weapon;
        }
    }
    assert(g.shots.empty() && booms.size() >= 2 && booms.size() <= 5);  // 1 to 4 contact blasts + the Detonate
    assert(Vector2Distance({booms[0].pos.x, booms[0].pos.z}, {t.x, t.z}) < 1.5f);  // first contact on the target
    assert(booms[0].weapon == g.weapon && booms[booms.size() - 2].weapon == -1);  // contact blasts carry the BounceFx; the Detonate is plain
    assert(bounces == (int)booms.size() - 2 || bounces == (int)booms.size() - 1);  // every contact that leaves it moving bounces
}

// W4M Tail Nail: 15 hp, the victim is pinned (no walking, no animals), a blast at its feet frees it.
static void checkTailNail() {
    Worm v = melee("Tail Nail", 1);
    assert(v.nailed && v.hp == 85 && Vector3Length(v.vel) == 0);
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    a.nailed = true;
    Vector3 p = a.pos;
    Input walk;
    walk.walk = 127, walk.buttons = Input::JUMP;
    for (int t = 0; t < 30; t++) g.step(walk);
    assert(Vector3Distance(a.pos, p) < 0.01f);
    g.weapon = weaponNamed("Sheep"), g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    assert(g.phase == Phase::Aim && g.shots.empty());  // no animals when nailed
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{a.pos, {0, 0, 0}, weaponNamed("Bazooka"), 0, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(!a.nailed);
}

// W4M Shield.DamageScale 0.25: an armoured worm takes a quarter of a blast.
static void checkArmour() {
    GameConfig c{23, 2, 1, "", 0};
    c.scheme.mines = c.scheme.barrels = 0;  // nothing else in the blasts
    Game g;
    g.start(c), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    g.objects.push_back(Object{Object::Crate, a.pos, {0, 0, 0}, weaponNamed("Armour"), -1, false, false});  // W4M Armour.Collected
    g.step(Input{});
    assert(a.armour && g.phase == Phase::Aim && g.ammo[a.team][weaponNamed("Armour")] == 0);  // on pickup, the turn goes on
    int ha = a.hp, hv = v.hp;
    v.pos = Vector3Add(a.pos, {20, 0, 0});
    g.phase = Phase::Flying, g.timer = 600;
    int baz = weaponNamed("Bazooka");
    g.shots = {{a.pos, {0, 0, 0}, baz, 0, false, 1}, {v.pos, {0, 0, 0}, baz, 0, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(ha - a.hp == (hv - v.hp) * Game::ARMOUR / 100);
}


static Input press(uint8_t b) { Input in; in.buttons = b; return in; }

// W4M GirderKitLogicEntity: a preview stepped 0.3 m camera-relative, FIRE adds a deck of land, the turn retreats.
static void checkGirder() {
    for (uint32_t wp : {0u, (uint32_t)WP_MULTI_GIRDER}) {
        Game g;
        GameConfig c{23, 2, 1, "", 0};
        c.wormpot = wp;
        g.start(c), g.hotSeat = 0;
        settle(g);
        g.hotSeat = 0;
        Worm &a = g.worms[g.current];
        g.weapon = weaponNamed("Girder"), g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        g.step(Input{});
        assert(g.girderOn && Vector3Distance(g.girder, Vector3Add(a.pos, {sinf(a.yaw) * 2, 0.75f, cosf(a.yaw) * 2})) < 0.01f);
        Vector3 was = g.girder;
        Input fwd = press(Input::TARGET);
        fwd.walk = 127;
        g.step(fwd);
        if (Vector3Distance(g.girder, was) > 0.01f) assert(fabsf(Vector3Distance(g.girder, was) - Game::GIRDER_STEP) < 1e-3f);
        g.girder = Vector3Add(a.pos, {sinf(a.yaw) * 7, 6, cosf(a.yaw) * 7});  // clear sky ahead
        g.step(press(Input::TARGET));
        assert(!g.girderFits(g.girder));
        Vector3 deck = Vector3Add(g.girder, {0, 0.5f, 0});
        assert(!g.terrain.solid(deck));
        g.step(Input{});
        g.step(press(Input::FIRE));
        assert(g.terrain.solid(deck) && g.girders == 1 && g.ammo[a.team][g.weapon] == 0);
        assert(wp ? g.phase == Phase::Aim : g.phase != Phase::Aim);  // GirdersDontEndTurn
        int x = (int)roundf(deck.x / Terrain::VOX), y = (int)roundf(deck.y / Terrain::VOX), z = (int)roundf(deck.z / Terrain::VOX);
        assert(g.terrain.isSteel(((size_t)z * Terrain::NY + y) * Terrain::NX + x));
        g.terrain.carve(deck, 4);  // W4M: ordinary land, blasts dig it (landscape-wide Land.Indestructable only)
        assert(!g.terrain.solid(deck));
    }
    // the replay undo log puts the voxels back, the girder material included
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    std::vector<std::pair<int, signed char>> log;
    Vector3 c = {40, 30, 40};
    g.terrain.undo = &log;
    g.terrain.weld(c, {2, 0.5f, 2});
    assert(g.terrain.solid(c));
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        if (it->first < 0) g.terrain.steel[-1 - it->first] = false;
        else g.terrain.d[it->first] = it->second;
    assert(!g.terrain.solid(c));
    for (size_t i = 0; i < g.terrain.steel.size(); i += 4099) assert(!g.terrain.steel[i]);
}

// W4M Binoculars: FIRE on an enemy solves a bazooka shot (no ammo, the turn goes on); the solution hits.
static void checkBinoculars() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    g.objects.clear(), g.wind = g.windZ = 0;  // nothing in the line of fire; the answer is windless (0x54add0)
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    v.pos = Vector3Add(a.pos, {12, 4, 0}), v.vel = {0, 0, 0};
    Vector3 to = Vector3Subtract(v.pos, a.pos);
    a.yaw = atan2f(to.x, to.z), a.pitch = asinf(to.y / Vector3Length(to));
    int bi = weaponNamed("Binoculars"), ammo = g.ammo[a.team][bi] = 3;
    g.weapon = bi, g.delays[a.team][bi] = 0;
    g.step(press(Input::FIRE));
    assert(g.scout.t == 1 && g.scout.ok && g.ammo[a.team][bi] == ammo && g.phase == Phase::Aim);
    for (int t = 0; t < 10; t++) g.step(Input{});
    assert(g.scout.t == 11);
    // fly the answer: our bazooka at that power and elevation
    float pw = g.scout.pitch, power = g.scout.power;
    Vector3 d = {cosf(pw) * sinf(a.yaw), sinf(pw), cosf(pw) * cosf(a.yaw)};
    int hp = v.hp;
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{launchPoint(WEAPONS[weaponNamed("Bazooka")], a.pos, a.yaw), Vector3Scale(d, WEAPONS[weaponNamed("Bazooka")].speed * power), weaponNamed("Bazooka"), 0, false, 1}};
    for (int t = 0; t < 600 && !g.shots.empty(); t++) v.pos = g.scout.at, v.vel = {}, g.step(Input{});  // held where it was scouted
    assert(v.hp < hp);
}

// W4M Change Worm (WormSelectLogicEntity 0x59a6c0): the first change spends one, more changes are free until a move.
static void checkChangeWorm() {
    Game g;
    g.start({23, 2, 3, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    const int team = g.worms[g.current].team, ci = weaponNamed("Change Worm"), first = g.current;
    g.ammo[team][ci] = 2, g.weapon = ci, g.delays[team][ci] = 0;
    g.step(press(Input::FIRE)), g.step(Input{});
    assert(g.current != first && g.ammo[team][ci] == 1);
    g.step(press(Input::FIRE)), g.step(Input{});
    assert(g.ammo[team][ci] == 1);
    Input walk;
    walk.walk = 60;
    g.step(walk), g.step(Input{}), g.step(press(Input::FIRE));
    assert(g.ammo[team][ci] == 0);
}

// W4M Bubble Trouble: shots from outside bounce off or burst on the shell, a worm inside is untouched; 6 turn ends.
static void checkBubble() {
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.weapon = weaponNamed("Bubble Trouble"), g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    g.step(press(Input::FIRE));
    assert(g.bubbles.empty() && g.delays[a.team][g.weapon] == 1);  // Bubble.LaunchDelay 400 ms; WeaponDelays[42] = 1
    for (int t = 1; t < msTicks(400); t++) g.step(Input{});
    assert(g.bubbles.empty());
    g.step(Input{});
    assert(g.bubbles.size() == 1 && g.bubbles[0].life == 6 && g.phase == Phase::Aim && g.ammo[a.team][g.weapon] == 0);  // the turn goes on
    for (int t = 0; t < 120; t++) g.step(Input{});
    Game::Bubble b = g.bubbles[0];
    Vector3 c = Vector3Add(b.pos, {0, Game::BUBBLE_UP, 0});
    a.pos = Vector3Add(c, {0, -1, 0}), a.vel = {0, 0, 0};
    int hp = a.hp, baz = weaponNamed("Bazooka");
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{Vector3Add(c, {6, 0, 0}), {-30, 0, 0}, baz, 0, false, 1}};
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    for (int t = 0; t < 60 && !g.shots.empty(); t++) g.step(Input{});
    assert(g.shots.empty() && a.hp == hp && g.bubbles.size() == 1);  // burst on the shell
    assert(g.bubbles[0].age > 90 && g.bubbles[0].hit > 90);  // past WXM_Create: HitBounce
    g.shots = {{a.pos, {0, 0, 0}, baz, 0, false, 1}};  // inside: pops it, hurts
    for (Projectile &s : g.shots) s.touching = 0;  // already in flight
    g.step(Input{});
    assert(g.bubbles.empty() && a.hp < hp);
    assert(std::any_of(g.events.begin(), g.events.end(), [](const GameEvent &e) { return e.kind == GameEvent::BubblePop; }));
    g.bubbles = {b};
    for (int k = 0; k < 6; k++) { assert(g.bubbles.size() == 1); endSettle(g); }
    assert(g.bubbles.empty());
}

// W4M Icarus Potion: cures, no heal; after a jump JUMP flaps up, but only in the 250 ms window of each 500 ms beat.
static void checkIcarus() {
    Game g;
    GameConfig c{23, 2, 1, "", 0};
    c.scheme = SCHEMES[0].s;  // W4M Standard: Redbull Delay 2
    g.start(c), g.hotSeat = 0;
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    a.hp = 40, a.poison = 10;
    g.weapon = weaponNamed("Icarus Potion"), g.ammo[a.team][g.weapon] = 1;
    assert(g.delays[a.team][g.weapon] == 2);  // W4M Standard Redbull Delay 2: locked for the team's first turns
    g.step(press(Input::FIRE));
    assert(g.icarus == 0 && a.hp == 40);
    g.step(Input{});
    g.delays[a.team][g.weapon] = 0;
    g.step(press(Input::FIRE));
    Vector3 p = a.pos;
    Input walk;
    walk.walk = 127;
    for (int t = 0; t < msTicks(500) / 2 - 1; t++) g.step(walk), g.step(Input::pick(weaponNamed("Bazooka")));  // W4M PostLaunchDelay 500: drinking
    assert(g.icarus == 3 && a.hp == 40 && Vector3Distance(p, a.pos) < 0.01f && WEAPONS[g.weapon].kind == Kind::Icarus);
    for (int t = 0; t < msTicks(500) && g.icarus == 3; t++) g.step(Input{});
    assert(a.hp == 40 && a.poison == 0 && g.icarus == 1 && g.phase == Phase::Aim);  // InitialEnergy is never set: no heal (0x587866)
    g.step(press(Input::JUMP));
    for (int t = 0; t < 20 && g.icarus != 2; t++) g.step(Input{});
    assert(g.icarus == 2);
    auto flapped = [&] { for (const GameEvent &e : g.events) if (e.kind == GameEvent::Jump) return true; return false; };
    g.step(press(Input::JUMP));  // too early: no flap, the wait starts over
    assert(!flapped() && g.flapAt == g.clock - 1 + Game::FLAP_WAIT);
    int open = g.flapAt;
    while (g.clock < open) g.step(Input{});
    g.step(press(Input::JUMP));
    assert(flapped() && fabsf(a.vel.y - Game::FLAP) < 0.3f);
    g.weapon = weaponNamed("Bazooka");
    g.step(Input{});
    assert(g.icarus == 0);
}

// W4M crate collect 0x5c9800: Double Damage doubles this turn's blasts at once; Crate Spy marks the team for good.
static void checkCollectedUtilities() {
    auto blastHp = [](bool dd) {
        Game g;
        g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
        settle(g);
        g.hotSeat = 0;
        Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
        if (dd) {
            Object o{Object::Crate, a.pos, {0, 0, 0}, weaponNamed("Double Damage"), -1, false, false};
            g.objects.push_back(o);
            g.step(Input{});
            assert(g.doubleDamage && g.ammo[a.team][weaponNamed("Double Damage")] == 0);
        }
        v.pos = Vector3Add(a.pos, {30, 0, 0});
        g.phase = Phase::Flying, g.timer = 600;
        g.shots = {{Vector3Add(v.pos, {0.7f, 0, 0}), {0, 0, 0}, weaponNamed("Bazooka"), 0, false, 1}};
        for (Projectile &s : g.shots) s.touching = 0;  // already in flight
        int hp = v.hp;
        g.step(Input{});
        int lost = hp - v.hp;
        endSettle(g);
        assert(!g.doubleDamage);  // turn over
        return lost;
    };
    int one = blastHp(false), two = blastHp(true);
    assert(one > 0 && two >= 2 * one - 1);
    Game g;
    g.start({23, 2, 1, "", 0}), g.hotSeat = 0;
    settle(g);
    Worm &a = g.worms[g.current];
    g.objects.push_back(Object{Object::Crate, a.pos, {0, 0, 0}, weaponNamed("Crate Spy"), -1, false, false});
    g.step(Input{});
    assert(g.spy[a.team] == 1 && g.ammo[a.team][weaponNamed("Crate Spy")] == 0);
    int team = a.team;
    endSettle(g), endSettle(g);
    assert(g.spy[team] == 1);
    Game d;  // W4M 0x4f4df0: the ending team's Inventory%d.WeaponDelays count down; custom schemes have none
    GameConfig dc{23, 2, 1, "", 0};
    dc.scheme = SCHEMES[0].s;
    d.start(dc), d.hotSeat = 0;
    int t0 = d.worms[d.current].team, bi = weaponNamed("Binoculars"), ab = weaponNamed("Banana Bomb");
    assert(d.delays[t0][bi] == 2 && d.delays[t0][ab] == 8 && d.delays[t0][weaponNamed("Bazooka")] == 0);
    endSettle(d);
    assert(d.delays[t0][bi] == 1 && d.delays[1 - t0][bi] == 2);
    dc.scheme.turnTime++;
    d.start(dc), d.hotSeat = 0;
    assert(d.delays[t0][ab] == 0);
}

// Fast bodies against thin land (one voxel, 0.25 m): sub-stepped moves never skip it; landings always count.
static void checkTunnelling() {
    auto arena = [](auto sdf) {
        Game g;
        g.start({33, 2, 1, "", 0}), g.hotSeat = 0;
        for (int z = 16; z < 80; z++)
            for (int y = 100; y < 250; y++)
                for (int x = 16; x < 200; x++)
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(sdf(Vector3Scale({(float)x, (float)y, (float)z}, Terrain::VOX)) * Terrain::Q, -64, 64);
        g.objects.clear(), g.hotSeat = 0;
        for (size_t i = 0; i < g.worms.size(); i++) g.worms[i].pos = {60, 60, 60 + 3.0f * i}, g.worms[i].vel = {};
        return g;
    };
    auto wall = [](Vector3 p) { return fmaxf(fminf(p.x - 13.875f, 14.125f - p.x), fmaxf(30 - p.y, p.y - 58)); };  // x 13.875..14.125
    auto slab = [](Vector3 p) { return fmaxf(fminf(p.y - 44.875f, 45.125f - p.y), fmaxf(fabsf(p.x - 12) - 5, fabsf(p.z - 12) - 5)); };
    for (float v : {20.0f, 45.0f}) {
        Game g = arena(wall);
        Worm &w = g.worms[g.current];
        w.pos = {10, 52, 12}, w.vel = {v, 0, 0}, w.grounded = false;
        g.objects = {{Object::Barrel, {10, 52, 14}, {v, 0, 0}, -1, -1, false, false}};
        g.shots = {{{10, 52, 16}, {v, 0, 0}, weaponNamed("Grenade"), 3, false, 1}, {{10, 52, 18}, {v, 0, 0}, weaponNamed("Bazooka"), 0, false, 1}};
        for (Projectile &s : g.shots) s.touching = 0;  // already in flight
        float far = 0;
        for (int t = 0; t < 60; t++) {
            g.step(Input{});
            far = fmaxf(far, w.pos.x);
            for (const Object &o : g.objects) far = fmaxf(far, o.pos.x + 0.5f);  // the barrel's leading side
            for (const Projectile &s : g.shots) far = fmaxf(far, s.pos.x);
        }
        assert(far < 13.9f);

        Game f = arena(slab);
        Worm &d = f.worms[f.current];
        d.pos = {12, 52, 12}, d.vel = {0, -v, 0}, d.grounded = false;
        f.objects = {{Object::Crate, {14, 52, 12}, {0, -v, 0}, -1, -1, false, false}};
        for (int t = 0; t < 60; t++) f.step(Input{});
        assert(d.pos.y > 45 && f.objects.size() == 1 && f.objects[0].pos.y > 45);
    }

    int lost = -1;  // a 20 m drop hurts the same wherever the last tick ends
    for (int i = 0; i < 8; i++) {
        Game g = arena([](Vector3 p) { return 30 - p.y; });
        Worm &w = g.worms[g.current];
        w.pos = {12, 50 + i * 0.04f, 12}, w.grounded = false;
        int hp = w.hp;
        for (int t = 0; t < 200; t++) g.step(Input{});
        assert(w.grounded && hp - w.hp >= 14 && (lost < 0 || abs(hp - w.hp - lost) <= 1));  // W4M FallDamage: 400 units → 15 hp
        lost = hp - w.hp;
    }

    Game c = arena([](Vector3 p) { return fmaxf(50 - p.y, fminf(p.x - 14, 20 - p.x)); });  // floor, block from x 14
    c.objects = {{Object::Crate, {12, 51, 12}, {20, 0, 0}, -1, -1, false, false}};
    for (int t = 0; t < 60; t++) c.step(Input{});
    Vector3 cp = c.objects[0].pos;
    assert(cp.x > 13 && !c.terrain.solid({cp.x + 0.45f, cp.y, cp.z}));  // shoved against the face, its side stays out

    Game s = arena([](Vector3 p) {  // 60 degree slope under a thin overhang from x 12.5
        float floor = fmaxf(50 - p.y, (50 + (p.x - 12) * tanf(60 * DEG2RAD) - p.y) * cosf(60 * DEG2RAD));
        return fmaxf(floor, fminf(fminf(p.y - 51.375f, 51.625f - p.y), p.x - 12.5f));
    });
    int sheep = weaponNamed("Sheep");
    s.phase = Phase::Flying;
    s.shots = {{{10, 50.35f, 12}, {WEAPONS[sheep].speed, 0, 0}, sheep, 30, false, 1}};
    float top = 0;
    for (int t = 0; t < 300 && !s.shots.empty(); t++) s.step(Input{}), top = s.shots.empty() ? top : fmaxf(top, s.shots[0].pos.y);
    assert(top < 51.4f);  // never climbs up through it

    Game k = arena([](Vector3 p) { return 50 - p.y; });
    k.objects = {{Object::Crate, {12, 50.5f, 12}, {0, 0, 0}, -1, -1, false, false}, {Object::Mine, {16, 50.15f, 12}, {0, 0, 0}, -1, -1, false, false, -1, -1, true}};
    for (int t = 0; t < 10; t++) k.step(Input{});
    k.terrain.carve({12, 50, 12}, 1.5f), k.terrain.carve({16, 50, 12}, 1.5f);
    for (int t = 0; t < 120; t++) k.step(Input{});
    assert(k.objects.size() == 2 && k.objects[0].pos.y < 49.5f && k.objects[1].pos.y < 49);  // dug out from under them: they drop in

    // past W4M SlideAngle 60 an object slides downhill, below it stays put; same result on a rerun
    auto slope = [&](float deg) {
        float tn = tanf(deg * DEG2RAD);
        Game o = arena([=](Vector3 p) { return fmaxf(30 - p.y, (40 + (p.x - 12) * tn - p.y) * cosf(deg * DEG2RAD)); });
        o.objects = {{Object::Barrel, {16, 40 + 4 * tn + 1, 12}, {0, 0, 0}, -1, -1, false, false}};
        for (int t = 0; t < 120; t++) o.step(Input{});
        return o.objects.empty() ? Vector3{} : o.objects[0].pos;
    };
    Vector3 steep = slope(70), flat = slope(30);
    assert(steep.x < 15.6f && Vector3Equals(steep, slope(70)));  // downhill, about 0.6 m in 2 s
    assert(fabsf(flat.x - 16) < 0.2f);
}

static Vector3 feetOf(const Worm &w) { return {w.pos.x, w.pos.y - Game::R, w.pos.z}; }

// W4M NinjaRopeUtilityLogicEntity 0x574480: an angle pendulum in the yaw's plane (0x5729e0), reel 0x5713c0, bounce and wrap
// 0x573060, unwrap 0x571600, release 0x573530; in the open air (all voxels cleared)
static void checkRope() {
    auto open = [](Game &g) {
        g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
        std::fill(g.terrain.d.begin(), g.terrain.d.end(), (signed char)-127);
        g.objects.clear(), g.ropeMax = 22.5f, g.weapon = weaponNamed("Ninja Rope"), g.timer = 100000;
        Worm &w = g.worms[g.current];
        for (Worm &o : g.worms) if (&o != &w) o.pos = {5, 40, 5};
        w.pos = {40, 50.5f, 40}, w.vel = {}, w.yaw = 0, w.grounded = false;
        return &w;
    };
    const float K = Game::DT / 0.02f;
    {  // hooked ahead and up: the eye's angle (> 0, behind the facing), length from the feet, no spin from rest
        Game g;
        Worm &w = *open(g);
        Vector3 hook = {40, 55, 45};
        g.ropeOn(hook);
        float th = acosf(4.25f / sqrtf(4.25f * 4.25f + 25));
        assert(g.roped && g.rope.n == 1 && fabsf(g.rope.len[0] - sqrtf(50)) < 1e-4f && fabsf(g.rope.angle - th) < 1e-4f && g.rope.spin == 0);
        Input turn;
        turn.turn = 127;
        g.step(turn);  // 0x5729e0 keeps the yaw; gravity's first pull: -0.1 sin / L units per 20 ms
        float l = sqrtf(50) * 20, spin = 400 * -0.00025f * sinf(th) / l * K;
        assert(w.yaw == 0 && fabsf(g.rope.spin - spin) < 1e-7f);
        assert(fabsf(Vector3Distance(feetOf(w), hook) - g.rope.len[0]) < 1e-3f && feetOf(w).z < hook.z);
        float lo = 0;
        Game low;  // at the first pass under the hook
        for (int t = 0; t < 300; t++) {
            g.step(Input{});
            lo = fminf(lo, g.rope.angle);
            if (!low.worms.size() && fabsf(feetOf(w).z - hook.z) < 0.2f) low = g;
            assert(fabsf(Vector3Distance(feetOf(w), hook) - g.rope.len[0]) < 1e-3f);  // on the circle, in the plane x = 40
            assert(fabsf(feetOf(w).x - 40) < 1e-4f);
        }
        assert(low.worms.size() && lo < 0 && -lo < th);  // swung past the bottom, damped by RotationDamping 0.99 per 20 ms
        Worm &u = low.worms[low.current];
        Vector3 at = feetOf(u), v = low.ropeRelease(low.rope, at, 0);
        Input jump;
        jump.buttons = Input::JUMP;
        low.step(jump);  // 0x573530: one more swing step's motion over its time, then a plain fall
        assert(!low.roped && fabsf(u.vel.z - v.z) < 1e-3f && fabsf(u.vel.y - (v.y - 12.5f * Game::DT)) < 0.05f && v.z > 3);
        assert(fabsf(Vector3DotProduct(v, Vector3Subtract(at, hook))) < 0.05f * Vector3Length(v) * low.rope.len[0]);
    }
    {  // the stick swings it along the facing: forward drives the angle negative, the body in front
        Game g;
        Worm &w = *open(g);
        g.ropeOn({40, 55.5f, 40});
        Input fwd;
        fwd.walk = 127;
        assert(Game::ropeSwing(fwd, 0) == 1 && Game::ropeSwing(Input{}, 0) == 0);
        fwd.buttons = Input::HEADING, fwd.turn = 64;  // stick at 90 deg to the facing: 0x571820 gives 0 between 81 and 99 deg
        assert(Game::ropeSwing(fwd, 0) == 0);
        fwd.turn = -128;
        assert(Game::ropeSwing(fwd, 0) == -1);
        fwd.buttons = 0, fwd.turn = 0;
        g.step(fwd);
        float lc = Vector3Distance(feetOf(w), g.rope.pt[0]) * 20;
        assert(g.rope.spin < 0 && g.rope.spin > -6e-5f * 20 * 5 * 10 / lc * K * 1.01f);  // SwingAmount x 20 x 5 MinLength / (L + 0.001 L²)
        float lo = 0, z = 0;
        for (int t = 0; t < 90; t++) g.step(fwd), lo = fminf(lo, g.rope.angle), z = fmaxf(z, feetOf(w).z);
        assert(lo < -0.2f && z > 41);
    }
    {  // reel: 10 m/s; MaxLength over the whole rope; never under MinLength or MinBendDistFromWorm 10 units
        Game g;
        Worm &w = *open(g);
        Vector3 hook = {40, 55, 40};
        g.ropeOn(hook);
        Input in;
        in.aim = 127;
        float l0 = g.rope.len[0];
        g.step(in);
        assert(fabsf(g.rope.len[0] - (l0 - 10 * Game::DT)) < 1e-4f && fabsf(Vector3Distance(feetOf(w), hook) - g.rope.len[0]) < 1e-3f);
        for (int t = 0; t < 120; t++) g.step(in);
        assert(g.rope.len[0] >= 0.5f && g.rope.len[0] < 0.5f + 10 * Game::DT);
        in.aim = -127;
        for (int t = 0; t < 240; t++) g.step(in);
        assert(fabsf(g.rope.len[0] - 22.5f) < 1e-3f);
    }
    {  // a wall's corner in the way: a bend 1 unit (+ half a voxel) off it, spin x old / new length; swung back, it unwraps
        Game g;
        Worm &w = *open(g);
        Vector3 hook = {40, 56, 40}, peg = {40, 54.5f, 44};
        for (int x = 140; x < 180; x++) g.terrain.d[((size_t)175 * Terrain::NY + 217) * Terrain::NX + x] = 127;  // a thin bar along x at z 44, y 54.4
        w.pos = {40, 56.5f, 49}, w.yaw = PI;  // the hook ahead: hung behind the facing, the rope level, clear above the bar
        g.ropeOn(hook);
        int most = 1;
        bool back = false;
        for (int t = 0; t < 1500 && !back && g.roped; t++) {
            Input pump;  // once wrapped, the stick pushes along the motion until it swings back over the bar
            pump.walk = most == 2 ? (g.rope.spin > 0 ? -127 : 127) : 0;
            g.step(pump);
            most = std::max(most, g.rope.n);
            if (g.rope.n == 2) {
                assert(fabsf(g.rope.pt[1].z - peg.z) < 0.4f && fabsf(g.rope.pt[1].y - peg.y) < 0.4f);
                assert(fabsf(Vector3Distance(feetOf(w), g.rope.pt[1]) - g.rope.len[1]) < 1e-3f);
                assert(fabsf(g.rope.len[0] + g.rope.len[1] - 9.03f) < 0.3f);
            }
            back = most == 2 && g.rope.n == 1;
        }
        assert(most == 2 && back && fabsf(g.rope.len[0] - Vector3Distance(feetOf(w), hook)) < 1e-3f);
    }
    {  // the body meets a worm's collider: 0x573060 turns the swing back at 0.9
        Game g;
        Worm &w = *open(g);
        w.pos = {40, 52.5f, 44}, w.yaw = PI;
        g.ropeOn({40, 56, 40});
        Worm &o = g.worms[1 - g.current];
        int bounced = 0;
        for (int t = 0; t < 300 && !bounced; t++) {
            float s = g.rope.spin;
            o.pos = {40, 50.95f, 37.6f}, o.vel = {}, o.grounded = true;
            g.step(Input{});
            if (s < 0 && g.rope.spin > 0 && feetOf(w).z > 38) bounced = t;
        }
        assert(bounced && fabsf(g.rope.spin) > 0);
    }
}

// W4M payload water (0x582050 / 0x580830 / 0x580aa0): skim, splash, disarm and sink at Payload.SinkSpeed, gone at Water.ExpiryDepth
static void checkWaterShots() {
    auto sea = [](Game &g) {
        g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
        std::fill(g.terrain.d.begin(), g.terrain.d.end(), (signed char)-127);
        g.objects.clear(), g.phase = Phase::Flying, g.timer = 100000, g.wind = g.windZ = 0;
        for (Worm &o : g.worms) o.pos = {5, 40, 5}, o.grounded = true;
    };
    {  // Bazooka: 20 m/s at -0.2 rad, over MinSpeedForSkim 0.1 units/ms and MaxAngleForSkim -0.4: SkimDamping (0.5, -0.5, 0.5)
        Game g;
        sea(g);
        int wi = weaponNamed("Bazooka");
        Vector3 v = {0, -20 * sinf(0.2f), 20 * cosf(0.2f)};
        g.shots = {{{40, g.water + 0.3f, 40}, v, wi, 0, false, 1}};
        bool splash = false;
        for (int t = 0; t < 10 && !splash; t++) {
            Vector3 was = g.shots[0].vel;
            g.step(Input{});
            for (const GameEvent &e : g.events) splash = splash || e.kind == GameEvent::Splash;
            if (splash) {
                const Projectile &s = g.shots[0];
                was.y -= 12.5f * 0.6f * Game::DT;  // that tick's gravity (IsLowGravity: Gravity.Slow)
                assert(!s.sunk && s.vel.y > 0 && fabsf(s.vel.y - was.y * -0.5f) < 1e-3f && fabsf(s.vel.z - was.z * 0.5f) < 1e-3f);
                assert(s.pos.y == g.water + 0.25f);
            }
        }
        assert(splash);
    }
    {  // Grenade dropped straight in: a splash at the surface, disarmed 6 units under it, sinks straight, its fuse never goes off
        Game g;
        sea(g);
        int wi = weaponNamed("Grenade");
        g.shots = {{{40, g.water + 1, 40}, {0, -8, 0}, wi, 1, false, 1}};
        bool splash = false, boom = false;
        int t = 0;
        for (; t < 30 && !g.shots[0].sunk; t++) {
            g.step(Input{});
            for (const GameEvent &e : g.events) splash = splash || e.kind == GameEvent::Splash;
        }
        const Projectile &s = g.shots[0];
        assert(splash && s.sunk && s.pos.y == g.water - 0.3f && s.vel.x == 0 && s.vel.z == 0 && s.vel.y <= -4 && s.vel.y >= -5);
        for (t = 0; t < 600 && !g.shots.empty(); t++) {
            g.step(Input{});
            for (const GameEvent &e : g.events) boom = boom || e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom;
            if (!g.shots.empty()) assert(g.shots[0].pos.y > Terrain::WATER - 10);
        }
        assert(g.shots.empty() && !boom && t > (int)((10 - 0.3f) / 5 / Game::DT) - 2);  // gone at Water.ExpiryDepth -200 units
    }
    {  // a Homing Missile homing: +0x6e clear, it only splashes and flies on under the water
        Game g;
        sea(g);
        int wi = weaponNamed("Homing Missile");
        g.shots = {{{40, g.water + 0.5f, 40}, {0, -10, 0}, wi, Game::HOMING_LOCK + 0.1f, false, 1, {40, g.water - 20, 40}}};
        for (int t = 0; t < 30; t++) g.step(Input{});
        assert(!g.shots.empty() && !g.shots[0].sunk && g.shots[0].pos.y < g.water - 1);
    }
}

// W4M 0x4736c7: weapons/Debris only when the blast changed the land
static void checkDebris() {
    Game g;
    g.start({29, 2, 1, "", 0});
    std::fill(g.terrain.d.begin(), g.terrain.d.end(), (signed char)-127);
    for (int z = 150; z < 170; z++)
        for (int y = 190; y < 200; y++)
            for (int x = 150; x < 170; x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = 127;
    auto debris = [&](Vector3 p) {
        g.events.clear(), g.explode(p, Game::MINE_BLAST);
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Debris) return true;
        return false;
    };
    assert(!debris({40, 80, 40}));
    assert(debris({40, 50, 40}));
}

// Poison Arrow stuck in land whose voxel goes (Land.NewShape, 0x5777f0): it falls again; its detonation keeps the first time
static void checkArrowFalls() {
    Game g;
    g.start({29, 2, 1, "", 0}), g.hotSeat = 0;
    std::fill(g.terrain.d.begin(), g.terrain.d.end(), (signed char)-127);
    for (int z = 150; z < 170; z++)
        for (int y = 190; y < 200; y++)
            for (int x = 150; x < 170; x++) g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = 127;
    for (Worm &o : g.worms) o.pos = {5, 40, 5};
    g.objects.clear(), g.phase = Phase::Flying, g.timer = 100000, g.wind = g.windZ = 0;
    int wi = weaponNamed("Poison Arrow");
    g.shots = {{{40, 51, 40}, {0, -10, 0}, wi, 0, false, 1}};
    g.shots[0].touching = 0;
    for (int t = 0; t < 30 && g.shots[0].stage != 1; t++) g.step(Input{});
    assert(g.shots[0].stage == 1);
    for (int t = 0; t < 30; t++) g.step(Input{});
    g.terrain.carve({40, 49.5f, 40}, 1.2f);
    g.step(Input{});
    assert(g.shots.size() == 1 && g.shots[0].stage == 2);
    int left = msTicks(2000) - 32;
    for (int t = 0; t < left - 2; t++) g.step(Input{});
    assert(g.shots.size() == 1 && g.shots[0].stage >= 1 && g.gas.empty());
    for (int t = 0; t < 4; t++) g.step(Input{});
    assert(g.shots.empty() && g.gas.size() == 1);
}

// W4M 0x5b4180 / 0x4f26b0: worms are placed first, then a mine or drum its sphere (radius + 5 units) clear of every worm collider (10 units) and object
static void checkPlacement() {
    const char *maps[] = {"", "Alien-w3d", "Accuracy", "ArabiaTest", "AssaultAndDefend", "BuildingSiteSaboteurs"};
    int checked = 0, overhang = 0;
    for (const char *m : maps)
        for (uint32_t seed = 1; seed <= 30; seed++) {
            Game g;
            GameConfig c{seed, 3, 3, m, 0};
            c.scheme.mines = 15, c.scheme.barrels = 10;
            g.start(c);
            const Grid gr = makeGrid(g.terrain);
            NodeCache nc;
            for (size_t i = 0; i < g.worms.size(); i++) {
                const Worm &w = g.worms[i];
                const float cx = Terrain::NX * Terrain::VOX / 2, cz = Terrain::NZ * Terrain::VOX / 2;
                bool fallback = fabsf(w.pos.y - (g.landTop() + 0.5f + Game::R)) < 1e-3f && fabsf(w.pos.x - cx) <= 2.5f && fabsf(w.pos.z - cz) <= 2.5f;
                for (size_t j = 0; j < i; j++)  // 0x5b4180: sphere 10 units; the no-point fallback (Land.MaxHeight + 10 units) is unchecked there too
                    assert(fallback || Vector3Distance(w.pos - Vector3{0, Game::R, 0}, g.worms[j].pos - Vector3{0, Game::R - 0.25f, 0}) >= 1.0f - 0.01f);  // its feet vs their colliders
                if (fallback) continue;
                const float x = w.pos.x + sinf(w.yaw) * 0.25f, z = w.pos.z + cosf(w.yaw) * 0.25f;  // less the ZOffset -5 units
                const int ci = gr.ci(x), cj = gr.cj(z);
                assert(fabsf(gr.at(ci, cj).x - x) < 1e-3f && fabsf(gr.at(ci, cj).y - z) < 1e-3f);
                int on = -1;
                for (int l = 0; l < 2; l++) {
                    const NodeH h = nodeH(g.terrain, g.water, gr, nc, ci, cj, l);
                    if (h.flag == 0 && fabsf((h.lo + h.hi) / 2 + 1.0f + Game::R - w.pos.y) < 1e-3f) on = l;
                }
                assert(on >= 0);  // a walkable cell (0x4ae810), +20 units
                if (on == 1) overhang++;
            }
            for (const Object &o : g.objects) {
                float r = o.type == Object::Mine ? 0.15f : 0.45f;
                if (o.type != Object::Mine && o.type != Object::Barrel) continue;
                for (const Worm &w : g.worms) {
                    Vector3 cw = {w.pos.x, w.pos.y - Game::R + 0.25f, w.pos.z};
                    assert(Vector3Distance(cw, o.pos) >= 0.5f + r + 0.25f - 0.1f);
                    checked++;
                }
            }
        }
    assert(checked > 0 && overhang > 0);  // layer 1: ground under an overhang
}

int main() {
    checkPlacement();
    checkCountCamera();
    {  // user-requested: SKIP_COUNT ends the damage display at once; hp final
        Game g;
        g.start({21, 2, 2, "", 0}), g.hotSeat = 0;
        g.phase = Phase::Settle, g.timer = 1, g.worms[1].counted = g.worms[1].hp + 10, g.countGroup = {1}, g.countT = 0;
        Input in;
        in.flags = Input::SKIP_COUNT;
        g.step(in);
        assert(g.countGroup.empty() && g.worms[1].counted == std::max(0, g.worms[1].hp));
    }
    {  // 0x5ab7e0: damage types 2..4 cap at 75 per worm per ApplyDamage (doubled with DoubleDamage), type 0 uncapped
        Game g;
        g.start({21, 2, 2, "", 0});
        Worm &w = g.worms[1];
        w.hp = 500;
        g.hurt(w, 60, false, 2), g.hurt(w, 60, false, 2), g.hurt(w, 60, false, 3), g.hurt(w, 200, false, 0);
        assert(w.hp == 500 - 75 - 60 - 200);
        g.applyDamage();
        g.hurt(w, 60, false, 2);
        assert(w.hp == 500 - 75 - 60 - 200 - 60);
    }
    assert(loadWeapons("romfs/weapons.json"));
    checkRope();
    checkWaterShots();
    checkArrowFalls();
    std::vector<bool> used(WEAPONS.size()), again(WEAPONS.size());
    assert(run(used) == run(again));
    for (size_t i = 0; i < WEAPONS.size(); i++) printf("%-18s %s\n", WEAPONS[i].name.c_str(), used[i] ? "used" : "-");

    uint32_t combos[] = {0, RULE_KING, RULE_HIGHLANDER, RULE_VAMPIRE, RULE_KARMA, RULE_LOW_GRAVITY,
                          RULE_ROPE_RACE, RULE_SUDDEN_DEATH, RULE_KARMA | RULE_VAMPIRE};
    for (uint32_t r : combos) assert(runRules(r, 42) == runRules(r, 42));
    for (const SchemePreset &p : SCHEMES) {
        Scheme sd = p.s;
        sd.roundTime = 1;  // reach sudden death within the run
        assert(runRules(RULE_SUDDEN_DEATH, 43, sd) == runRules(RULE_SUDDEN_DEATH, 43, sd));
    }
    assert(runRules(0, 42, SCHEMES[0].s) != runRules(0, 42, SCHEMES[2].s));  // the scheme is part of the checksum

    checkKing();
    checkDeathQueue();
    checkSheepCamera();
    checkEventCameras();
    checkDrownFloat();
    checkDrownPair();
    checkPostActivity();
    checkDeathBlast();
    checkKarma();
    checkVampire();
    checkLowGravity();
    checkDebris();
    checkSuddenDeath();
    checkRopeRace();
    checkHighlander();
    checkObjects();
    checkPoison();
    checkMelee();
    checkSniper();
    checkGunLand();
    checkScopeCrest();
    checkCrateWalk();
    checkShotgun();
    checkHoming();
    checkTeamWeapon();
    checkSentry();
    checkDiffuse();
    checkJumps();
    checkJumpTrajectory();
    checkPayloadForces();
    checkLaunchAtWall();
    checkDroppedInFront();
    checkPointBlankDown();
    checkWallClearance();
    checkWalkW4M();
    checkLowLedges();
    checkVault();
    checkNarrowSlot();
    checkW4MWalkRules();
    checkHeading();
    checkWallStuck();
    checkJumpAtWall();
    checkSelfHurtEndsTurn();
    checkDynamite();
    checkOffMapShot();
    checkCrateHold();
    checkCrateBetweenTurns();
    checkHotSeat();
    checkSkipGo();
    checkWipeEndsMatch();
    checkScheme();
    checkWormpot();
    checkWormpotModes();
    checkMystery();
    checkCustomWeapons();
    checkFuse();
    checkParachute();
    checkRetreatInFlight();
    checkPayloadFollow();
    checkToolWeapons();
    checkJetpack();
    checkJetpackSecondary();
    checkJetpackDrown();
    checkToolGaps();
    checkEmptyHand();
    checkSchemeFactory();
    checkGunObjects();
    checkAbduction();
    checkSuperSheep();
    checkOldWoman();
    checkDonkey();
    checkMineDuds();
    checkEndlessGun();
    checkFactoryBlast();
    checkMineBlast();
    checkMineFlyby();
    checkMineTypes();
    checkRopeShots();
    checkGrapple();
    checkRopeColliders();
    checkScouser();
    checkGasCloud();
    checkBomber();
    checkAirstrike();
    checkTargetCursor();
    checkFatkins();
    checkNoDelays();
    checkReticles();
    checkTailNail();
    checkArmour();
    checkGirder();
    checkBinoculars();
    checkChangeWorm();
    checkBubble();
    checkIcarus();
    checkCollectedUtilities();
    checkTunnelling();
    for (size_t i = 0; i < WEAPONS.size(); i++) {
        bool fired, again;
        Kind k = WEAPONS[i].kind;
        if (collected(k) || k == Kind::Girder || k == Kind::Binoculars) continue;  // crate-only, or their own checks above
        uint32_t a = fireEach((int)i, fired), b = fireEach((int)i, again);
        printf("%-18s %s\n", WEAPONS[i].name.c_str(), fired ? "fired" : "NOT FIRED");
        assert(fired && a == b);
    }

    puts("sim_check OK");
}
