// Weapon panel: its direct pick (Input::pick) must land on the weapon whatever the ticks per frame.
// From client/: make ui_check
#include "../src/fx.h"
#include "../src/ui.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

int main() {
    loadWeapons("romfs/weapons.json");
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
    // FE clips run the exe's key curve at FE.AnimSpeed 0.9: in_scalehitxy scale 0.75 / 1.012 at clip 0.05 / 0.1 s (w4m-models eval)
    assert(fabsf(Ui::clipKeys(Ui::IN_SCALEHIT_S, 6, 0.045f) - 0.75f) < 0.005f && fabsf(Ui::clipKeys(Ui::IN_SCALEHIT_S, 6, 0.09f) - 1.012f) < 0.005f);
    puts("ui_check ok");
}
