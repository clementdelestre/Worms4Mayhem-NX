// Weapon panel: its direct pick (Input::pick) must land on the weapon whatever the ticks per frame.
// From client/: make ui_check
#include "../src/ui.h"
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
    {  // Switch repro: take off, panel pick of Dynamite with A, thrust held, ZL (JUMP) drops it and the flight goes on
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
    puts("ui_check ok");
}
