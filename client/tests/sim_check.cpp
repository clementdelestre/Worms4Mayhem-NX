// Determinism check: two games fed the same seed and inputs must stay bit-identical.
// Each turn selects the next weapon in the table and uses it, so the whole arsenal gets exercised.
#include "../src/sim.h"
#include "../src/controls.h"
#include "raymath.h"
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
static uint32_t runRules(uint32_t rules, uint32_t seed, const Scheme &scheme = Scheme{}, uint32_t wormpot = 0) {
    Game g;
    GameConfig c{seed, 2, 2, "", rules};
    c.scheme = scheme;
    c.wormpot = wormpot;
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

// Settle now, through its hp count and deaths, up to the next turn
static void endSettle(Game &g) {
    g.phase = Phase::Settle, g.timer = 1;
    for (int t = 0; t < 60 * 30 && g.phase == Phase::Settle; t++) g.step(Input{});
}

static void checkKing() {
    Game g;
    g.start({1, 2, 2, "", RULE_KING});
    g.worms[0].hp = 0;  // worm 0 of team 0 is the king
    std::vector<int> died;
    g.phase = Phase::Settle, g.timer = 1;
    for (int t = 0; t < 60 * 30 && g.phase == Phase::Settle; t++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death) died.push_back(e.worm);
    }
    assert(died == std::vector<int>({0, 1}));        // the king, then his team through the death queue
    assert(g.worms[2].alive && g.worms[3].alive);    // other team untouched
}

// W4M death queue: dead worms of one count blow up one after another.
static void checkDeathQueue() {
    Game g;
    g.start({1, 3, 1, "", 0});
    for (int i = 0; i < 2; i++) g.worms[i].hp = 0, g.worms[i].pos = Vector3Add(g.worms[2].pos, {2.0f * i + 2, 0, 0});
    std::vector<int> at;
    g.phase = Phase::Settle, g.timer = 1;
    for (int t = 0; t < 60 * 30 && g.phase == Phase::Settle; t++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Death) at.push_back(t);
    }
    assert(at.size() == 2 && at[1] - at[0] == Game::COUNT_DEATH + Game::COUNT_THROES);
}

// W4M drowning: no hp count, the worm floats a moment with the camera on it, then pops at the surface.
static void checkDrownFloat() {
    Game g;
    g.start({1, 2, 1, "", 0});
    Worm &d = g.worms[0];
    d.pos = {d.pos.x, g.water - 0.5f, d.pos.z}, d.vel = {0, 0, 0}, d.grounded = false;
    for (int k = 0; k < 400 && g.terrain.solid({d.pos.x, d.pos.y - 1.5f, d.pos.z}); k++) d.pos.x += 0.1f;  // open sea
    g.step(Input{});
    assert(!d.alive && d.counted > 0 && g.countTicks(0) == Game::COUNT_FLOAT);
    int died = -1, focus = 0;
    float boomY = 0;
    g.phase = Phase::Settle, g.timer = 1;
    for (int t = 0; t < 60 * 30 && g.phase == Phase::Settle; t++) {
        g.step(Input{});
        focus += g.dying() == 0;
        for (const GameEvent &e : g.events) {
            if (e.kind == GameEvent::Death && e.worm == 0) died = g.countT;
            if (e.kind == GameEvent::Boom && died < 0) boomY = e.pos.y;
        }
    }
    assert(died == Game::COUNT_TRAVEL + Game::COUNT_FLOAT && focus >= Game::COUNT_FLOAT - 1);
    assert(boomY == g.water && d.counted == 0);  // popped at the surface, then nothing left to draw
}

// W4M PostActivityTime: 2400 ms between the end of the settle and the next turn.
static void checkPostActivity() {
    Game g;
    g.start({1, 2, 1, "", 0});
    g.phase = Phase::Settle, g.timer = 1;
    int t = 0;
    for (; t < 600 && g.phase == Phase::Settle; t++) g.step(Input{});
    assert(t == 1 + Game::POST_ACTIVITY && g.phase == Phase::Aim && g.hotSeat == 10 * 60);
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
    endSettle(g);
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
    g.step(Input{});
    assert(g.events.empty());  // GameOver (jingle, victory voice) is emitted once, not every tick
}

static void checkHighlander() {
    Game g;
    g.start({13, 2, 1, "", RULE_HIGHLANDER});
    int wi = clusterWeapon();
    g.ammo[0][wi] = 0, g.delays[0][wi] = 0;
    g.ammo[1][wi] = 2, g.delays[1][wi] = 0;
    g.shots = {{g.worms[1].pos, {0, 0, 0}, wi, 0, true, 1}};
    g.step(Input{});  // explode() tags lastHitTeam[1] with the attacker's (team 0) team
    g.worms[1].hp = 0;
    endSettle(g);
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
    g.start(cfg ? *cfg : GameConfig{99u + wi, 2, 2, "", 0});
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

static void checkPoison() {
    Game g;
    g.start({21, 2, 1, "", 0});
    int victim = 1 - g.current, hp = g.worms[victim].hp;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, weaponNamed("Poison Arrow"), 0, false, 1}};
    g.step(Input{});
    assert(g.worms[victim].poison > 0 && g.worms[victim].hp < hp);
    g.phase = Phase::Settle, g.timer = 300;
    while (g.countGroup.empty()) g.step(Input{});  // knocked back: landed, its fall counted too
    hp = g.worms[victim].hp;
    while (g.phase == Phase::Settle) g.step(Input{});
    assert(g.worms[victim].hp == hp - g.worms[victim].poison);  // ticks at the next turn start
}

static void settle(Game &g);

// Victim placed `ahead` m in front of the settled attacker, `up` m higher; returns the victim after the swing.
static Worm melee(const char *weapon, float ahead, float up = 0) {
    Game g;
    g.start({23, 2, 1, "", 0});
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
static void checkShotgun() {
    Game g;
    g.start({25, 2, 1, "", 0});
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    a.pos = {10, 55, 10}, v.pos = {10.4f, 55, 20};  // open sky, off-centre hit
    a.yaw = 0, a.pitch = 0;
    g.weapon = weaponNamed("Shotgun");
    Input in;
    in.buttons = Input::FIRE;
    g.step(in);
    assert(v.hp == 100 - (int)WEAPONS[g.weapon].damage);
}

// Homing missile: flies off along the aim, then dives onto the reticle point.
static void checkHoming() {
    Game g;
    g.start({29, 2, 1, "", 0});
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    g.hotSeat = 0;
    a.pos = {20, 55, 20}, a.yaw = 0, a.pitch = 0;  // open sky: the reticle falls on the ground 30 m ahead
    g.weapon = weaponNamed("Homing Missile");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input in;
    in.buttons = Input::FIRE;
    for (int t = 0; t < 89; t++) g.step(in), a.pos = {20, 55, 20}, a.vel = {0, 0, 0};
    v.pos = Vector3Add(g.target(), {0, Game::R + 0.05f, 0}), v.vel = {0, 0, 0};
    g.step(in);  // full charge fires
    assert(g.phase == Phase::Flying);
    while (g.phase == Phase::Flying) g.step(Input{});
    assert(v.hp <= 100 - 40 || !v.alive);
}

// W4M: each team gets back the weapon it last had in hand, or the next one with ammo.
static void checkTeamWeapon() {
    Game g;
    g.start({39, 2, 1, "", 0});
    int t0 = g.worms[g.current].team, bat = weaponNamed("Baseball Bat"), sheep = weaponNamed("Sheep");
    auto endTurn = [&] { endSettle(g); };
    g.weapon = sheep;
    endTurn();
    assert(g.worms[g.current].team != t0 && g.weapon != sheep);
    g.weapon = bat;
    endTurn();
    assert(g.worms[g.current].team == t0 && g.weapon == sheep);
    g.ammo[t0][bat] = 0, g.delays[t0][bat] = 0;
    g.ammo[1 - t0][bat] = 0, g.delays[1 - t0][bat] = 0;
    endTurn();
    assert(g.weapon != bat && g.ammo[1 - t0][g.weapon]);  // out of ammo: next available
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
        g.start(c);
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

// Sniper at a worm just over a crest, aimed like a player: target moved to the scope camera's screen centre.
static void checkScopeCrest() {
    Game g;
    g.start({25, 2, 1, "", 0});
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
    g.start({27, 2, 1, "", 0});
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
    g.start({23, 2, 1, "", 0});
    settle(g);
    g.weapon = weaponNamed("Sheep");
    Controls::reset();
    Camera3D cam = {{0, 30, 0}, g.worms[g.current].pos, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    int checked = 0;
    for (int t = 0; t < 60 * 8 && g.phase == Phase::Flying; t++) {
        g.step(Input{});
        bool chase = !g.shots.empty();
        Controls::camera(cam, g, chase, false, false, Game::DT);
        if (chase && t > 30) assert(cam.position.y > g.shots[0].pos.y), checked++;
    }
    assert(checked > 60);
}

// W4M event cameras (docs/camera.md): worm, crate and winner TrackCams cut next to their target, homing FlyCam stays behind
// the missile, the shoulder camera only zooms in when ~all its occlusion rays are blocked.
static void checkEventCameras() {
    auto away = [] { return Camera3D{{0, 60, 0}, {-10, 60, -10}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE}; };  // looks off the map
    auto inView = [](const Camera3D &c, Vector3 p) {
        Vector3 f = Vector3Normalize(Vector3Subtract(c.target, c.position)), to = Vector3Subtract(p, c.position);
        return Vector3DotProduct(f, to) > 0.8f * Vector3Length(to);
    };
    {  // a worm knocked ballistic off screen: WormTrackCamera cut, then kept in view
        Game g;
        g.start({23, 2, 1, "", 0});
        settle(g);
        Controls::reset();
        g.phase = Phase::Retreat, g.timer = 600;
        Worm &w = g.worms[1 - g.current];
        w.vel = {6, 9, 0}, w.grounded = false;
        Camera3D cam = away();
        int seen = 0, n = 0;
        for (int t = 0; t < 60 * 3 && !w.grounded; t++, n++) {
            Controls::camera(cam, g, false, false, false, Game::DT);
            if (t == 6) assert(Vector3Distance(cam.position, {0, 60, 0}) > 5 && inView(cam, w.pos));  // 2 ViewPoints tried a frame
            seen += inView(cam, w.pos);
            g.step(Input{});
        }
        assert(n > 20 && seen > n * 8 / 10);
    }
    {  // CrateTrackCamera: starts off screen, cut to a crate ViewPoint (15-25 m)
        Game g;
        g.start({43, 2, 1, "", 0});
        settle(g);
        Controls::reset();
        Worm &w = g.worms[g.current];
        g.objects.push_back({Object::Crate, Vector3Add(w.pos, {3, 12, 0}), {0, 0, 0}, -1, -1, true, false});
        Camera3D cam = away();
        for (int t = 0; t < 6; t++) Controls::focus(&g.objects.back().pos, 0, true), Controls::camera(cam, g, false, false, false, Game::DT);
        float d = Vector3Distance(cam.position, g.objects.back().pos);
        assert(d > 10 && d < 26 && inView(cam, g.objects.back().pos));
    }
    {  // game over: the winner from a worm ViewPoint, in clear view
        Game g;
        g.start({23, 2, 1, "", 0});
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
        g.start({29, 2, 1, "", 0});
        Worm &a = g.worms[g.current];
        g.hotSeat = 0;
        a.pos = {20, 55, 20}, a.yaw = 0, a.pitch = 0;
        g.weapon = weaponNamed("Homing Missile");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;  // not about the Standard delays
        Controls::reset();
        Input in;
        in.buttons = Input::FIRE;
        for (int t = 0; t < 90; t++) g.step(in), a.pos = {20, 55, 20}, a.vel = {0, 0, 0};
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
        g.start({25, 2, 1, "", 0});
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
    for (int inside : {0, 1}) {  // NinjaCamMkIII: a hill between never zooms it in; inside land it swings to the first clear yaw offset
        Game g;
        g.start({25, 2, 1, "", 0});
        Worm &w = g.worms[g.current];
        w.pos = {40, 55, 40}, w.yaw = 0, w.grounded = false, w.vel = {0, 0, 0};
        g.roped = true, g.anchor = {40, 60, 40}, g.ropeLen = 5;
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
    for (int sea : {0, 1}) {  // W4M CMS 0x51d408: the death / hp-count framing (5) gives way to a thrown worm landing ashore (5), not to one lost at sea (3)
        Game g;
        g.start({23, 2, 1, "", 0});
        settle(g);
        Controls::reset();
        g.phase = Phase::Retreat, g.timer = 600;
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
        if (sea) assert(n > 20 && Vector3Distance(cam.target, spot) < 2);
        else assert(n > 30 && seen > (n - 20) * 7 / 10);
    }
    {  // AlienAbductionCamera 0x547490: worm + (0, 50, 50) units while the beam lifts it, else the fixed shot above the UFO
        Game g;
        g.start({23, 2, 1, "", 0});
        settle(g);
        Controls::reset();
        g.weapon = weaponNamed("Alien Abduction");
        g.phase = Phase::Retreat, g.timer = 600;
        Worm &w = g.worms[1 - g.current];
        w.pos.y += 3, w.vel = {0, 16, 0}, w.grounded = false;
        Camera3D cam = away();
        int close = 0, far = 0;
        for (int t = 0; t < 60 * 3 && !w.grounded; t++) {
            Controls::camera(cam, g, false, false, false, Game::DT);
            float d = Vector3Distance(cam.position, w.pos);
            close += w.vel.y > 0 && fabsf(d - 3.536f) < 0.05f && cam.position.y > w.pos.y;
            far += w.vel.y < -1 && d > 6;
            g.step(Input{});
        }
        assert(close > 20 && far > 10);
    }
    {  // homing: FIRE in the Blimp takes the target (W4M state 1, no launch), then the launcher is aimed and powered; it homes on that point
        Game g;
        g.start({29, 2, 1, "", 0});
        Worm &a = g.worms[g.current];
        g.hotSeat = 0;
        g.weapon = weaponNamed("Homing Missile");
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        auto hold = [&] { a.pos = {20, 55, 20}, a.vel = {0, 0, 0}; };
        Input in;
        in.buttons = Input::TARGET;
        for (int t = 0; t < 5; t++) hold(), g.step(in);
        Vector3 h;
        assert(g.cursorOn && !g.locked && Controls::targetHeld(g) && g.blimpHit(&h));
        in.buttons = Input::TARGET | Input::FIRE;
        hold(), g.step(in), g.step(in);
        assert(g.locked && Vector3Distance(g.lockAt, h) < 0.01f && g.shots.empty() && g.power == 0 && !Controls::targetHeld(g));
        hold(), g.step(Input{});
        in.buttons = Input::FIRE;
        for (int t = 0; t < 30 && g.shots.empty(); t++) hold(), g.step(in);
        hold(), g.step(Input{});
        assert(g.shots.size() == 1 && Vector3Distance(g.shots[0].aim, g.lockAt) < 1e-3f);
    }
    {  // victory fireworks in the OrbitCam frame: ours (camera target +-10 m, 5-10 m up) and W4M's (Land.Center +-0.5 Radius, MaxHeight + 30 units)
        Game g;
        g.start({23, 2, 1, "", 0});
        settle(g);
        Controls::reset();
        g.phase = Phase::GameOver, g.winner = g.worms[g.current].team;
        Camera3D cam = away();
        float top = g.terrain.colTop.empty() ? 20 : g.terrain.colTop.back() * Terrain::VOX, c = Terrain::NX * Terrain::VOX / 2, R = c;
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
                    for (float k : {0.0f, 1.0f}) {
                        assert(framed(Vector3Add(cam.target, {10 * dx, 5 + 5 * k, 10 * dz})));
                        assert(framed({c + 0.5f * R * dx, top + 1.5f * k, c + 0.5f * R * dz}));
                    }
            checked++;
        }
        assert(checked > 10);
    }
}

static void settle(Game &g) {
    for (int t = 0; t < 120; t++) g.step(Input{});  // land; empty input keeps the hot seat running
}

// W4M Worm.Death*: the dead worm's blast takes up to 35 hp off its neighbours, throws them, digs 1.75 m; they count it after.
static void checkDeathBlast() {
    const Blast &k = Game::DEATH_BLAST;
    assert(Game::blastDamage(k, {}, {0.8f, 0, 0}) == 35 && Game::blastDamage(k, {}, {2.5f, 0, 0}) == 14 && Game::blastDamage(k, {}, {3.6f, 0, 0}) == 0);
    Game g;
    g.start({23, 2, 2, "", 0});
    settle(g);
    Worm &d = g.worms[0], &n = g.worms[2];
    n.pos = Vector3Add(d.pos, {0.8f, 0, 0}), n.vel = {0, 0, 0};
    settle(g);
    Vector3 under = {d.pos.x, d.pos.y - Game::R - 0.4f, d.pos.z}, was = n.pos;
    assert(g.terrain.solid(under) && n.hp == 100);
    d.hp = 0;
    endSettle(g);
    assert(!d.alive && n.alive && n.hp <= 65 && n.counted == n.hp && Vector3Distance(n.pos, was) > 3 && !g.terrain.solid(under));
}

static Vector3 facing(const Worm &w) { return {sinf(w.yaw), 0, cosf(w.yaw)}; }

// Concave corner (floor + two walls): walking into it or dropping against a wall leaves the body out of the rock.
static void checkWallClearance() {
    auto room = [](Game &g) {  // floor y 50, walls x < 10 and z < 10; density = signed distance, clamped like the map's
        for (int z = 24; z < 72; z++)
            for (int y = 180; y < 240; y++)
                for (int x = 24; x < 72; x++) {
                    float in = fmaxf(50 - y * Terrain::VOX, fmaxf(10 - x * Terrain::VOX, 10 - z * Terrain::VOX));
                    g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = (signed char)Clamp(in * Terrain::Q, -127, 127);
                }
    };
    auto clear = [](const Game &g, const Worm &w) {  // deepest point of a 0.25 m ring at body heights
        float worst = -1;
        for (float h : {0.0f, 0.3f})
            for (int k = 0; k < 16; k++)
                worst = fmaxf(worst, g.terrain.sample({w.pos.x + 0.25f * cosf(k * PI / 8), w.pos.y + h, w.pos.z + 0.25f * sinf(k * PI / 8)}));
        return worst <= 0;
    };
    Game g;
    g.start({33, 2, 1, "", 0});
    room(g);
    Worm &w = g.worms[g.current];
    w.pos = {12, 50.5f, 12}, w.vel = {}, w.yaw = -3 * PI / 4;  // facing the corner
    g.hotSeat = 0;
    Input walk;
    walk.walk = 127;
    for (int t = 0; t < 120; t++) g.step(walk);
    assert(w.pos.x < 10.6f && w.pos.z < 10.6f && clear(g, w));  // reached the corner, body outside both walls

    Game f;
    f.start({33, 2, 1, "", 0});
    room(f);
    Worm &d = f.worms[f.current];
    d.pos = {10.05f, 53, 13}, d.vel = {-2, 0, 0};  // falls against the wall
    f.hotSeat = 0;
    for (int t = 0; t < 120; t++) f.step(Input{});
    assert(d.grounded && clear(f, d));
}

// Density clamped like the imported .vox maps (64 / Q): corridors and steps stay walkable, W4M vaults ledges up to the body.
static void checkWalkW4M() {
    auto walk = [](auto sdf, int ticks, float yaw = PI / 2, Vector3 from = {9, 50.5f, 12}) {
        Game g;
        g.start({33, 2, 1, "", 0});
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

// W4M launch 0x5a5d30 + exact Integrate 0x5a6e90, 20 units = 1 m: jump 50 units up, 80 along; backflip 80 up, 50.6 back.
static void checkJumpTrajectory() {
    auto jump = [](bool flip, uint32_t rules, float &h, float &d, int &air) {
        Game g;
        g.start({33, 2, 1, "", rules});
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

// W4M payloads (0x57ea40): wind adds Wind.Speed to the acceleration; the homing missile has no gravity and homes only in stage 2.
static void checkPayloadForces() {
    Game g;
    g.start({29, 2, 1, "", 0});
    g.wind = 1, g.phase = Phase::Flying;
    int baz = weaponNamed("Bazooka"), hom = weaponNamed("Homing Missile");
    g.shots = {{{20, 70, 20}, {0, 0, 0}, baz, 0, false, 1}, {{30, 70, 20}, {0, 0, 10}, hom, 0, false, 1, {30, 70, -100}}};
    for (int t = 0; t < 60; t++) g.step(Input{});
    assert(fabsf(g.shots[0].vel.x - Game::WIND_ACCEL) < 0.01f && fabsf(g.shots[0].vel.y + 12.5f * 0.6f) < 0.01f);  // 1 s: wind, Gravity.Slow
    assert(g.shots[1].vel.y == 0 && g.shots[1].vel.z == 10);  // stage 1: straight on
    for (int t = 0; t < 60; t++) g.step(Input{});
    assert(g.shots[1].vel.z < 0 && Vector3Length(g.shots[1].vel) <= Game::HOMING_MAX + 1e-3f);  // turned to its target
}

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

    // W4M DetectJump: every kind leaves when the 300 ms window ends; the stick at that moment picks the variant
    auto leave = [&](bool twice, bool held, int8_t walk, float &along) {
        Game f;
        f.start({31, 2, 1, "", 0});
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
    float along, up = leave(true, false, 0, along);
    assert(along < -1.5f && up > 9.5f);  // backflip
    up = leave(true, false, 127, along);
    assert(along > 1.5f && along < 1.7f && up > 9.5f);  // forward flip
    up = leave(false, true, 0, along);
    assert(fabsf(along) < 0.01f && up > 9 && up < 9.4f);  // held: vertical jump
    up = leave(false, true, 127, along);
    assert(along > 3 && up < 8);  // held with the stick forward: a normal jump
}

// Dynamite: the worm walks away while the fuse burns, can't fire again, and the blast ends the turn.
static void checkDynamite() {
    Game g;
    g.start({37, 2, 1, "", 0});
    settle(g);
    g.hotSeat = 0;
    int first = g.current, dyn = weaponNamed("Dynamite");
    Worm &w = g.worms[first];
    g.weapon = dyn, g.ammo[w.team][dyn] = 2, g.delays[w.team][dyn] = 0;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    g.step(Input{});  // released: dropped
    assert(g.phase == Phase::Retreat && g.shots.size() == 1 && g.ammo[w.team][dyn] == 1);
    Vector3 start = w.pos;
    int t = 0;
    for (; !g.shots.empty() && t < 60 * 20; t++) {
        Input in;
        in.walk = t < 150 ? -127 : 0;
        in.buttons = t % 2 ? Input::FIRE : 0;
        if (!g.shots.empty()) assert(g.phase == Phase::Retreat && g.shots.size() == 1);
        g.step(in);
    }
    assert(std::abs(t - (int)(WEAPONS[dyn].fuse * 60)) <= 2 && g.ammo[w.team][dyn] == 1);
    assert(Vector3Distance(w.pos, start) > 3 && g.phase == Phase::Settle);
    for (t = 0; t < 60 * 8 && !(g.phase == Phase::Aim && g.current != first); t++) g.step(Input{});
    assert(g.current != first);
}

// A shot leaving the map flies on (Flying, camera on it) until it falls into the sea with a splash.
static void checkOffMapShot() {
    Game g;
    g.start({41, 2, 1, "", 0});
    Worm &a = g.worms[g.current];
    const float W = Terrain::NX * Terrain::VOX;
    a.pos = {W - 2, 40, W / 2}, a.vel = {0, 0, 0}, a.yaw = PI / 2, a.pitch = 0.6f;  // edge, aiming out
    g.hotSeat = 0, g.wind = 0, g.weapon = weaponNamed("Bazooka");
    Input fire;
    fire.buttons = Input::FIRE;
    for (int t = 0; t < 120 && g.phase == Phase::Aim; t++) g.step(t < 45 ? fire : Input{}), a.pos = {W - 2, 40, W / 2}, a.vel = {0, 0, 0};  // half power: Gravity.Slow carries a full one past W + 100
    assert(g.phase == Phase::Flying);
    float far = 0;
    bool splash = false;
    while (g.phase == Phase::Flying) {
        if (!g.shots.empty()) far = fmaxf(far, g.shots[0].pos.x);
        g.step(Input{});
        for (const GameEvent &e : g.events) splash |= e.kind == GameEvent::Splash && e.worm < 0;
    }
    assert(far > W + 20 && splash && g.shots.empty());
}

// W4M: a dropped crate holds the turn (no clock, no control) until it lands.
static void checkCrateHold() {
    Game g;
    g.start({43, 2, 1, "", 0});
    settle(g);
    g.hotSeat = 0;
    Worm &w = g.worms[g.current];
    g.objects.push_back({Object::Crate, Vector3Add(w.pos, {3, 12, 0}), {0, 0, 0}, -1, -1, true, false});
    int timer = g.timer;
    Vector3 p = w.pos;
    Input in;
    in.walk = 127;
    for (int t = 0; t < 60; t++) g.step(in);
    assert(g.dropping() && g.timer == timer && Vector3Distance(p, w.pos) < 0.01f);
    for (int t = 0; t < 60 * 15 && g.dropping(); t++) g.step(Input{});
    assert(!g.dropping());
    g.step(Input{});
    assert(g.timer == timer - 1);
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

// The match ends once a team is wiped out, even with a shotgun shot still pending; both wiped = draw.
static void checkWipeEndsMatch() {
    for (int draw = 0; draw < 2; draw++) {
        Game g;
        g.start({39, 2, 1, "", 0});
        settle(g);
        int me = g.worms[g.current].team;
        g.weapon = weaponNamed("Shotgun"), g.shotsLeft = 1;
        for (Worm &w : g.worms) if (w.team != me || draw) w.hp = 0;
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

static int hitHp(uint32_t wormpot) {  // victim hp after a cluster bomblet lands on it
    Game g;
    GameConfig c{6, 2, 1, "", 0};
    c.wormpot = wormpot;
    g.start(c);
    int victim = 1 - g.current;
    g.shots = {{g.worms[victim].pos, {0, 0, 0}, clusterWeapon(), 0, true, 1}};
    g.step(Input{});
    return g.worms[victim].hp;
}

static float walked(uint32_t wormpot) {
    Game g;
    GameConfig c{31, 2, 1, "", 0};
    c.wormpot = wormpot;
    g.start(c);
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
    c.wormpot = WP_GOLIATH | WP_CRATE_DROPS | WP_VITAL_WORM | WP_LOW_GRAVITY;
    Game g;
    g.start(c);
    assert(g.worms[0].hp == 200 && g.worms[1].hp == 50 && (g.cfg.rules & RULE_KING) && (g.cfg.rules & RULE_LOW_GRAVITY));
    for (size_t i = 0; i < WEAPONS.size(); i++)
        if (WEAPONS[i].kind != Kind::SkipGo && WEAPONS[i].kind != Kind::Surrender) assert(g.ammo[0][i] == 0);
    c.wormpot = WP_ONE_SHOT;
    g.start(c);
    for (const Worm &w : g.worms) assert(w.hp == 1);

    c.wormpot = WP_NO_JUMPING;
    g.start(c);
    settle(g);
    Input jump;
    jump.buttons = Input::JUMP;
    g.step(jump);
    assert(!g.jumpDelay);
    for (int b = 0; b < WORMPOT_REELS[3]; b++) assert(runRules(0, 44, Scheme{}, 1u << b) == runRules(0, 44, Scheme{}, 1u << b));
    assert(runRules(0, 44, Scheme{}, WP_DOUBLE_DAMAGE) != runRules(0, 44, Scheme{}));  // the wormpot is part of the checksum
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
    assert(list.size() == 2 && list[0].name == w.name && list[0].fuse == w.fuse && list[0].bounce == w.bounce && list[0].model == "holy" && list[1].kind == Kind::Airstrike);
    GameConfig c{51, 2, 2, "", 0};
    c.custom = list;
    Game g;
    g.start(c);
    assert(WEAPONS.size() == base + 2 && WEAPONS[base].name == "Test Lobber" && g.ammo[0][base] == 2);
    uint32_t with = g.checksum();
    for (size_t i = base; i < WEAPONS.size(); i++) {
        bool fired, again;
        c.seed = 99u + (int)i;
        uint32_t x = fireEach((int)i, fired, &c), y = fireEach((int)i, again, &c);
        assert(fired && again && x == y);
    }
    c.custom.clear();
    c.seed = 51;
    g.start(c);
    assert(WEAPONS.size() == base && g.checksum() != with);
}

// W4M FuseUp: the grenade family's fuse is set on the d-pad (1..5 s) and is exact to the tick; other weapons ignore it.
static int fuseTicks(int presses, uint8_t key, const char *weapon, int *choir = nullptr) {
    Game g;
    g.start({29, 2, 1, "", 0});
    Worm &a = g.worms[g.current];
    g.hotSeat = 0;
    g.weapon = weaponNamed(weapon);
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    for (int i = 0; i < presses; i++) { Input in; in.buttons = key; g.step(in); g.step(Input{}); }
    a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.yaw = 0, a.pitch = 0.5f;
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
    g.start({29, 2, 1, "", 0});
    g.hotSeat = 0;
    g.weapon = weaponNamed("Bazooka");
    Input in;
    in.buttons = Input::FUSE_UP;
    g.step(in);
    assert(g.fuses[g.worms[g.current].team] == 3);
}

// W4M "Will auto activate if selected": a long fall with the parachute in hand opens it, no fall damage, drifts downwind.
static void checkParachute() {
    Game g;
    g.start({29, 2, 1, "", 0});
    Worm &a = g.worms[g.current];
    g.hotSeat = 0, g.wind = 1;
    int chute = weaponNamed("Parachute");
    g.weapon = chute, g.ammo[a.team][chute] = 1, g.delays[a.team][chute] = 0;
    a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = false;
    float x = a.pos.x;
    for (int t = 0; t < 60 && !g.chute; t++) g.step(Input{});
    assert(g.chute && g.ammo[a.team][chute] == 0);
    for (int t = 0; t < 60 * 30 && !a.grounded; t++) g.step(Input{});
    assert(a.grounded && a.hp == 100 && a.pos.x > x + 1);
}

// Rope and jetpack: a weapon taken in hand fires without leaving the tool, which keeps working through the retreat.
static void checkToolWeapons() {
    for (const char *tool : {"Ninja Rope", "Jetpack"}) {
        Game g;
        g.start({29, 2, 1, "", 0});
        Worm &a = g.worms[g.current];
        g.hotSeat = 0;
        bool rope = tool[0] == 'N';
        if (rope) g.roped = true, g.anchor = {20, 58, 20}, g.ropeLen = 3, g.ropeMax = 25;
        else g.jetting = true, g.fuel = 6, g.thrust = 28;
        a.pos = {20, 55, 20}, a.vel = {0, 0, 0}, a.grounded = false;
        int gun = weaponNamed("Shotgun"), grenade = weaponNamed("Grenade");
        g.weapon = rope ? gun : grenade;
        g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
        Input fire;
        fire.buttons = Input::FIRE;
        g.step(fire);
        if (rope) { g.step(Input{}); g.step(fire); }  // both shotgun shots
        else g.step(Input{});                          // released: thrown
        assert(g.phase != Phase::Aim && (rope ? g.roped : g.jetting) && g.ammo[a.team][g.weapon] == 0);
        while (g.phase == Phase::Flying) g.step(Input{});
        assert(g.phase == Phase::Retreat && (rope ? g.roped : g.jetting));
        if (!rope) {
            float vy = a.vel.y;
            g.step(fire);
            assert(a.vel.y > vy);  // still thrusts
        }
        while (g.phase == Phase::Retreat) g.step(Input{});
        assert(!g.roped && !g.jetting);
    }
}

// W4M Alien Abduction: worms under the UFO lose half their health and are lifted.
static void checkAbduction() {
    Game g;
    g.start({29, 2, 1, "", 0});
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    g.hotSeat = 0;
    a.pos = {20, 55, 20}, a.yaw = 0, a.pitch = 0;
    g.weapon = weaponNamed("Alien Abduction");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    v.pos = Vector3Add(g.target(), {0, Game::R + 0.05f, 0}), v.vel = {0, 0, 0}, v.hp = 90;
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    assert(v.hp == 45 && v.vel.y > 0);
}

// W4M Super Sheep: walks, FIRE takes off (25 s flight), FIRE again blows it up.
static void checkSuperSheep() {
    Game g;
    g.start({23, 2, 1, "", 0});
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
    assert(g.shots[0].stage == 1 && g.shots[0].vel.y > 0 && g.shots[0].fuse > WEAPONS[g.weapon].fuse - 0.1f && g.timer > 25 * 60);
    g.step(Input{});
    g.step(fire);
    assert(g.shots.empty());
}

// W4M Old Woman: steered, FIRE explodes, each enemy she bumps loses 1-8 of a weapon to her team.
static void checkOldWoman() {
    Game g;
    g.start({23, 2, 1, "", 0});
    settle(g);
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    int wi = weaponNamed("Old Woman"), baz = weaponNamed("Bazooka");
    for (int &n : g.ammo[v.team]) n = 0;
    g.ammo[v.team][baz] = 8, g.delays[v.team][baz] = 0, g.ammo[a.team][baz] = 0, g.delays[a.team][baz] = 0;
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{Vector3Add(v.pos, {0.1f, 0, 0}), {WEAPONS[wi].speed, 0, 0}, wi, 5, false, 1}};
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

// W4M Concrete Donkey: smashes on down, every 0.75 s, until its 8 s LifeTime or the water.
static void checkDonkey() {
    Game g;
    g.start({23, 2, 1, "", 0});
    settle(g);
    int wi = weaponNamed("Concrete Donkey"), booms = 0;
    g.phase = Phase::Flying, g.timer = 1200;
    g.shots = {{{40, 40, 40}, {0, -WEAPONS[wi].speed, 0}, wi, 0, false, 1 << 30, {0, 40, 0}}};
    for (int t = 0; t < (int)(Game::DONKEY_LIFE * 60) + 1 && !g.shots.empty(); t++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) booms += e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom;
    }
    assert(g.shots.empty() && booms > 3);
}

// W4M Mine.DudProbability: about one mine in ten fizzles and stays inert.
static void checkMineDuds() {
    Game g;
    g.start({23, 2, 1, "", 0});
    g.objects.clear();
    int duds = 0;
    for (int k = 0; k < 400; k++) {
        g.objects = {{Object::Mine, {5, 60, 5}, {0, 0, 0}, -1, 0.01f, false, false}};
        g.step(Input{});
        duds += !g.objects.empty() && g.objects[0].dud;
    }
    assert(duds > 15 && duds < 70);
    g.objects[0].dud = true, g.objects[0].fuse = -1;
    g.worms[0].pos = g.objects[0].pos;
    g.step(Input{});
    assert(g.objects[0].fuse < 0);  // a dud never re-arms
}

// W4M payload Explosion handler: a blast only pushes a mine, up and away; it does not arm it.
static void checkMineBlast() {
    Game g;
    g.start({23, 2, 1, "", 0});
    settle(g);
    Vector3 at = g.worms[0].pos;
    for (Worm &w : g.worms) w.pos.x += 30;
    g.objects = {{Object::Mine, Vector3Add(at, {1.5f, -0.4f, 0}), {0, 0, 0}, -1, -1, false, false}};
    g.shots = {{{at.x, at.y - Game::R - 0.3f, at.z}, {0, 0, 0}, weaponNamed("Bazooka"), 0, false, 1}};
    g.step(Input{});
    assert(g.shots.empty() && g.objects.size() == 1 && g.objects[0].fuse < 0 && g.objects[0].vel.y > 1 && g.objects[0].vel.x > 0);
}

// W4M ArmingRadius 45 units: a worm blown past a mine 2 m off arms it; a laid mine ignores worms for ArmingCourtesyTime.
static void checkMineFlyby() {
    Game g;
    g.start({23, 2, 1, "", 0});
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

// W4M Ninja.NumShots: 5 launches a turn; the hook catches a crate, reels it in and out, jump lets it go.
static void checkRopeShots() {
    Game g;
    g.start({29, 2, 1, "", 0});
    Worm &a = g.worms[g.current];
    g.hotSeat = 0;
    a.pos = {20, 55, 20}, a.yaw = 0, a.pitch = 0, a.vel = {0, 0, 0};
    g.weapon = weaponNamed("Ninja Rope");
    g.objects = {{Object::Crate, {20, 55, 26}, {0, 0, 0}, -1, -1, false, false}};
    Input fire;
    fire.buttons = Input::FIRE;
    g.step(fire);
    assert(g.ropeShots == 1 && !g.roped && g.objects[0].hooked && fabsf(g.ropeLen - 6) < 0.1f);
    Input reel;
    reel.aim = 127;  // stick up: shorter
    for (int k = 0; k < 20; k++) g.step(reel);  // 2 m in: the crate is still out of reach
    assert(g.ropeLen < 4.1f && g.objects.size() == 1 && Vector3Distance(g.objects[0].pos, a.pos) <= g.ropeLen + 0.2f);
    reel.aim = -127;
    for (int k = 0; k < 10; k++) g.step(reel);
    assert(g.ropeLen > 4.5f && g.objects[0].hooked);
    Input jump;
    jump.buttons = Input::JUMP;
    for (int k = 0; k < 6; k++) { g.step(jump); g.step(Input{}); g.step(fire); }
    assert(g.ropeShots == Game::ROPE_SHOTS);
}

// W4M Inflatable Scouser: swallows a worm, floats it up, pops and drops it.
static void checkScouser() {
    Game g;
    g.start({23, 2, 1, "", 0});
    settle(g);
    Worm &v = g.worms[1 - g.current];
    int wi = weaponNamed("Inflatable Scouser"), hp = v.hp;
    float y = v.pos.y;
    g.phase = Phase::Flying, g.timer = 1200;
    g.shots = {{Vector3Add(v.pos, {0.1f, 0, 0}), {WEAPONS[wi].speed, 0, 0}, wi, WEAPONS[wi].fuse, false, 1}};
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
    g.start({23, 2, 1, "", 0});
    settle(g);
    Worm &v = g.worms[1 - g.current];
    int wi = weaponNamed("Gas Canister");
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{Vector3Add(v.pos, {0, 0.5f, 0}), {0, 0, 0}, wi, Game::DT, false, 1}};
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
    g.start({23, 2, 1, "", 0});
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.weapon = weaponNamed("Super Airstrike");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input fire, turn;
    fire.buttons = Input::FIRE, turn.turn = 127;
    g.step(fire);
    g.step(Input{});
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
    g.start({23, 2, 1, "", 0});
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.objects.clear();  // no barrel or crate chain blasts
    g.weapon = weaponNamed("Airstrike");
    g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    Input fire;
    fire.buttons = Input::FIRE;
    Vector3 tgt = g.target(), sum{};
    g.step(fire);
    assert(g.shots.size() == 2 && !g.shots[0].child && g.shots[0].prey == 1);  // the first bomb leaves at once
    assert(fabsf(Vector2Length({g.shots[1].vel.x, g.shots[1].vel.z}) - Game::BOMBER_SPEED) < 1e-3f);  // with the plane's speed
    int booms = 0, last = -100;
    for (int t = 1; t < 10 * 60 && !g.shots.empty(); t++) {
        g.step(Input{});
        if (!g.shots.empty() && !g.shots[0].child) assert(g.shots[0].prey == 1 + t / Game::STRIKE_TICKS);
        int now = 0;
        for (const GameEvent &e : g.events) now += e.kind == GameEvent::Boom;
        assert(now <= 1);  // one blast, one sound per tick
        if (now) assert(t - last >= Game::STRIKE_TICKS / 2), last = t, booms++;
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom) sum = Vector3Add(sum, e.pos);
    }
    assert(booms >= 2 && g.shots.empty());
    sum = Vector3Scale(sum, 1.0f / booms);
    assert(Vector2Distance({sum.x, sum.z}, {tgt.x, tgt.z}) < 3);  // released early: the run still lands on the target
}

// W4M Blimp targeting: TARGET inputs yaw and move the camera focus (not the worm); the strike lands at the view's centre.
static void checkTargetCursor() {
    Game g;
    g.start({23, 2, 1, "", 0});
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

// W4M Fatkins is a Bomber payload: it leaves the plane flying the strike direction and lands on the target.
static void checkFatkins() {
    Game g;
    g.start({23, 2, 1, "", 0});
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
    assert(g.shots.size() == 1 && Vector3DotProduct(g.shots[0].vel, d) > Game::BOMBER_SPEED - 0.01f);
    Vector3 boom{};
    for (int k = 0; k < 600 && !g.shots.empty() && boom.y == 0; k++) {
        g.step(Input{});
        for (const GameEvent &e : g.events) if ((e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) && boom.y == 0) boom = e.pos;
    }
    assert(Vector2Distance({boom.x, boom.z}, {t.x, t.z}) < 1.5f);  // first smash on the target
}

// W4M Tail Nail: 15 hp, the victim is pinned (no walking, no animals), a blast at its feet frees it.
static void checkTailNail() {
    Worm v = melee("Tail Nail", 1);
    assert(v.nailed && v.hp == 85 && Vector3Length(v.vel) == 0);
    Game g;
    g.start({23, 2, 1, "", 0});
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
    g.step(Input{});
    assert(!a.nailed);
}

// W4M Shield.DamageScale 0.25: an armoured worm takes a quarter of a blast.
static void checkArmour() {
    Game g;
    g.start({23, 2, 1, "", 0});
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
        g.start(c);
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
    g.start({23, 2, 1, "", 0});
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
    g.start({23, 2, 1, "", 0});
    settle(g);
    g.hotSeat = 0;
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
    g.shots = {{Vector3Add(a.pos, Vector3Scale(d, 1.2f)), Vector3Scale(d, WEAPONS[weaponNamed("Bazooka")].speed * power), weaponNamed("Bazooka"), 0, false, 1}};
    for (int t = 0; t < 600 && !g.shots.empty(); t++) g.step(Input{});
    assert(v.hp < hp);
}

// W4M Bubble Trouble: shots from outside bounce off or burst on the shell, a worm inside is untouched; 6 turn ends.
static void checkBubble() {
    Game g;
    g.start({23, 2, 1, "", 0});
    settle(g);
    g.hotSeat = 0;
    Worm &a = g.worms[g.current];
    g.weapon = weaponNamed("Bubble Trouble"), g.ammo[a.team][g.weapon] = 1, g.delays[a.team][g.weapon] = 0;
    g.step(press(Input::FIRE));
    assert(g.bubbles.size() == 1 && g.bubbles[0].life == 6);
    for (int t = 0; t < 120; t++) g.step(Input{});
    Game::Bubble b = g.bubbles[0];
    Vector3 c = Vector3Add(b.pos, {0, Game::BUBBLE_UP, 0});
    a.pos = Vector3Add(c, {0, -1, 0}), a.vel = {0, 0, 0};
    int hp = a.hp, baz = weaponNamed("Bazooka");
    g.phase = Phase::Flying, g.timer = 600;
    g.shots = {{Vector3Add(c, {6, 0, 0}), {-30, 0, 0}, baz, 0, false, 1}};
    for (int t = 0; t < 60 && !g.shots.empty(); t++) g.step(Input{});
    assert(g.shots.empty() && a.hp == hp && g.bubbles.size() == 1);  // burst on the shell
    g.shots = {{a.pos, {0, 0, 0}, baz, 0, false, 1}};  // inside: pops it, hurts
    g.step(Input{});
    assert(g.bubbles.empty() && a.hp < hp);
    g.bubbles = {b};
    for (int k = 0; k < 6; k++) { assert(g.bubbles.size() == 1); endSettle(g); }
    assert(g.bubbles.empty());
}

// W4M Icarus Potion: cures and heals; after a jump JUMP flaps up, but only in the 250 ms window of each 500 ms beat.
static void checkIcarus() {
    Game g;
    GameConfig c{23, 2, 1, "", 0};
    c.scheme = SCHEMES[0].s;  // W4M Standard: Redbull Delay 2
    g.start(c);
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
    assert(a.hp == 100 && a.poison == 0 && g.icarus == 1 && g.phase == Phase::Aim);
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
        g.start({23, 2, 1, "", 0});
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
    g.start({23, 2, 1, "", 0});
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
    d.start(dc);
    int t0 = d.worms[d.current].team, bi = weaponNamed("Binoculars"), ab = weaponNamed("Banana Bomb");
    assert(d.delays[t0][bi] == 2 && d.delays[t0][ab] == 8 && d.delays[t0][weaponNamed("Bazooka")] == 0);
    endSettle(d);
    assert(d.delays[t0][bi] == 1 && d.delays[1 - t0][bi] == 2);
    dc.scheme.turnTime++;
    d.start(dc);
    assert(d.delays[t0][ab] == 0);
}

// Fast bodies against thin land (one voxel, 0.25 m): sub-stepped moves never skip it; landings always count.
static void checkTunnelling() {
    auto arena = [](auto sdf) {
        Game g;
        g.start({33, 2, 1, "", 0});
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
    checkDeathQueue();
    checkSheepCamera();
    checkEventCameras();
    checkDrownFloat();
    checkPostActivity();
    checkDeathBlast();
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
    checkWallClearance();
    checkWalkW4M();
    checkSelfHurtEndsTurn();
    checkDynamite();
    checkOffMapShot();
    checkCrateHold();
    checkHotSeat();
    checkWipeEndsMatch();
    checkScheme();
    checkWormpot();
    checkCustomWeapons();
    checkFuse();
    checkParachute();
    checkToolWeapons();
    checkAbduction();
    checkSuperSheep();
    checkOldWoman();
    checkDonkey();
    checkMineDuds();
    checkMineBlast();
    checkMineFlyby();
    checkRopeShots();
    checkScouser();
    checkGasCloud();
    checkBomber();
    checkAirstrike();
    checkTargetCursor();
    checkFatkins();
    checkTailNail();
    checkArmour();
    checkGirder();
    checkBinoculars();
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
