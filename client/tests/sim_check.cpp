// Determinism check: two games fed the same seed and inputs must stay bit-identical.
// Each turn selects the next weapon in the table and uses it, so the whole arsenal gets exercised.
#include "../src/sim.h"
#include "raymath.h"
#include <cassert>
#include <cstdio>

static uint32_t run(std::vector<bool> &used) {
    Game g;
    g.start({42, 3, 2, "", 0});
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
static uint32_t runRules(uint32_t rules, uint32_t seed, const Scheme &scheme = Scheme{}) {
    Game g;
    GameConfig c{seed, 2, 2, "", rules};
    c.scheme = scheme;
    g.start(c);
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

static void checkKing() {
    Game g;
    g.start({1, 2, 2, "", RULE_KING});
    g.worms[0].hp = 0;  // worm 0 of team 0 is the king
    g.phase = Phase::Settle;
    g.timer = 1;
    g.step(Input{});
    assert(!g.worms[0].alive && !g.worms[1].alive);  // whole team out
    assert(g.worms[2].alive && g.worms[3].alive);    // other team untouched
}

// Injects a cluster explosion directly into shots[] rather than going through use(), to isolate the damage rule.
static void checkKarma() {
    Game g;
    g.start({5, 2, 1, "", RULE_KARMA});
    int attacker = g.current, victim = 1 - attacker;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, clusterWeapon(), 0, true, 1}};
    g.step(Input{});
    assert(g.worms[attacker].hp < 100);
    assert(g.worms[attacker].hp > g.worms[victim].hp);  // attacker's cut is smaller than the victim's hit
}

static void checkVampire() {
    Game g;
    g.start({6, 2, 1, "", RULE_VAMPIRE});
    int attacker = g.current, victim = 1 - attacker;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, clusterWeapon(), 0, true, 1}};
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
    g.start({11, 2, 2, "", RULE_SUDDEN_DEATH});
    g.clock = g.cfg.scheme.roundTime * 3600;  // round time is up: the next turn flips sudden death
    float before = g.water;
    g.phase = Phase::Settle;
    g.timer = 1;
    g.step(Input{});
    assert(g.suddenDeath);
    for (const Worm &w : g.worms) if (w.alive) assert(w.hp == 1);
    assert(g.water > before);
}

static void checkRopeRace() {
    Game g;
    g.start({7, 2, 1, "", RULE_ROPE_RACE});
    int team = g.worms[g.current].team;
    g.worms[g.current].pos = g.raceFinish;
    g.step(Input{});
    assert(g.phase == Phase::GameOver && g.winner == team);
}

static void checkHighlander() {
    Game g;
    g.start({13, 2, 1, "", RULE_HIGHLANDER});
    int wi = clusterWeapon();
    g.ammo[0][wi] = 0;
    g.ammo[1][wi] = 2;
    g.shots = {{g.worms[1].pos, {0, 0, 0}, wi, 0, true, 1}};
    g.step(Input{});  // explode() tags lastHitTeam[1] with the attacker's (team 0) team
    g.worms[1].hp = 0;
    g.phase = Phase::Settle;
    g.timer = 1;
    g.step(Input{});
    assert(!g.worms[1].alive);
    assert(g.ammo[0][wi] == 1);  // killer's team inherits the victim's weapon
}

static void checkObjects() {
    Game g;
    g.start({17, 2, 1, "", 0});
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
    for (int t = 0; t < 200 && !g.objects.empty(); t++) g.step(Input{});
    assert(g.objects.empty() && g.worms[victim].hp < hp);
}

static int weaponNamed(const char *n) {
    for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].name == n) return (int)i;
    assert(!"weapon missing from weapons.json");
    return 0;
}

// One scripted turn per weapon (charge, release, steer, detonate): its Fire event must show up.
static uint32_t fireEach(int wi, bool &fired) {
    Game g;
    g.start({99u + wi, 2, 2, "", 0});
    g.ammo[g.worms[g.current].team][wi] = 1;
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

static void checkPoison() {
    Game g;
    g.start({21, 2, 1, "", 0});
    int victim = 1 - g.current, hp = g.worms[victim].hp;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, weaponNamed("Poison Arrow"), 0, false, 1}};
    g.step(Input{});
    assert(g.worms[victim].poison > 0 && g.worms[victim].hp < hp);
    hp = g.worms[victim].hp;
    g.phase = Phase::Settle;
    g.timer = 1;
    g.step(Input{});
    assert(g.worms[victim].hp == hp - g.worms[victim].poison);  // ticks at the next turn start
}

static void checkMelee() {
    Game g;
    g.start({23, 2, 1, "", 0});
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    v.pos = Vector3Add(a.pos, {sinf(a.yaw), 0, cosf(a.yaw)});
    v.vel = {0, 0, 0};
    a.pitch = 0.3f;
    g.weapon = weaponNamed("Baseball Bat");
    Input in;
    in.buttons = Input::FIRE;
    g.step(in);
    assert(v.hp < 100 && Vector3Length(v.vel) > 10);  // knocked away along the aim
}

static void checkSniper() {
    Game g;
    g.start({25, 2, 1, "", 0});
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    a.pos = {10, 55, 10}, v.pos = {10, 55, 30};  // open sky, 20 m apart
    a.yaw = 0, a.pitch = 0;
    g.weapon = weaponNamed("Sniper Rifle");
    Input in;
    in.buttons = Input::FIRE;
    g.step(in);
    assert(v.hp <= 100 - (int)WEAPONS[g.weapon].damage + 1);
}

static void checkSentry() {
    Game g;
    g.start({27, 2, 1, "", 0});
    Worm &w = g.worms[g.current];
    w.pos = {10, 55, 10};
    Object sentry = {Object::Sentry, {10, 55, 14}, {0, 0, 0}, weaponNamed("Sentry Gun"), -1, false, false, 1 - w.team};
    g.objects = {sentry};
    g.step(Input{});
    assert(w.hp < 100 && g.objects[0].fuse > 0);  // shot the enemy in range, now reloading
}

static void settle(Game &g) {
    for (int t = 0; t < 120; t++) g.step(Input{});  // land; empty input keeps the hot seat running
}

static Vector3 facing(const Worm &w) { return {sinf(w.yaw), 0, cosf(w.yaw)}; }

static void checkJumps() {
    Game g;
    g.start({31, 2, 1, "", 0});
    settle(g);
    Worm &w = g.worms[g.current];
    assert(w.grounded && g.phase == Phase::Aim);
    Input jump;
    jump.buttons = Input::JUMP;
    g.step(jump);
    assert(g.jumpDelay > 0 && w.grounded);  // waits for a possible second press
    for (int t = 0; t < Game::JUMP_WINDOW && w.grounded; t++) g.step(Input{});
    assert(!w.grounded && Vector3DotProduct(w.vel, facing(w)) > 2 && w.vel.y > 6 && w.vel.y < 8);  // forward jump

    Game f;
    f.start({31, 2, 1, "", 0});
    settle(f);
    Worm &b = f.worms[f.current];
    f.step(jump);
    f.step(Input{});
    f.step(jump);  // second press inside the window
    assert(!b.grounded && Vector3DotProduct(b.vel, facing(b)) < -0.5f && b.vel.y > 9);  // high backflip
}

// W4M: the turn ends as soon as the active worm takes damage, without retreat time.
static void checkSelfHurtEndsTurn() {
    Game g;
    g.start({33, 2, 1, "", 0});
    settle(g);
    g.shots = {{g.worms[g.current].pos, {0, 0, 0}, clusterWeapon(), 0, true, 1}};
    g.step(Input{});
    assert(g.worms[g.current].hp < 100 && g.phase == Phase::Settle);
}

static void checkHotSeat() {
    Game g;
    g.start({35, 2, 1, "", 0});
    int timer = g.timer;
    assert(g.hotSeat == g.cfg.scheme.hotSeat * 60);
    g.step(Input{});
    assert(g.timer == timer && g.clock == 0);  // turn clock frozen
    Input in;
    in.turn = 50;
    g.step(in);
    g.step(Input{});
    assert(!g.hotSeat && g.timer < timer && g.clock > 0);  // any input starts the turn
}

static void checkScheme() {
    GameConfig c{37, 2, 2, "", 0};
    c.scheme.health = 150;
    c.scheme.mines = c.scheme.barrels = 0;
    c.scheme.crateChance = 0;
    c.scheme.weapons = Scheme::SET_BNG;
    c.scheme.turnTime = 20;
    Game g;
    g.start(c);
    for (const Worm &w : g.worms) assert(w.hp == 150);
    assert(g.objects.empty() && g.timer == 20 * 60);
    for (size_t i = 0; i < WEAPONS.size(); i++) {
        Kind k = WEAPONS[i].kind;
        if (k == Kind::Rope || k == Kind::Sheep) assert(g.ammo[0][i] == 0);
        if (WEAPONS[i].name == "Bazooka") assert(g.ammo[0][i] == -1);
    }
    c.scheme.fallDamage = 0;
    g.start(c);
    Worm &w = g.worms[0];
    w.pos.y += 15;  // long drop
    w.vel = {0, 0, 0};
    w.grounded = false;
    for (int t = 0; t < 200; t++) g.step(Input{});
    assert(w.hp == 150 || !w.alive);
}

int main() {
    assert(loadWeapons("romfs/weapons.json"));
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
    checkKarma();
    checkVampire();
    checkLowGravity();
    checkSuddenDeath();
    checkRopeRace();
    checkHighlander();
    checkObjects();
    checkPoison();
    checkMelee();
    checkSniper();
    checkSentry();
    checkJumps();
    checkSelfHurtEndsTurn();
    checkHotSeat();
    checkScheme();
    for (size_t i = 0; i < WEAPONS.size(); i++) {
        bool fired, again;
        uint32_t a = fireEach((int)i, fired), b = fireEach((int)i, again);
        printf("%-18s %s\n", WEAPONS[i].name.c_str(), fired ? "fired" : "NOT FIRED");
        assert(fired && a == b);
    }

    puts("sim_check OK");
}
