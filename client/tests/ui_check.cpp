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
            hud.input(g, in, true, 0, tick);
            for (int t = 0; t < (f * 7 + want) % 3; t++) g.step(in), tick++;  // 0..2 ticks per frame
        }
        assert(g.weapon == want);
    }
    puts("ui_check ok");
}
