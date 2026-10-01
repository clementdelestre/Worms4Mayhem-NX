// Determinism check: two games fed the same seed and inputs must stay bit-identical.
// Each turn selects the next weapon in the table and uses it, so the whole arsenal gets exercised.
#include "../src/sim.h"
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
static uint32_t runRules(uint32_t rules, uint32_t seed) {
    Game g;
    g.start({seed, 2, 2, "", rules});
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
    g.turnCount = Game::SD_TURNS * g.teams - 1;  // one more turn flips sudden death
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

int main() {
    assert(loadWeapons("romfs/weapons.json"));
    std::vector<bool> used(WEAPONS.size()), again(WEAPONS.size());
    assert(run(used) == run(again));
    for (size_t i = 0; i < WEAPONS.size(); i++) printf("%-18s %s\n", WEAPONS[i].name.c_str(), used[i] ? "used" : "-");

    uint32_t combos[] = {0, RULE_KING, RULE_HIGHLANDER, RULE_VAMPIRE, RULE_KARMA, RULE_LOW_GRAVITY,
                          RULE_ROPE_RACE, RULE_SUDDEN_DEATH, RULE_KARMA | RULE_VAMPIRE};
    for (uint32_t r : combos) assert(runRules(r, 42) == runRules(r, 42));

    checkKing();
    checkKarma();
    checkVampire();
    checkLowGravity();
    checkSuddenDeath();
    checkRopeRace();
    checkHighlander();

    puts("sim_check OK");
}
