// Weapon panel: the NEXT_WEAPON presses it emits must land on the picked weapon whatever the ticks per frame.
// From client/: g++ -std=c++17 -I../third_party/raylib-nx/src tests/ui_check.cpp $(ls src/*.cpp | grep -v main) -o ui_check <raylib libs> && ./ui_check
#include "../src/ui.h"
#include <cassert>
#include <cstdio>

int main() {
    loadWeapons("romfs/weapons.json");
    for (int want = 0; want < (int)WEAPONS.size(); want++) {
        Game g;
        g.start({7, 2, 2, "", 0});
        Ui::Hud hud;
        if (!g.ammo[g.worms[g.current].team][want]) continue;
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
