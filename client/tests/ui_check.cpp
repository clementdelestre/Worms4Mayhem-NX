// Weapon panel: its direct pick (Input::pick) must land on the weapon whatever the ticks per frame.
// From client/: make ui_check
#include "../src/audio.h"
#include "../src/fx.h"
#include "../src/controls.h"
#include <cstring>
#include "../src/ui.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

int main() {
    loadWeapons("romfs/weapons.json");
    {  // Lobby: teams are added at the end, removing one shifts the later ones up, pads in use come from this console's human teams
        Lobby l;
        l.cfg.teams = 2, l.cfg.teamSetup.resize(Lobby::MAX_TEAMS);
        for (int k = 0; k < Lobby::MAX_TEAMS; k++) l.cfg.teamSetup[k].name = std::to_string(k), l.cfg.teamSetup[k].pad = (uint8_t)k;
        assert(l.addTeam() && l.addTeam() && !l.addTeam() && l.cfg.teams == 4);
        l.cfg.teamSetup[3].cpu = 2;
        assert(l.padsUsed() == 0b0111);  // team 3 is a CPU
        assert(l.removeTeam(1, 2) && l.cfg.teams == 3 && l.cfg.teamSetup[1].name == "2" && l.cfg.teamSetup[2].name == "3" && l.cfg.teamSetup[3].name == "1");
        assert(l.removeTeam(0, 2) && !l.removeTeam(0, 3));
        l.owner[0] = 1;  // another console's team: not editable here, but its pad is not ours either
        assert(!l.canEdit(0) && !l.removeTeam(0, 1) && l.isHost() && l.canEditMatch());
        l.self = 1;
        assert(!l.isHost() && !l.canEditMatch() && l.canEdit(0));
    }
    {  // Teams screen: two pad cursors move and edit different teams at once, and follow their team when another is removed
        Lobby l;
        l.cfg.teams = 3, l.cfg.teamSetup.resize(Lobby::MAX_TEAMS);
        TeamCursors c;
        c.card(1, 2, l.cfg.teams), c.row(0, 1, l.cfg.teams), c.row(1, 3, l.cfg.teams);
        assert(c.at[0].card == 0 && c.at[0].row == 1 && c.at[1].card == 2 && c.at[1].row == 3);
        assert(l.cycle(0, 1, 1, 9, 5, 7) && l.cycle(2, 3, 1, 9, 5, 7) && l.cfg.teamSetup[0].pad == 1 && l.cfg.teamSetup[2].hat == 1);  // both edits land
        c.card(1, 1, l.cfg.teams);  // the empty card has the name row only
        assert(c.at[1].card == 3 && c.at[1].row == 0);
        c.card(1, -1, l.cfg.teams), c.row(1, 0, l.cfg.teams);
        assert(l.removeTeam(1, 2));
        c.removed(1, l.cfg.teams);
        assert(c.at[1].card == 1 && c.at[0].card == 0);  // pad 2's team moved up with it
        l.owner[0] = 7;
        assert(!l.cycle(0, 1, 1, 9, 5, 7) && l.cycle(1, 2, -1, 9, 5, 7) && l.cfg.teamSetup[1].voice == 4);  // another console's team is read-only; voice wraps
    }
    {  // network teams saved in setup.txt: name with spaces, hat, pad and the number of teams come back
        std::vector<GameConfig::Team> mine = {{"Pirate Pete", 0, 0, 3, 2}, {"Two Words Here", 0, 0, 1, 1}}, back;
        std::string txt = Ui::netProfileText(mine) + "teams 3\n";
        size_t at = 0;
        for (size_t nl; (nl = txt.find('\n', at)) != std::string::npos; at = nl + 1) {
            bool net = Ui::netProfileLine(txt.substr(at, nl - at).c_str(), 10, back);
            assert(net == !txt.compare(at, 3, "net"));
        }
        assert(back.size() == 2 && back[0].name == "Pirate Pete" && back[0].hat == 3 && back[0].pad == 2 && back[1].name == "Two Words Here" && back[1].pad == 1);
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
        assert(Ui::resultBanner(g) == "DRAW!");
        g.winner = 1;
        assert(Ui::resultBanner(g) == "Team 2 WINS!");
        g.cfg.mission = &ms;
        assert(Ui::resultBanner(g).empty());
        g.winner = -1;
        assert(Ui::resultBanner(g).empty());
    }
    puts("ui_check ok");
}
