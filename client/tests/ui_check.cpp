// Weapon panel: its direct pick (Input::pick) must land on the weapon whatever the ticks per frame.
// From client/: make ui_check
#include "../src/audio.h"
#include "../src/fx.h"
#include "../src/controls.h"
#include <cstring>
#include "../src/ui.h"
#include "../src/barracks.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <set>
#include <string_view>
#include <cstdio>

int main() {
    loadWeapons("romfs/weapons.json");
    {  // an overflowing text waits 2 s, then keeps sliding; a looping one slides from the start
        assert(Ui::marqueeShift(600, 1, false) == 0 && Ui::marqueeShift(600, 3, false) > 0 && Ui::marqueeShift(600, 3.5f, false) > Ui::marqueeShift(600, 3, false));
        assert(Ui::marqueeShift(600, 0.5f, true) > 0);
    }
    {  // Lobby slots: a pad puts a saved team in a free slot only, a pad may play several, release frees it, pads in use are this console's human slots (a CPU team needs none)
        Lobby l;
        l.cfg.teams = Lobby::MAX_TEAMS, l.cfg.teamSetup.resize(Lobby::MAX_TEAMS);
        GameConfig::Team h, cpu;
        h.name = "H", cpu.name = "D", cpu.cpu = 3;
        assert(l.count() == 0 && l.place(2, 1, h) && !l.place(2, 0, h) && l.place(0, 1, h) && l.playing(0) && l.playing(2) && !l.playing(1) && l.padsUsed() == 0b10);
        assert(l.ownedBy(2, 1) && !l.ownedBy(2, 0) && !l.ownedBy(1, 1));
        assert(l.place(3, 0, cpu) && l.cfg.teamSetup[3].cpu == 3 && l.padsUsed() == 0b10 && !l.ownedBy(3, 0));
        assert(l.release(3) && !l.playing(3) && !l.release(3));
        l.cfg.teamSetup[0].name = "A", l.cfg.teamSetup[2].name = "C", l.place(3, 0, cpu);
        GameConfig m = l.match();  // playing slots 0, 2, 3 in order
        assert(m.teams == 3 && m.teamSetup.size() == 3 && m.teamSetup[0].name == "A" && m.teamSetup[1].name == "C" && m.teamSetup[2].name == "D");
        {  // a human team on an absent pad moves to the lowest present pad; present pads, CPU and other consoles' teams stay
            Lobby r = l;
            r.cfg.teamSetup[0].pad = 3, r.cfg.teamSetup[2].pad = 1, r.cfg.teamSetup[3].pad = 3;
            assert(r.rehome(0b0011) && r.cfg.teamSetup[0].pad == 0 && r.cfg.teamSetup[2].pad == 1 && r.cfg.teamSetup[3].pad == 3 && !r.rehome(0b0011));
            r.owner[2] = 7, r.cfg.teamSetup[0].pad = 2;
            assert(r.rehome(0b0001) && r.cfg.teamSetup[0].pad == 0 && r.cfg.teamSetup[2].pad == 1);
            assert(!r.rehome(0) && r.cfg.teamSetup[0].pad == 0);
        }
        l.owner[0] = 7;  // another console's slot is read-only
        assert(!l.ownedBy(0, 1) && !l.release(0) && !l.place(0, 1, h) && l.padsUsed() == 0b10);
    }
    {  // Teams screen tokens move on their own and a room's later slots shift up on a release
        TeamCursors c;
        c.move(0, 1), c.move(1, -1), c.slot[2] = 3, c.move(2, 1);  // no wrap at either end
        assert(c.slot[0] == 1 && c.slot[1] == 0 && c.slot[2] == 3);
        c.slot[1] = 2, c.removed(1);
        assert(c.slot[0] == 1 && c.slot[1] == 1);
        // a card is open for one pad at a time; each pad may have its own open
        assert(c.claim(0, 2) && c.claim(1, 3) && !c.claim(1, 2) && c.open[1] == 3 && c.claim(0, 2));
        c.unclaim(0);
        assert(c.open[0] == -1 && c.claim(1, 2));
    }
    {  // a Joy-Con pair's colour: neon blue + neon red = purple (hue halfway on the shorter arc), not the grey RGB average
        Color p = mixColors({10, 185, 230, 255}, {255, 60, 40, 255});
        Vector3 h = ColorToHSV(p);
        assert(h.x > 250 && h.x < 310 && h.y > 0.7f && p.a == 255);
        Color w = mixColors(WHITE, WHITE);
        assert(w.r == 255 && w.g == 255 && w.b == 255);
    }
    {  // W4M lobby timer: 30 s from the second player, restarted by a join, off under 2 players, everyone ready at its end
        ReadyTimer t;
        assert(!t.update(0, 1) && t.seconds(0) == -1);
        assert(!t.update(10, 2) && t.seconds(10) == 30 && t.seconds(35.5) == 5);
        assert(!t.update(20, 3) && t.seconds(20) == 30);  // a join restarts it
        assert(!t.update(49.9, 3) && t.update(50, 3) && !t.update(51, 3) && t.seconds(51) == -1);  // fires once
        assert(!t.update(60, 1) && !t.update(61, 2) && t.seconds(61) == 30);  // back under 2 players: armed again
    }
    {  // W4M names of the hats, the multiplayer landscapes (assets from tools/w4m-re/setuplists.py)
        std::vector<std::string> maps = {"", "Multi_CarpetCapers", "Accuracy", "Clean-w3d", "ChallengeAccuracy", "ChallengeCrate"};
        Ui::multiplayerMaps(maps);
        if (FileExists("assets/multimaps.txt")) assert((maps == std::vector<std::string>{"", "Multi_CarpetCapers", "Clean-w3d", "ChallengeCrate"}));
        if (FileExists("assets/multimaps.txt")) {  // one entry per landscape: no two offered maps share their terrain data
            std::vector<std::string> all;
            FilePathList fl = LoadDirectoryFilesEx("assets/maps", ".cells", false);
            for (unsigned i = 0; i < fl.count; i++) all.push_back(GetFileNameWithoutExt(fl.paths[i]));
            UnloadDirectoryFiles(fl);
            Ui::multiplayerMaps(all);
            std::set<size_t> seen;
            for (const std::string &m : all) {
                int n = 0;
                unsigned char *d = LoadFileData(TextFormat("assets/maps/%s.cells", m.c_str()), &n);
                assert(d && seen.insert(std::hash<std::string_view>()({(char *)d, (size_t)n})).second);
                UnloadFileData(d);
            }
        }
    }
    for (int want = 0; want < (int)WEAPONS.size(); want++) {
        Game g;
        g.start({7, 2, 2, "", 0});
        Ui::Hud hud;
        if (!g.usable(g.worms[g.current].team, want)) continue;  // a delayed weapon is refused (FETXT.HTPSubtopic4)
        hud.select(want);
        uint32_t tick = 1;
        for (int f = 0; f < 8 * (int)WEAPONS.size() && g.weapon != want; f++) {
            Input in;
            in.buttons = (f % 2 ? 0 : Input::FIRE | Input::JUMP);  // A bounce while the pick is pending
            hud.input(g, in, true, 0, tick);
            assert(!(in.buttons & (Input::FIRE | Input::JUMP)));
            for (int t = 0; t < (f * 7 + want) % 3; t++) g.step(in), tick++;  // 0..2 ticks per frame
        }
        assert(g.weapon == want);
    }
    {  // Switch repro: take off, panel pick of Dynamite with A, thrust held, B (JUMP) drops it and the flight goes on
        Game g;
        g.start({29, 2, 1, "", 0});
        g.hotSeat = 0;
        int jp = -1, dyn = -1;
        for (int i = 0; i < (int)WEAPONS.size(); i++) jp = WEAPONS[i].name == "Jetpack" ? i : jp, dyn = WEAPONS[i].name == "Dynamite" ? i : dyn;
        Worm &a = g.worms[g.current];
        g.weapon = jp, g.ammo[a.team][jp] = 1, g.ammo[a.team][dyn] = 2, g.delays[a.team][jp] = g.delays[a.team][dyn] = 0;
        Ui::Hud hud;
        uint32_t tick = 1;
        auto frame = [&](uint8_t b) {
            Input in;
            in.buttons = b;
            hud.input(g, in, true, 0, tick++);
            g.step(in);
        };
        for (int f = 0; f < 3; f++) frame(0);
        for (int f = 0; f < 60 && !g.jetting; f++) frame(Input::FIRE);
        assert(g.jetting);
        hud.select(dyn);  // panel A: still held as thrust
        for (int f = 0; f < 10; f++) frame(Input::FIRE);
        assert(g.secondary == dyn && g.jetting);
        frame(Input::FIRE | Input::JUMP);
        assert(g.secondary < 0 && g.ammo[a.team][dyn] == 1);
        for (int f = 0; f < 150; f++) frame(Input::FIRE), assert(g.jetting);
    }
    for (const char *pk : {"Sheep", "Landmine", "Bazooka"}) {  // panel pick under the jetpack: toolDrop() = secondary, else the tool ends (0x565d30)
        Game g;
        g.start({29, 2, 1, "", 0});
        g.hotSeat = 0;
        int jp = -1, pw = -1;
        for (int i = 0; i < (int)WEAPONS.size(); i++) jp = WEAPONS[i].name == "Jetpack" ? i : jp, pw = WEAPONS[i].name == pk ? i : pw;
        Worm &a = g.worms[g.current];
        g.weapon = jp, g.ammo[a.team][jp] = 1, g.ammo[a.team][pw] = 2, g.delays[a.team][jp] = g.delays[a.team][pw] = 0;
        Ui::Hud hud;
        uint32_t tick = 1;
        auto frame = [&](uint8_t b) {
            Input in;
            in.buttons = b;
            hud.input(g, in, true, 0, tick++);
            g.step(in);
        };
        for (int f = 0; f < 3; f++) frame(0);
        for (int f = 0; f < 60 && !g.jetting; f++) frame(Input::FIRE);
        assert(g.jetting);
        hud.select(pw);
        for (int f = 0; f < 10; f++) frame(Input::FIRE);
        if (toolDrop(WEAPONS[pw])) assert(g.jetting && g.weapon == jp && g.secondary == pw);
        else assert(!g.jetting && g.weapon == pw && g.secondary < 0);
    }
    {  // W4M weapon panel grid (0x920e58): its 38 named cells but the Weapon Factory one (no team weapon here) hold ours; Teleport and the crate-only effects have none
        Game g;
        g.start({7, 2, 2, "", 0});
        std::set<int> seen;
        int filled = 0;
        for (int c = 0; c < 42; c++) if (int w = Ui::panelWeapon(c); w >= 0) filled++, seen.insert(w);
        assert(filled == 38 && seen.size() == 38);
        for (size_t w = 0; w < WEAPONS.size(); w++)
            if (!seen.count((int)w)) assert(WEAPONS[w].kind == Kind::Teleport || collected(WEAPONS[w].kind));
    }
    {  // user-requested: X during the count ends it; an open panel is never hidden by the ready screen
        Game g;
        g.start({7, 2, 2, "", 0});
        Ui::Hud hud;
        hud.forceX = true;
        g.phase = Phase::Settle, g.countGroup = {1};
        Input in;
        hud.input(g, in, true, 0, 1);
        assert((in.flags & Input::SKIP_COUNT) && !hud.open);
        g.phase = Phase::Aim, g.countGroup.clear(), hud.counting = -1, in = Input{};
        hud.input(g, in, true, 0, 2);
        assert(hud.open);
        hud.open = false;
        g.phase = Phase::Aim, g.hotSeat = 60, g.countGroup.clear(), hud.mine = true, hud.counting = 1;
        in = Input{};
        hud.input(g, in, true, 0, 2);
        assert(hud.open && hud.skipHp && !hud.readyScreen(g, false));
        hud.open = false, hud.mine = false;  // a CPU or remote turn shows the ready screen too
        assert(hud.readyScreen(g, false));
    }
    {  // victory fireworks (W4M 0x4ff8d0): nothing in the 4 s wait, PARTTWK emitters during the 5 s show, all burnt out after
        Fx::clear(), Fx::fireworks({40, 10, 40}, 40, 20);
        for (int t = 0; t < 240; t++) Fx::update(1 / 60.f);
        assert(Fx::count() == 0);
        int peak = 0;
        for (int t = 0; t < 300; t++) Fx::update(1 / 60.f), peak = std::max(peak, Fx::count());
        assert(peak > 50);
        for (int t = 0; t < 600; t++) Fx::update(1 / 60.f);
        assert(Fx::count() == 0);
    }
    {  // wind meter needle (0x5fc6e0): ArrowOrien tilt and roll, the -sin(-268 / 640) yaw term, grey fixed at -pi/2
        Vector2 grey = Ui::windPointer(false, 1, 0, {0, 1});
        assert(fabsf(grey.x - cosf(0.2f)) < 1e-4f && fabsf(grey.y - sinf(0.2f)) < 1e-4f);
        Vector2 ahead = Ui::windPointer(true, 0, 1, {0, 1});  // downwind along the view: up the screen, turned left by the yaw term
        assert(ahead.y < -0.5f && ahead.x < -0.2f);
        for (float a = 0; a < 6.3f; a += 0.1f) {
            Vector2 v = Ui::windPointer(true, cosf(a), sinf(a), {0.3f, 0.7f}), w = Ui::windPointer(true, -cosf(a), -sinf(a), {0.3f, 0.7f});
            assert(Vector2Length(v) > sinf(0.75f) - 1e-4f && Vector2Length(v) < 1 + 1e-4f);
            assert(fabsf(v.x + w.x) < 1e-4f && fabsf(v.y + w.y) < 1e-4f);
        }
    }
    {  // Text3D (0x5faaa0): HUD camera lens, so the em does not grow with the HeadCam zoom; the centre stays on the zoomed worm
        Camera3D c = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, Controls::FOV0, CAMERA_PERSPECTIVE};
        Vector3 p = {0.4f, 0.3f, -8};
        Vector2 a0, a1;
        float e0 = Ui::text3dPlace(p, 0.25f, c, {0, 0, -1}, {1280, 720}, a0);
        c.fovy = 2 * atanf(0.48f * 0.05f) * RAD2DEG;
        float e1 = Ui::text3dPlace(p, 0.25f, c, {0, 0, -1}, {1280, 720}, a1);
        Vector2 w = GetWorldToScreenEx(p, c, 1280, 720);
        assert(e0 > 10 && e1 > e0 && e1 < e0 * 1.1f && fabsf(a1.x - 640 - (w.x - 640) * 160 / 159) < 0.5f);
    }
    // FE clips run the exe's key curve at FE.AnimSpeed 0.9: in_scalehitxy scale 0.75 / 1.012 at clip 0.05 / 0.1 s (w4m-models eval)
    assert(fabsf(Ui::clipKeys(Ui::IN_SCALEHIT_S, 6, 0.045f) - 0.75f) < 0.005f && fabsf(Ui::clipKeys(Ui::IN_SCALEHIT_S, 6, 0.09f) - 1.012f) < 0.005f);
    for (int i = 0; i < (int)Audio::Sfx::Count; i++) {  // a match sound must never be a frontend one (the enum, the files and the events march together)
        auto id = (Audio::Sfx)i;
        bool fe = !strncmp(Audio::sfxFile(id), "fe_", 3) || !strncmp(Audio::sfxFile(id), "wormpot_", 8), feEv = strstr(Audio::sfxEvent(id), "frontendsfx/") || strstr(Audio::sfxEvent(id), "In_") || strstr(Audio::sfxEvent(id), "FEError") || strstr(Audio::sfxEvent(id), "Typewriter") || strstr(Audio::sfxEvent(id), "Highlight") || strstr(Audio::sfxEvent(id), "click2");
        if (fe != feEv) printf("sfx %d: file %s / event %s\n", i, Audio::sfxFile(id), Audio::sfxEvent(id));
        assert(fe == feEv);
    }
    {  // game-over text: versus only; a mission or challenge shows missionEnd's panel alone
        static MissionSpec ms;
        Game g;
        g.start({7, 2, 2, "", 0});
        g.phase = Phase::GameOver;
        g.winner = -1;
        assert(Ui::resultBanner(g) == "Nobody" && !strcmp(Ui::resultLine(g), "Wins the round!"));
        g.winner = 1;
        assert(Ui::resultBanner(g) == "Team 2" && !strcmp(Ui::resultLine(g), "Wins the match!"));
        g.allied = {2, 2};  // an alliance of two standing teams wins under its colour name (0x4f5280 > 1)
        assert(Ui::resultBanner(g) == "Green");
        g.surrendered[0] = 1;
        assert(Ui::resultBanner(g) == "Team 2");
        g.cfg.mission = &ms;
        assert(Ui::resultBanner(g).empty());
        g.winner = -1;
        assert(Ui::resultBanner(g).empty());
    }
    {  // saved teams: W4M's 7 defaults seed teams.txt, an old setup.txt's teams join once, 20 at most, the last human team stays
        const char *path = "barracks_check.txt", *setup = "barracks_check_setup.txt";
        remove(path);
        SaveFileText(setup, (char *)"team 0 0 1 cowbo Old Boys\nteam 1 2 0 - Gunslingers\nnet 0 0 0 - Net Team\n");
        Barracks b;
        b.load(path, "assets/defteams.txt", setup);
        assert(b.teams.size() == 9 && b.humans() == 6 && b.teams[0].name == "Gunslingers" && b.teams[0].worms[0] == "Shotgun Sam" && b.teams[0].grave == 8);
        assert(b.teams[0].flag == "Flag.Cowboy" && b.teams[0].speech == "vocowbo" && b.teams[0].hat == "WXFE.A.Hat.Cowboy" && b.teams[0].glasses.empty());
        assert(b.teams[6].name == "CPU Hard" && b.teams[6].skill == 5 && b.teams[7].name == "Old Boys" && b.teams[7].speech == "vocowbo" && b.teams[8].name == "Net Team");
        assert(b.teams[7].hat == Barracks::legacyHat(1) && !b.teams[7].hat.empty());
        assert(b.save(path));
        Barracks c;
        c.load(path, "assets/defteams.txt", setup);  // teams.txt exists: no seeding, no migration
        assert(c.teams.size() == 9 && c.teams[1].worms[3] == "Kickin Kel " && c.teams[8].name == "Net Team");
        assert(c.create("", 0, "A,B", "Flag.UK", 1) < 0 && c.create("Scots", 0, "A,B", "Flag.UK", 1) < 0);  // empty, taken
        int n = c.create("New", 3, "A,B,C,D,E,F,G", "Flag.UK", 7);
        assert(n == 9 && c.teams[n].flag == "Flag.Terminator" && c.teams[n].skill == 3 && c.teams[n].speech == "voclassi");
        for (int i = 0; i < 6; i++) for (int j = 0; j < i; j++) assert(!c.teams[n].worms[i].empty() && c.teams[n].worms[i] != c.teams[n].worms[j]);
        while ((int)c.teams.size() < Barracks::MAX) assert(c.create("T" + std::to_string(c.teams.size()), 0, "A", "Flag.UK", 1) >= 0);
        assert(c.create("Full", 0, "A", "Flag.UK", 1) < 0);
        for (int i = (int)c.teams.size() - 1; i >= 0; i--) if (c.teams[i].skill || c.humans() > 1) assert(c.remove(i));
        assert(c.humans() == 1 && !c.canDelete(c.find(c.teams[0].name)) && !c.remove(0));
        GameConfig::Team t = b.teams[1].slot(5, 2, -1);
        assert(t.name == "Funky Dudes" && t.worms == 5 && t.alliance == 2 && t.handicap == -1 && t.glasses == "WXFE.A.Glasses.Star" && t.wormNames.size() == 6);
        remove(path), remove(setup);
    }
    {  // W4M team lists: Insert hides the teams already in a slot and, where CPU teams are off, the CPU ones; the human lists skip CPU teams; Delete protects the last human team
        Barracks b;
        remove("barracks_check.txt");
        b.load("barracks_check.txt", "assets/defteams.txt");
        auto placed = [](const std::string &n) { return n == "Gunslingers" || n == "CPU Easy"; };
        assert(b.list(Barracks::List::Insert).size() == 7 && b.list(Barracks::List::Edit).size() == 7 && b.list(Barracks::List::Human).size() == 4);
        std::vector<int> ins = b.list(Barracks::List::Insert, placed), noCpu = b.list(Barracks::List::Insert, placed, false);
        assert(ins.size() == 5 && b.teams[ins[0]].name == "Funky Dudes" && noCpu.size() == 3 && b.teams[noCpu.back()].name == "Scots");
        assert(b.list(Barracks::List::Delete).size() == 7);
        while (b.humans() > 1) b.remove(0);
        std::vector<int> del = b.list(Barracks::List::Delete);
        assert(del.size() == 3 && b.list(Barracks::List::Human).size() == 1 && b.canCreate());
        for (int i = 0; i < Barracks::MAX; i++) b.create("T" + std::to_string(i), 0, "A", "Flag.UK", 1);
        assert(!b.canCreate());
    }
    {  // slots: a team enters with min(4, 16 - the worms in), the row caps keep the match at 16 worms, the lowest free alliance
        Lobby L;
        L.cfg.teams = Lobby::MAX_TEAMS, L.cfg.teamSetup.assign(Lobby::MAX_TEAMS, {});
        SavedTeam st;
        st.name = "A";
        assert(L.newWorms() == 4 && L.freeAlliance() == 0 && L.count() == 0);
        assert(L.place(0, 1, st.slot(6, L.freeAlliance())) && L.place(1, 0, st.slot(6, L.freeAlliance())) && !L.place(1, 0, st.slot(1)));
        assert(L.newWorms() == 4 && L.wormsIn() == 12 && L.cfg.teamSetup[0].pad == 1 && L.cfg.teamSetup[1].alliance == 1 && L.freeAlliance() == 2);
        assert(L.place(2, 0, st.slot(3, 2)) && L.newWorms() == 1 && L.maxWorms(2) == 4 && L.maxWorms(0) == 6 && L.freeAlliance() == 3);
        L.cfg.teamSetup[2].worms = 4;
        assert(L.newWorms() == 0 && L.wormsIn() == 16 && L.maxWorms(2) == 4 && L.maxWorms(0) == 6);
        L.cfg.teamSetup[0].worms = 3;
        assert(L.newWorms() == 3 && L.maxWorms(1) == 6 && L.release(2) && !L.playing(2) && L.count() == 2 && L.wormsIn() == 9);
    }
    {  // Story progress is the picked team's (W4M StoredTeamData StoryMissionsCompleted), the unlocks and movie flags the profile's; an older progress.txt becomes the first team's
        const std::string funky = Progress::teamFile("", "Funky Dudes"), scots = Progress::teamFile("", "Scots");
        assert(funky == "progress_Funky%20Dudes.txt" && funky != scots);
        for (const char *f : {"pg_check.txt", "pg_check_old.txt", "pg_check_first.txt"}) remove(f);
        Progress a;
        a.open("pg_check.txt", funky.c_str());
        a.record("m1", true, 900), a.unlocks = {"Lock.A"}, a.welcome = true;
        a.save();
        Progress b;
        b.open("pg_check.txt", scots.c_str());
        assert(b.get("m1").best == 0 && !b.get("m1").done && b.unlocks == a.unlocks && b.welcome);
        Progress c;
        c.open("pg_check.txt", funky.c_str());
        assert(c.get("m1").done && c.get("m1").best == 900);
        SaveFileText("pg_check_old.txt", (char *)"m1 1 500\nm2 0 0\nunlock Lock.X\nintro\n");
        Progress::migrate("pg_check_old.txt", "pg_check_first.txt");
        Progress d, e;
        d.open("pg_check_old.txt"), e.open("pg_check_old.txt", "pg_check_first.txt");
        assert(d.entries.empty() && d.storyIntro && d.unlocks.size() == 1 && e.get("m1").done && e.get("m1").best == 500 && e.entries.size() == 2);
        Progress::migrate("pg_check_old.txt", "pg_check_first.txt");  // nothing left to move
        for (const char *f : {"pg_check.txt", "pg_check_old.txt", "pg_check_first.txt", funky.c_str()}) remove(f);
    }
    {  // the attachments a team may wear: State Unlocked by default, or its Lock.* in the save's unlocks, the current one stays
        const Catalog &k = catalog();
        std::vector<std::string> hats = k.usable(0, {}, "");
        assert(hats[0].empty() && std::find(hats.begin(), hats.end(), "WXFE.A.Hat.Cowboy") != hats.end());
        const Catalog::Item *locked = nullptr;
        for (const Catalog::Item &a : k.attachments) if (a.type == 0 && a.state != 2 && !a.lock.empty()) { locked = &a; break; }
        assert(locked && std::find(hats.begin(), hats.end(), locked->id) == hats.end());
        assert(std::find(k.usable(0, {locked->lock}, "").begin(), k.usable(0, {locked->lock}, "").end(), locked->id) != k.usable(0, {locked->lock}, "").end());
        std::vector<std::string> kept = k.usable(0, {}, locked->id);
        assert(std::find(kept.begin(), kept.end(), locked->id) != kept.end());
    }
    {  // the editor's lists: W4M's 16 graves, 91 flags (to their ui png), 30 speech rows, 7 stock weapons; attachments by type
        const Catalog &k = catalog();
        assert(k.graves.size() == 16 && k.graves[8].id == "Grave.WildWest" && k.graves[8].model == "graves/wildwest");
        assert(k.flags.size() == 91 && k.flag("Flag.WizardsHat") && k.flag("Flag.WizardsHat")->model == "flags/custom_wizard_hat");
        assert(k.speech.size() == 30 && k.stockWeapons.size() == 7 && k.stockWeapons[0] == "FETXT.AFewProblems");
        assert(k.attachment("WXFE.A.Mustache.Cowboy") && k.attachment("WXFE.A.Mustache.Cowboy")->type == 3 && !strcmp(k.drawName("WXFE.A.Hat.Cowboy"), "cowboy"));
        assert(!strcmp(k.drawName("WXFE.A.Glasses.Star"), "glasses/star") && !k.drawName(""));
        // the hat's Glasses / Tash bools and the Outfit preview's FrontEndOffsets
        const Catalog::Item *suit = k.attachment("WXFE.A.Hat.Spacesuit"), *star = k.attachment("WXFE.A.Glasses.Star");
        assert(suit && !suit->glasses && !suit->tash && k.attachment("WXFE.A.Hat.Afro")->glasses);
        assert(star && star->fePos[0] == 7 && star->feRot[0] == -0.3f && star->feScale[1] == 1.5f);
    }
    puts("ui_check ok");
}
