// Included at the end of ui.cpp (inside namespace Ui): W4M's match-end screens WXFE.WinMatch / WinRound / PreRound (MENUTWKXPCCOMMON, MENUTWKX) and
// the network variants WXNET.WinMatch / DrawMatch, built from the frontend widgets above. Layout: docs/w4m/frontend.md "Match flow".

// In_SpringX = the scale X channel of In_ScaleY's keys; In_Next (Bundl10 WXFrontend.Anim in_next: scale XY and rotate Z)
static const float K_IN_NEXT_S[7][6] = {{0, 0, 0.39648f, 0.054565f, 0, 0}, {0.00016665f, 0.44629f, 0.43433f, 0.38306f, 0.24548f, 0.21667f},
                                        {0.125f, 1.0664f, 0.0070343f, 0.45874f, 0.0016842f, 0.017197f}, {0.16663f, 0.96875f, 0.041656f, -0.097717f, 0.041656f, 0.050232f},
                                        {0.20825f, 1.0186f, 0.041656f, 0.050232f, 0.041656f, -0.026138f}, {0.25f, 0.99316f, 0.041656f, -0.026138f, 0.041656f, 0.0063591f},
                                        {0.2915f, 0.99951f, 0.041656f, 0.0063591f, 1, 0}},
                   K_IN_NEXT_R[2][6] = {{0, 0.34888f, 0.044098f, -0.32935f, 0.12793f, -0.95557f}, {0.2915f, 0, 0.62549f, 0, 0.62549f, 0}};
static const FeClip IN_SPRINGX = {keys(K_IN_SCALEY, false), {}, {}, {}}, IN_NEXT = {keys(K_IN_NEXT_S, true), keys(K_IN_NEXT_S, true), {}, {}, keys(K_IN_NEXT_R, true)},
                    IN_SCALEHITXY = {keys(IN_SCALEHIT_S, false), keys(IN_SCALEHIT_S, false), keys(IN_SCALEHIT_X, false), keys(IN_SCALEHIT_Y, false)};

namespace {
const float RESULTS_OUT = 0.25f;  // the items' Out_* clips (0.21 / 0.25 s x FE.AnimSpeed) are done
const Color GC_HEADER_ORANGE[2] = {{255, 241, 174, 255}, {255, 163, 73, 255}}, GC_BLANK[2] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
const Color GC_TEAM[4][2] = {{{255, 74, 38, 255}, WHITE}, {{109, 148, 255, 255}, WHITE}, {{83, 255, 127, 255}, WHITE}, {{255, 224, 100, 255}, WHITE}};  // text colours 15..18
const char *const W4M_WEAPON[31] = {"Bazooka", "Grenade", "ClusterGrenade", "Airstrike", "Dynamite", "HolyHandGrenade", "BananaBomb", "Landmine", "Shotgun",
                                    "BaseballBat", "Prod", "FirePunch", "HomingMissile", "Flood", "Sheep", "GasCanister", "OldWoman", "ConcreteDonkey",
                                    "SuperSheep", "Starburst", "FactoryWeapon", "AlienAbduction", "Fatkins", "Scouser", "NoMoreNails", "PoisonArrow",
                                    "SentryGun", "SniperRifle", "SuperAirstrike", "ClusterBomb", "Bananette"};  // kWeapon<name> 1..31 (table 0x90c920)

// one ListDetail stat row (the MatchStatsList rows): label right of the centre, value left of it, in a W4M row `w` FE wide centred at c
void resultRow(Vector2 c, float w, float size, const char *label, const char *value, const Color *lg, const Color *vg, int anim) {
    float half = w * 0.48f, k = feK;  // the row's centre is spread like every FE position, its columns are not
    FeFlat flat;
    FeBox b = {{c.x * k - (2.5f + half / 2), c.y}, half / 2, size / 2, size, 0, -1, 5, 0, true, lg, lg == GC_WHITE ? BC_WHITE : BC_BLACK};
    feText(label, b);
    b = {{c.x * k + (2.5f + half / 2), c.y}, half / 2, size / 2, size, 0, -1, 3, anim, true, vg, vg == GC_WHITE ? BC_WHITE : BC_BLACK};
    feText(value, b);
}
}  // namespace

// The three results menus. WinMatch / WinRound / PreRound items by their W4M data (docs/w4m/frontend.md "Match flow"); returns the action
int results(ResultsScreen &st, Results &r, const std::vector<std::string> &maps) {
    const std::string &map = r.cfg.map;
    using S = Audio::Sfx;
    menuPage = true;
    static float last = 0;
    static std::string winLast;
    float t = now(), dt = fminf(t - last, 0.1f);
    if (st.shown < 0) {  // the menu's Audio_Incoming (In_Controller for the end screens, none for PreRound)
        st.shown = t, st.leaving = -1, st.sel = st.kind == ResultsScreen::PreRound || st.kind == ResultsScreen::WinRound ? 1 : 0;
        if (st.kind != ResultsScreen::PreRound) Audio::play(S::FeController);
        st.pageT = t;
    }
    last = t;
    const float since = t - st.shown, out = st.leaving >= 0 ? t - st.leaving : -1;
    const bool busy = st.leaving >= 0 || st.confirm >= 0 || st.land >= 0, match = st.kind == ResultsScreen::WinMatch, round = st.kind == ResultsScreen::WinRound,
               pre = st.kind == ResultsScreen::PreRound, draw = st.kind == ResultsScreen::DrawMatch;
    const int teams = (int)r.pages.size();
    int action = ResultsScreen::None;
    if (st.leaving >= 0 && t - st.leaving >= RESULTS_OUT) {  // its items are out: what follows them
        st.leaving = -1, st.shown = -1;
        if (st.after == ResultsScreen::PreRound) st.kind = ResultsScreen::PreRound;
        else return st.after == ResultsScreen::NextRound ? ResultsScreen::NextRound : ResultsScreen::Leave;
        return ResultsScreen::None;
    }
    auto leave = [&](int then) { st.leaving = t, st.after = then, Audio::play(S::FeNextOut); };
    // input: the items' Navigate_* of the data; NAV quit asks (WinRound, PreRound) or leaves (WinMatch)
    enum { Quit, Ok, Prev, Next, Land };
    if (!busy) {
        int dx = P({RIGHT}, {KEY_RIGHT}) - P({LEFT}, {KEY_LEFT}), dy = P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP});
        bool ok = P({A}, {KEY_ENTER, KEY_SPACE}), back = P({B}, {KEY_BACKSPACE, KEY_ESCAPE});
        if (match) {
            const bool canPrev = st.page > 0, canNext = st.page < teams - 1;
            if (st.sel == Quit && (dx > 0 || dy < 0) && teams > 1) st.sel = canPrev ? Prev : Next;
            else if (st.sel == Prev) st.sel = dx > 0 && canNext ? Next : dx < 0 || dy > 0 ? Quit : Prev;
            else if (st.sel == Next) st.sel = dx < 0 && canPrev ? Prev : dy > 0 ? Quit : Next;
            if (!canPrev && st.sel == Prev) st.sel = Next;
            if (!canNext && st.sel == Next) st.sel = canPrev ? Prev : Quit;
            if (ok && (st.sel == Prev || st.sel == Next)) st.page += st.sel == Next ? 1 : -1, st.pageT = t;  // Match.TeamStats.Next / Prev
            else if (ok || back) leave(ResultsScreen::Leave);
        } else if (draw) {
            if (ok || back) leave(ResultsScreen::Leave);
        } else {
            if (st.sel == Quit && dx > 0) st.sel = Ok;
            else if (st.sel == Ok && dx < 0) st.sel = Quit;
            if (pre && dy < 0 && st.sel != Land) st.sel = Land;
            else if (st.sel == Land && (dy > 0 || dx != 0)) st.sel = dx > 0 ? Ok : Quit;
            if (ok && st.sel == Land && !maps.empty()) {  // Request^Menu^Landscape: WXFEP.SelectLandscape2
                st.land = std::max<int>(0, int(std::find(maps.begin(), maps.end(), map) - maps.begin())), st.landShown = t;
                if (st.land >= (int)maps.size()) st.land = 0;
            } else if (back || (ok && st.sel == Quit)) st.confirm = 0, st.popShown = t;  // WXFEP.ConfirmMatchQuit
            else if (ok && st.sel == Ok) leave(round ? ResultsScreen::PreRound : ResultsScreen::NextRound);
        }
    }
    if (st.confirm >= 0 && t - st.popShown > 0.1f) {
        int dy = P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP});
        if (dy) st.confirm = clampWrap(st.confirm + dy, 2);
        if (P({B}, {KEY_BACKSPACE, KEY_ESCAPE})) st.confirm = -1;
        else if (P({A}, {KEY_ENTER, KEY_SPACE})) {  // No closes it; Yes: AnimDivide$Out_Split, FE.ChangeMenu$WXFE.MainMenu
            if (st.confirm) st.confirm = -1, leave(ResultsScreen::Leave);
            else st.confirm = -1;
        }
    }

    if (st.land >= 0 && t - st.landShown > 0.1f) {  // the highlight moves the picture (WXD.Level.Copy), A sets the level, B keeps it
        int dy = P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP});
        if (dy) st.land = clampWrap(st.land + dy, (int)maps.size());
        if (P({B}, {KEY_BACKSPACE, KEY_ESCAPE})) st.land = -1;
        else if (P({A}, {KEY_ENTER, KEY_SPACE})) r.cfg.map = maps[st.land], st.land = -1;
    }

    // ---- the scene: the generic back, the Paper Fader (PaperPopUp2 tinted (255, 80, 0, 230), LayerOffset -5), then the items
    uploadArt();
    if (Texture2D bg = tex("back/loadbackgeneric"); bg.id) DrawTexturePro(bg, {0, 0, (float)bg.width, (float)bg.height}, {0, 0, 1280, 720}, {}, 0, WHITE);
    FrontBg::page(1);
    FrontBg::draw(dt, false);
    const bool splitOut = out >= 0 && st.after == ResultsScreen::Leave;  // Messages_CancelPressed / Yes: AnimDivide$Out_Split
    feMesh("bluedivide", {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, false, splitOut ? "Out_Split" : "In_Split", splitOut ? out : since);  // BeforeMenuDisplayed: AnimDivide$In_Split
    if (!image("fe/paperpopup02", {-1000, -400, 3280, 1520}, {255, 80, 0, 230})) DrawRectangle(0, 0, 1280, 720, {255, 80, 0, 230});
    auto slide = [&](float x, float y, const FeClip *in, float delay, const FeClip *outc, float slideBy = -1300) { return feItem(fePos(x, y), in, delay, outc, since, out, slideBy); };

    if (!pre && !draw) {  // Firework1..7 (WXFE_MeshObjectParticleDesc: effect at the item's FE position, SpawnDelay then every SpawnLoopTime ms)
        static const struct { const char *fx; float x, y, delay, loop; } FW[7] = {{"WXP_FE_Firework3", 250, 40, 6, 8}, {"WXP_FE_Firework2", -170, 0, 0.1f, 8},
                                                                                    {"WXP_FE_Firework4", 130, 110, 0.5f, 8}, {"WXP_FE_Firework1", 120, -100, 3, 8},
                                                                                    {"WXP_FE_Firework5", -130, -90, 7.5f, 8}, {"WXP_FE_Firework2", -120, 130, 3.5f, 8},
                                                                                    {"WXP_FE_Firework6", 130, 110, 5.2f, 9}};
        static int fired[7];
        static float firedAt[7];
        if (since < 0.02f) for (int &f : fired) f = 0;
        for (int i = 0; i < 7; i++)
            if (out < 0 && since >= FW[i].delay && (!fired[i] || since >= firedAt[i] + FW[i].loop)) fired[i] = 1, firedAt[i] = since, Fx::start(FW[i].fx, {FW[i].x * 0.05f, FW[i].y * 0.05f, 0}, 0.05f);
        Fx::update(dt);
        Camera3D cam = {{0, 0, 903.5f * 0.05f}, {0, 0, 0}, {0, 1, 0}, 0.54f * RAD2DEG, CAMERA_PERSPECTIVE};
        BeginMode3D(cam);
        Fx::draw(cam);
        EndMode3D();
    }
    if (!pre && !draw || draw) {  // Scrolling 2 / 3 / 1: FE.WinString, ScrollText with a Solid_Team_Red outline, wrapping, moving back
        static const struct { float x, y, size, half, speed, delay; } SC[3] = {{-6, 229, 60, 340, 0.2f, 0.35f}, {-4, -225, 30, 340, 0.1f, 0.385f}, {-4, 186, 30, 340, 0.1f, 0.385f}};
        for (const auto &c : SC) {
            if (!feItem(fePos(c.x, c.y), &IN_SLIDEX, c.delay, nullptr, since, out)) continue;
            std::string tile = r.winString + " ";
            float px = c.size * FE_S * 0.85f, w = textWidth(tile.c_str(), px) * TEXT_X, shift = fmodf(floorf(t * 50) * c.speed * FE_S, w);
            Vector2 o = fePos(c.x, c.y);
            rlPushMatrix(), rlTranslatef(o.x, o.y, 0), rlRotatef(-0.026f * RAD2DEG, 0, 0, 1);
            BeginScissorMode(0, (int)(o.y - px), 1280, (int)(2 * px));
            for (float u = -1300 - shift; u < 1300; u += w) rlPushMatrix(), rlTranslatef(u, 0, 0), rlScalef(TEXT_X, 1, 1), textG(tile.c_str(), 0, -px / 2, px, GC_SCROLLTEXT[0], GC_SCROLLTEXT[1], 0, {255, 78, 60, 255}), rlPopMatrix();
            rlDrawRenderBatchActive();
            EndScissorMode();
            rlPopMatrix(), rlPopMatrix();
        }
    }
    if (pre) {  // Scrolling 1..3 carry WXFE.PreRoundTitle
        static const struct { float x, y, size, speed, delay; } SC[3] = {{-6, 229, 60, 0.2f, 0.35f}, {-4, -225, 30, 0.1f, 0.385f}, {-4, 186, 30, 0.1f, 0.385f}};
        for (const auto &c : SC) {
            if (!feItem(fePos(c.x, c.y), &IN_SLIDEX, c.delay, nullptr, since, out)) continue;
            std::string tile = std::string(TextFormat(tr("FETXT.Round", "Round %u"), (unsigned)r.round + 1)) + " ";
            float px = c.size * FE_S * 0.85f, w = textWidth(tile.c_str(), px) * TEXT_X, shift = fmodf(floorf(t * 50) * c.speed * FE_S, w);
            Vector2 o = fePos(c.x, c.y);
            rlPushMatrix(), rlTranslatef(o.x, o.y, 0), rlRotatef(-0.026f * RAD2DEG, 0, 0, 1);
            BeginScissorMode(0, (int)(o.y - px), 1280, (int)(2 * px));
            for (float u = -1300 - shift; u < 1300; u += w) rlPushMatrix(), rlTranslatef(u, 0, 0), rlScalef(TEXT_X, 1, 1), textG(tile.c_str(), 0, -px / 2, px, GC_SCROLLTEXT[0], GC_SCROLLTEXT[1], 0, {255, 78, 60, 255}), rlPopMatrix();
            rlDrawRenderBatchActive();
            EndScissorMode();
            rlPopMatrix(), rlPopMatrix();
        }
    }
    if (slide(0, -159.1f, &IN_SCALEY, 0.3f, &OUT_SCALEY)) tooltipStrip(), rlPopMatrix();  // ToolTip
    if (st.kind != ResultsScreen::DrawMatch && slide(11, -159, &IN_SLIDEX, 0.3f, &OUT_SCALEY)) {  // TickerTapeTXT: the highlighted item's tooltip (WXMsg.CopyString^FETIP.*^FE.ToolTip)
        const char *tip = match ? tr("FETIP.EndOfMatch", "End of match results.") : round ? (st.sel == 1 ? tr("FETIP.EndOfRound", "End of round results.") : tr("FETIP.Return", "Return to the main menu."))
                          : st.sel == 1 ? tr("FETIP.Ready", "Ready?") : st.sel == 2 ? tr("FETIP.Landscape2", "Select a landscape to battle on.") : tr("FETIP.Return", "Return to the main menu.");
        tickerText(t, tip), rlPopMatrix();
    }  // TickerTapeTXT
    {  // HeaderBottom (white outline, Blank fill) under HeaderTop (Header_Orange, Button_Yellow on PreRound), both FE.WinString / PreRoundTitle
        std::string head = pre ? TextFormat(tr("FETXT.Round", "Round %u"), (unsigned)r.round + 1) : r.winString;
        const FeClip *in = round ? &IN_SLIDEX : &IN_FLIPY;
        float delay = round ? 0.3f : 0.4f;
        if (slide(0, 200, in, delay, nullptr)) {
            FeBox b = {{0, 200}, 220, 20, 40, 0, -1, 4, 1, true, GC_BLANK, BC_WHITE};
            feText(head, b);
            b.grad = pre ? GC_BUTTON_YELLOW : GC_HEADER_ORANGE, b.edge = BC_NONE;
            feText(head, b);
            rlPopMatrix();
        }
    }
    const bool netVariant = st.net;
    if (match || draw || round) {
        const char *wins = round ? tr("FETXT.WinsTheRound", "Wins the round!", "Gagne le round !") : tr("FETXT.WinsTheMatch", "Wins the match!", "Gagne le match !");
        if (r.winner < 0 && !round && !draw) wins = "";  // (a draw ends in WinRound / DrawMatch)
        if (slide(0, 166, round ? &IN_SLIDEX : &IN_SPRINGX, round ? 0.35f : 0.6f, nullptr)) {  // WinsMatchTEXT / WinsRoundTEXT
            FeBox b = {{0, 166}, 170, 10, 24, 0.03f, -1, 4, 4, true, GC_WHITE, BC_WHITE};
            feText(wins, b), rlPopMatrix();
        }
    }
    if (match && teams) {
        const Results::Page &p = r.pages[Clamp(st.page, 0, teams - 1)];
        const float ex = netVariant ? -10 : 0, by = netVariant ? -121 : -101;
        const Color *tg = GC_TEAM[r.cfg.alliance(p.team) & 3];
        if (slide(ex, 138, &IN_SLIDEX, 0.8f, nullptr)) {  // Elapsed Time: row 5 of the list, List_Lable_Blue
            uint32_t s = r.match.seconds;
            resultRow({ex, 138}, 540, 22, tr("Stat.Match.ElapsedTime", "Total time"), TextFormat("%um, %us", s / 60, s % 60), GC_LIST_LABLE_BLUE, GC_LIST_LABLE_BLUE, 2);
            rlPopMatrix();
        }
        float pt = t - st.pageT;  // the page's AddItem: In_SlideX at 200 ms, its rows In_ScaleY at 250 + 50 n ms
        if (feItem(fePos(0, 70), &IN_SLIDEX, 0.2f, nullptr, since < pt ? since : pt + 0.2f, -1)) {
            Vector2 c = fePos(0, 70);
            Tilt tl(c, -0.03f);
            const char *wn = p.weapon >= 1 && p.weapon <= 31 ? tr(TextFormat("Text.kWeapon%s", W4M_WEAPON[p.weapon - 1]), W4M_WEAPON[p.weapon - 1]) : "";
            const char *lab[5] = {tr("FETXT.Team", "Team"), tr("Stat.Match.Damage", "Total Damage"), tr("Stat.Match.Kills", "Kills"), tr("Stat.Match.Accuracy", "Accuracy"),
                                  tr("Stat.Match.Weapon", "Weapon of choice")};
            std::string val[5] = {p.name, TextFormat("%u", p.damage), TextFormat("%u", p.kills), TextFormat("%u", p.accuracy), wn};
            for (int i = 0; i < 5; i++) {
                float rowT = i ? pt - (0.25f + 0.05f * (i - 1)) : 1;
                if (i && !feItem(fePos(0, 70 - 25.0f * i), &IN_SCALEY, 0, nullptr, rowT + 0.0f, -1)) continue;
                resultRow({0, 70 - 25.0f * i}, 540, 24, lab[i], val[i].c_str(), GC_WHITE, tg, 2);
                if (i) rlPopMatrix();
            }
            rlPopMatrix();
        }
        if (slide(0, by, &IN_SLIDEX, 0.8f, nullptr)) {  // ViewTeamsLABEL
            FeBox b = {{0, by}, 41, 12, 22, 0, -1, 4, 0, true, GC_WHITE, BC_WHITE};
            feText(tr("FETXT.Teams", "Teams"), b), rlPopMatrix();
        }
        for (int k = 0; k < 2; k++)  // Previous Team / Next Team
            if (slide(k ? 53 : -53, by, &IN_SLIDEX, 0.9f, nullptr)) {
                bool en = k ? st.page < teams - 1 : st.page > 0;
                pageArrow(k, !en ? 2 : st.sel == (k ? Next : Prev) ? 1 : 0, {k ? 53.0f : -53.0f, by}, 18), rlPopMatrix();
            }
    }
    if (round) {  // Stat Row 1..4 (WXFE.WinRoundList rows 2..5), else the "No Stats" text (WXD.StatResultFailed)
        static const struct { float x, y, rot, delay; } ROW[4] = {{-20, 80, 0.05f, 0.4f}, {-20, 50, 0.05f, 0.57f}, {20, -30, -0.05f, 0.63f}, {20, -60, -0.05f, 0.7f}};
        if (r.awards.empty()) {
            FeBox b = {{0, 0}, 250, 110, 30, -0.07f, fmaxf(since, 0), 4, 0, true, GC_WHITE, BC_WHITE};
            feText(tr("FETXT.TutRecBody", "FETXT.TutRecBody"), b);
        }
        for (int i = 0; i < (int)r.awards.size() && i < 4; i++) {
            const Results::Award &a = r.awards[i];
            if (!slide(ROW[i].x, ROW[i].y, &IN_SPRINGXY, ROW[i].delay, &OUT_SCALEY)) continue;
            Vector2 c = fePos(ROW[i].x, ROW[i].y);
            Tilt tl(c, ROW[i].rot);
            const float k = feK;
            FeFlat flat;
            FeBox b = {{ROW[i].x * k - (1.5f + 270 * 0.62f), ROW[i].y}, 270 * 0.62f, 15, 28, 0, -1, 5, 2, true, GC_WHITE, BC_WHITE};
            feText(a.label, b);
            b = {{ROW[i].x * k + (1.5f + 270 * 0.35f), ROW[i].y}, 270 * 0.35f, 15, 28, 0, -1, 3, 2, true, GC_TEAM[r.cfg.alliance(a.team) & 3], BC_WHITE};
            feText(a.who, b);
            rlPopMatrix();
        }
    }
    if (pre) {
        const unsigned n = (unsigned)r.cfg.teams;
        if (slide(120, -17, &IN_SPRINGX, 0.4f, nullptr)) {  // Paper Back (layer 0)
            Tilt tl(fePos(120, -17), -0.04f);
            image("fe/paperpopup02", feRect(120, -17, 130, 118)), rlPopMatrix();
        }
        if (slide(-147, 111, &IN_SLIDEX, 0.35f, nullptr)) edHeader(tr("FETXTSH.Landscape", "Landscape"), -147, 111, 120, 0.04f), rlPopMatrix();
        if (slide(123, 111, &IN_SLIDEX, 0.35f, nullptr)) edHeader(tr("FETXT.RoundsWon", "Rounds Won"), 123, 111, 120, -0.04f), rlPopMatrix();
        if (slide(120, -71, &IN_SCALEHITXY, 0.55f, nullptr)) {  // Prepare TEXT
            FeBox b = {{120, -71}, 100, 30, 22, 0.05f, -1, 4, 4, true, GC_WHITE, BC_WHITE};
            feText(tr("FETXT.PrepareTeams", "Prepare your teams for battle!"), b), rlPopMatrix();
        }
        if (slide(120, 18, &IN_SPRINGX, 0.4f, nullptr)) {  // PreRoundList: a row per team, its name (team colour) and its round wins
            Tilt tl(fePos(120, 18), -0.04f);
            const float k = feK;
            FeFlat flat;
            for (unsigned i = 0; i < n; i++) {
                float y = 18 + 45 - 30.0f * i;
                FeBox b = {{120 * k - (100 - 70), y}, 70, 13, 26, 0, -1, 3, 0, true, GC_TEAM[r.cfg.alliance((int)i) & 3], BC_WHITE};
                feText(Ui::teamName(r.cfg, (int)i), b);
                b = {{120 * k + (100 - 15), y}, 15, 13, 26, 0, -1, 5, 0, true, GC_WHITE, BC_WHITE};
                feText(TextFormat("%d", r.roundWins[i]), b);
            }
            rlPopMatrix();
        }
        if (slide(-144, -12, &IN_SPRINGX, 0.4f, nullptr)) {  // LandscapeControl: the picture in its ButtonBig frame, 115 FE outside
            Tilt tl(fePos(-144, -12), 0.03f);
            Rectangle pr = feRect(-144, -12, 98, 98);
            if (!cover(preview(map), pr)) DrawRectangleRec(pr, {30, 60, 40, 255});
            pictureFrame(pr, st.sel == Land);
            rlPopMatrix();
        }
    }
    // NAV buttons: quit (Back) and, on WinRound / PreRound, OK (the jar on PreRound, the tick on WinRound)
    if (slide(-256, -159, round ? &IN_PREV : nullptr, 0.3f, round ? &OUT_PREV : nullptr)) navButton(2, st.sel == Quit && !draw ? 1 : 0, {-256, -159}, 40), rlPopMatrix();
    if (round || pre)
        if (slide(264, -166, round ? &IN_SPRINGXY : &IN_NEXT, 0.3f, round ? &OUT_SCALEXY : nullptr)) navButton(pre ? 0 : 1, st.sel == Ok ? 1 : 0, {264, -166}, 40), rlPopMatrix();
    if (st.land >= 0) {  // WXFEP.SelectLandscape2: centre (0, 25), 200 x 125, the list at (95, -20) 95 x 78, the picture at (-96, -20) 90 x 90 on its Paper Back (-92, -24)
        DrawRectangle(0, 0, 1280, 720, {40, 40, 60, 160});
        FeFlat flat;  // the popup's own offsets, not spread
        popup(feRect(0, 25, 200, 125));
        image("fe/paperpopup02", feRect(-92, -24, 90, 90), {255, 70, 70, 255});
        Rectangle pr = feRect(-96, -20, 73, 73);
        if (!cover(preview(maps[st.land]), pr)) DrawRectangleRec(pr, {30, 60, 40, 255});
        pictureFrame(pr, false);
        const int rows = 5, first = Clamp(st.land - rows / 2, 0, std::max(0, (int)maps.size() - rows));
        for (int i = first; i < std::min((int)maps.size(), first + rows); i++) {
            Rectangle rr = feRect(95, 40 - 25.0f * (i - first), 90, 12);
            if (i == st.land) brush(rr);
            FeBox b = {{95, 40 - 25.0f * (i - first)}, 90, 12, 22, 0, -1, 4, 0, true, i == st.land ? GC_WHITE : GC_LIST_LABLE_BLUE, BC_BLACK};
            feText(mapTitle(maps[i]), b);
        }
    }
    if (st.confirm >= 0) {
        DrawRectangle(0, 0, 1280, 720, {40, 40, 60, 160});  // FullScreenColour
        yesNo(tr("FETXT.ConfirmQuit", "Exit Game? Are You Sure?", "Quitter le jeu ? Vraiment ?"), st.confirm, t - st.popShown);
    }
    return action;
}
