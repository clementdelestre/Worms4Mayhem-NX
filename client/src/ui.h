#pragma once
#include "raylib.h"
#include "mission.h"
#include "net.h"
#include "lobby.h"
#include "barracks.h"
#include "sim.h"
#include <string>
#include <vector>

// false: no instant replay, no .w4r recording, no Replays menu entry.
constexpr bool REPLAYS = false;

// W4M-styled frontend and HUD. Art: assets/ui/*.png from tools/w4m-ui; missing files fall back to plain shapes.
namespace Ui {
extern const Color TEAM_COLORS[4];
void load();
bool preload(double until);  // boot: uploads the decoded frontend art until GetTime() reaches until; true while some is pending
bool warm(const std::vector<std::string> &names, double until);  // uploads the predecoded pictures until GetTime() reaches until; true while some are pending
void predecode(const std::vector<std::string> &names);  // decodes assets/ui/<name>.png in order on a worker; drawn once in, nothing before
void unload();
bool warmHud(int &i, double until);  // match prep: loads the HUD art (weapon icons, hud/) until GetTime() reaches until; true while more remain
void text(const char *t, float x, float y, float size, Color c, int align = 0);  // align: 0 left, 1 centre, 2 right
float textWidth(const char *t, float size);
const Font &textFont();  // the font text() draws, for rotated or outline-free text
Texture2D art(const char *name);  // assets/ui/<name>.png, cached; id 0 when missing
bool artReady(const char *name);  // art() would not read the SD card for it
void logo(float cx, float y, float w, float deg = 0, float stretch = 1);  // W4M logo, top centre at (cx, y), tilted deg
// one channel of a W4M frontend item clip t s after it started (WXFrontend.Anim keys {time s, value, in tangent x, y, out tangent
// x, y}, at FE.AnimSpeed), held past its ends
float clipKeys(const float (*keys)[6], int n, float t, bool weighted = false);
extern const float IN_SCALEHIT_S[6][6], IN_SCALEHIT_X[7][6], IN_SCALEHIT_Y[7][6];
// Menu language: 0 English, 1 French (lang.txt, else the system language). tr(): W4M's string for key
// (assets/lang/<en|fr>.txt from tools/w4m-ui), else the built-in en / fr text (key may be null).
extern int language;
const char *tr(const char *key, const char *en, const char *fr = nullptr);
void background();
std::string teamName(const GameConfig &c, int team);
std::string resultBanner(const Game &g);  // versus game-over text; empty in a mission or challenge (missionEnd shows the result)
void mapHeads(const std::vector<std::string> &maps);  // any thread: reads the maps' json heads that mapTitle() and the previews use
void multiplayerMaps(std::vector<std::string> &maps);  // keeps what W4M's landscape select offers
const char *hatLabel(const std::string &id);  // W4M's name of an attachment id, "None" for ""
const std::vector<std::string> &hatIds();     // "" (none), then the WXFE.A hats in W4M order
std::vector<std::string> speechIds();         // "vo" + each voices/ bank
int voiceBankOf(const std::string &speech);   // the Audio bank of a W4M speech id, -1 none (team default)
std::string mapTitle(const std::string &map);  // display name: W4M's level name, else from the file name
const char *wormName(const GameConfig &c, int team, int i);
// pad -1 = any pad. D-pad buttons also fire on left-stick flicks and auto-repeat while held.
bool pressed(int pad, std::initializer_list<int> buttons, std::initializer_list<int> keys);
void pollStick();  // once per frame, before any pressed()
// Bottom button hints. pad: "A" "B" "X" "Y" "+" "-" "L" "R" "ZL"...; key: shown on desktop without a controller.
struct Hint { const char *pad, *key, *label; };
void hints(std::initializer_list<Hint> h);
void hints(const std::vector<Hint> &h);
void controls(bool game);  // full-screen controller diagram: match or menu controls
bool helpHeld();            // - (desktop F1) held past 0.35 s: show controls()
extern bool forceHelp;      // --ui help captures

struct MenuItem;  // ui.cpp: one W4M menu entry and its place on screen
// Title, main menu, match setup, options. frame() updates and draws (inside Begin/EndDrawing).

struct Frontend {
    enum Screen { Title, Main, Options, Controls, SchemeEdit, Wormpot, Factory, FactoryEdit, Local, Network, MyWorms, HelpOpts, Confirm, Missions, SetupTeams, SetupMap, SetupRules, TeamEdit } screen = Title;  // Missions: hands over to missionMenu(); SetupTeams / Map / Rules: the match setup, local or in a network room (lobby); TeamEdit: W4M WXFE.TeamOptions
    enum Action { None, StartLocal, Quit, Replays, SinglePlayer, QuickMatch, Salon, RoomStart, LeaveRoom };  // Salon: open the network salon (wantLan); RoomStart: the host starts the room's match; LeaveRoom: B on the room's first screen
    const char *capture = nullptr;  // screenshot path for the next frame (--ui)
    int missionTab = 0;             // SinglePlayer: 0 story missions, 1 challenges
    std::string missionTeamName();  // missionTeam if it is still a saved team, else the first human one (null name: none)
    std::string missionTeam;        // the saved team picked for them (W4M WXFE.MissionTeamName): its progress, its name on the Challenges page
    Net *net = nullptr;             // the connection of a network room: SetupTeams / Map / Rules then show net->lobby
    bool wantLan = false;           // Salon: Réseau local, else En ligne
    int timer = -1;                 // lobby timer seconds (ReadyTimer::seconds), -1 none
    struct PadIn { int dx = 0, dy = 0, card = 0; bool ok = false, back = false, plus = false; };
    static unsigned simPads;        // tests (--ui setup2): pads that count as connected and take `sim` instead of the real pad
    static PadIn sim[4];
    static std::string simText;     // tests (W4NX_INPUTSCRIPT): characters typed into a text entry, which the keyboard cannot send as key events
    void enterRoom(bool lan);       // joined / created a room: the setup screens in network mode, with the saved team picked for it
    void backToRoom();              // a room match is over
    void dropRoom() { online = false, screen = Network, loaded = false; }  // the connection is gone
    int minePad(int idx) const { return idx >= 0 && idx < (int)myTeams.size() ? myTeams[idx].pad : 0; }  // local pad of this console's idx-th team
    void captureState(const std::string &what);  // tests (--ui captures): selectteam, createask, teamedit1..3, slotpopup: states the scripted pads cannot click to
    void go(Screen s);              // W4M menu change: current items fly out, then s slides in
    Action frame(GameConfig &cfg, const std::vector<std::string> &maps, std::string &host, int &port, std::string &name);

private:
    int row = 0, mainRow = 0, mapSel = 0, schemeRow = 0, subRow[5] = {};  // subRow: Local, Network, MyWorms, HelpOpts, Confirm
    Screen shown = (Screen)-1, from = Title;  // W4M menus: item slide-in since `entered` (from: previous screen), smoothed highlight per item
    float entered = -100, glow[8] = {};
    Screen next = Title;  // W4M menu change: the current items fly out for LEAVE s since `leaving`, then `next` slides in
    float leaving = -1;
    float subIn(float t) const;  // submenu panel progress: 0 hidden .. 1 shown
    float titleIn(float t) const;  // s into the title -> main menu glide, run backwards when B goes back to the title
    void menu(const MenuItem *items, int n, int &sel, int dy, float t, bool live = true);  // live: animate + take input
    bool online = false, lan = false, loaded = false, music = true, layout = false;  // layout: Controls shows the pad diagram  // online: network setup (LAN or server)
    std::string *editing = nullptr;  // desktop text entry target
    Barracks barracks;               // the saved teams (teams.txt), loaded by loadSetup
    Lobby lobby;                     // the match being set up (SetupTeams / SetupMap / SetupRules): local, or a view of net->lobby
    std::vector<GameConfig::Team> myTeams;  // network: this console's teams, with their local pads; sent as TEAMS
    bool teamsDirty = false;         // myTeams changed, not sent yet
    float teamsSent = 0;
    bool room() const { return online && net && net->roomId; }
    void fromNet(const std::vector<std::string> &maps);  // lobby <- net->lobby + myTeams
    void toNet();                    // lobby -> myTeams / net->lobby (host)
    TeamCursors cursors;             // SetupTeams: one token per pad
    Vector2 tokenAt[4] = {};         // where each token is drawn (it glides to its slot)
    int rulesRow = 0, editPad = 0;   // SetupRules row; the pad that is typing
    RenderTexture2D wormRt[Lobby::MAX_TEAMS] = {};  // each slot's worm preview, drawn before the slots' 2D pass
    bool live = true;                // this frame takes input (the menu is not flying out, no text entry)
    PadIn padIn(int p);        // pad p's presses this frame (a scripted pad replaces them)
    // W4M popups (docs/w4m/frontend.md "Saved teams"): one at a time, modal; the Teams screen's belong to a pad, the others to any pad
    struct Pop {
        enum Kind { None, Teams, Ask, Cpu, Name, Slot, Confirm, Pick } kind = None;
        enum Use { Insert, Mission, Online, Edit, Delete, Create, Soundbank, Head, Eyes, Face, Hands, Weapon, Flag, Grave, Quit, NoField } use = Insert;
        int pad = -1, row = 0, slot = 0, arg = 0, skill = 0, top = 0;  // top: Pick's first visible row
        std::vector<int> ids;            // Teams: barracks indexes, -1 the Create Team row
        std::vector<std::string> vals;   // Pick: the ids of its rows
        std::string name;                // Name: what is typed
        bool typing = false, err = false;
        float t0 = 0;
    } pop;
    struct Editor {  // WXFE.TeamOptions: the working copy of a saved team
        int index = -1, page = 0, row = 0, placeSlot = -1, placePad = 0;
        SavedTeam t;
        Screen ret = MyWorms;
        Pop::Use after = Pop::Edit;      // after a create: Insert places the team, Mission / Online reopen their list
        bool changed = false;            // W4M TM.Accept: Accept enabled, B asks first
        std::vector<std::string> unlocks;
        float pageT = -100;              // the page's AddItem (0x7398ee): its In_ScaleY and In_Next start 200 ms later
        bool pageSfx = false;
        std::string anim = "Base";       // WXFE_WormDesc: WXD.WormAnim or one of its RandomAnims, from animT
        float animT = 0, idleUntil = 0;
    } ed;
    void pickDraw();
    static int pickRows(Pop::Use u);
    void editorWorm(const char *clip, float ct, bool loop, float slide);
    Action pending = None;
    std::string onlineTeam;          // the saved team this console brings to a room
    void teamSlot(Rectangle d, int k, float t);
    void openTeams(Pop::Use use, int pad, int slot, int arg = 0);
    int reopen = -1, reopenArg = 0;  // a Pop::Use whose list opens again when the editor's screen change is done (after Create Team)
    void setPop(Pop p);
    void closePop();
    void popInput(PadIn in, float t);
    void popDraw(float t);
    void placeTeam(int slot, int pad, int index);
    void releaseSlot(int s);
    void startCreate(Pop::Use use);
    void openEditor(int index, Screen ret, int placeSlot, int placePad, Pop::Use after);
    void editor(PadIn in, float t);
    void leaveEditor(bool accepted);
    void pickList(Pop::Use use);
    std::string *fieldRef(Pop::Use u);
    std::string getField(Pop::Use u);
    void setField(Pop::Use u, const std::string &v);
    std::vector<std::string> fieldValues(Pop::Use u);
    std::string fieldLabel(Pop::Use u, const std::string &id) const;
    bool placed(const std::string &name) const;
    bool setupTeams(bool plus, float t);  // true: leave the room
    void setupMap(int dx, int dy, bool back, bool plus, const std::vector<std::string> &maps);
    bool setupRules(int dx, int dy, bool ok, bool back, bool plus);  // true: START
    void wormPreview(int k, const GameConfig::Team &tm, float t);
    bool slotTake() const;  // the slot popup offers "Control with this pad": the team is a human one another pad plays
    void loadTeams();
    void loadSetup(GameConfig &cfg, const std::vector<std::string> &maps);
    void saveSetup(const GameConfig &cfg) const;
    bool edit(std::string &s, const char *hint);  // true when s changed (Switch: swkbd, desktop: starts inline entry)
    // Wormpot slot machine (3 reels) and Weapon Factory (custom_weapons.json, sent to the room in Start)
    int reel = 0, spinTo[3] = {}, facSel = 0, facRow = 0;
    float spinEnd[3] = {};
    std::vector<WeaponDef> customs;
    void wormpot(GameConfig &cfg, int dx, int dy, bool ok, bool back, float t);
    void factory(int dx, int dy, bool ok, bool back);
    void factoryEdit(int dx, int dy, bool ok, bool back, bool typing, float t);
};

// In-game HUD and W4M weapon panel (X / Q). A pick goes out as Input::pick() until game.held() shows it (a frame may run no tick).
// First-person aim reticle at screen point c, per weapon like W4M (the Sniper Rifle: its scope).
void reticle(const WeaponDef &wd, Vector2 c);
// W4M Blimp-view reticle at the screen centre: Bomber cursor (arrows = the run, left to right) or Targeting cursor.
void targetCursor(const WeaponDef &wd, int state, const Vector2 *lock = nullptr, const Vector2 *at = nullptr);  // state: 0 valid, 1 water, 2 no target, -1 none; lock: homing marker; at: centre, default screen centre
Vector2 windPointer(bool live, float wx, float wz, Vector2 fwd);  // wind meter needle on screen (y down), length foreshortened
float text3dPlace(Vector3 p, float scale, const Camera3D &cam, Vector3 fwd, Vector2 screen, Vector2 &at);  // Text3D em (px) and centre
// W4M PiP (HUDTWK PiP.*, WXFE_Border_Bubble): the event camera's picture in its tilted inset; show / full as Controls::inset
void pipInset(const RenderTexture2D &scene, float show, float full);
struct Hud {
    bool skipHp = false;  // X pressed during the count (user-requested)
    int counting = -1;  // worm whose label is counting, camera on it; wait: camera travel / linger seconds
    bool reopen = false;  // X skipped the Settle count: the next local Aim opens the panel
    bool forceX = false;  // tests: X pressed
    bool readyScreen(const Game &g, bool cinematic) const;
    bool open = false, mine = false;  // mine: a human here plays the current turn
    bool quiet = false;               // no bottom hints this frame
    bool fp = false;                  // first-person aim, or just left it: no label on the current worm
    float pipShow = 0, pipFull = 0;   // Controls::inset: labels stay off the PiP, ActWormInfo moves to its PosPiP
    // local: a human here plays the current turn; tick: ticks simulated so far
    void input(const Game &g, Input &in, bool local, int pad, uint32_t tick);
    void labels(const Game &g, const Camera3D &cam, uint32_t tick);  // W4M bin 3DText (29): Text3Ds, under the reticles and the HUD; first each frame
    void draw(const Game &g, const Camera3D &cam, uint32_t tick);
    void select(int weapon) { pick = weapon, open = false, swallow = true; }

private:
    int cursor = 0, pick = -1;  // pick: sent until it lands
    bool swallow = true;  // a held A (weapon pick, menu START) must not fire
    int introWorm = -1;       // turn-start name banner (CPU/remote): current worm and the tick it became current
    uint32_t introStart = 0;
    // W4M hp count: labels/bars show `shown`, which ticks toward the sim's counted hp (Settle: timed by the sim)
    struct HpTrack { int seen = 0; float shown = 0, from = 0; bool poison = false; };
    struct Popup { int worm, amount; float age, punch; bool poison, live; };  // big damage counter; age runs once its count ends
    std::vector<HpTrack> hpt;
    std::vector<Popup> popups;
    std::vector<int> order;  // worms with a count to play, in first-hit order
    float wait = 0, tickGap = 0;
    uint32_t hpTick = 0;
    int hpClock = 0;
    bool readyNow = false;  // labels(): this frame's ready screen
    bool trackHp(const Game &g, bool turnStart, uint32_t tick);  // true while the camera is on a worm or a crate
};
void hudEvent(const Game &g, const GameEvent &e);  // per sim event: W4M commentary banners (deaths, crates)
// audio hook: a subtitle Miss.Generic.Lose<n> line shows (SubtitleGraphicEntity 0x5f90a0 plays EFMV/Failures/Failures_Narrator_0<n>)
extern void (*onNarrator)(int n);
// + menu in a match. Local play stops stepping the sim while open; online it is only an overlay.
struct Pause {
    // W4M WXFEP.PauseList rows (TablePopulationService 0x4cd3a0), in its order; Seed needs a generated landscape, which we lack
    enum Row { Continue, Options, Draw, Restart, Briefing, Quit };
    enum Action { None, DoQuit, DoRestart, DoDraw };
    bool open = false, help = false;  // open: the match is paused (the list, a confirm popup, options or the briefing)
    int row = 0;
    bool canDraw = false, canRestart = false;  // GameOver.GameType Multiplayer / Story, Challenge, Deathmatch
    const MissionSpec *story = nullptr;  // a Story mission: the Briefing row opens W4M WXFEP.MissionBriefing
    int confirm = -1, yes = 0;           // WXFEP.ConfirmDraw / Restart / Quit (a Row), its highlighted answer (0 No, 1 Yes)
    float brief = -1, shown = -1;        // when the briefing / the current popup opened (now() s), -1 none
    Action update();
    void draw() const;

private:
    float last = 0, veil = 0;  // previous update's clock; FullScreenColour alpha
    std::vector<int> rows() const;
    void popUp(int which);
    void briefing() const;
};
// Replays list (inside Begin/EndDrawing): picked file index, -1 none yet, -2 back. Y toggles `instant` (instant replay).
int replayList(const std::vector<std::string> &files, int &sel, bool &instant);
void playbackBar(bool paused, int speed, bool freeCam, float sec, float total, const char *note);  // match playback overlay
void replayBadge();  // instant replay overlay
// Network salon (inside Begin/EndDrawing): Créer / Rejoindre, then the LAN games heard or the server's rooms. 1 create, 2 join entry sel, -1 back, 0 nothing yet.
struct Salon { int row = 0, sel = 0, page = -1; bool joining = false; float t0 = 0, t1 = 0; };  // page: the entries shown (-1 none yet), t0 / t1: when the screen / entries came in
int salon(Salon &st, bool lan, const std::vector<std::string> &names, const std::vector<std::string> &info, const std::string &status);
// Single player (inside Begin/EndDrawing): Missions / Challenges list, then the briefing. Mission index to start, -1 none yet, -2 back.
// shown: when the list / briefing appeared (-1: opening now); leaving: when its items started out; to: what follows them
struct MissionMenu { int tab = 0, sel[2] = {}; float shown = -1, leaving = -1; int to = -1; std::string team; };  // team: User.HumanTeamName
int missionMenu(MissionMenu &st, const std::vector<MissionSpec> &list, const Progress &p);
// Mission over (drawn over the match): 0 nothing yet, 1 next mission, 2 retry, 3 back to the list
int missionEnd(const Game &g, const MissionSpec &m, const Progress::Entry &best, bool hasNext);
// WXFE.MoviePlayer (inside Begin/EndDrawing): t s since the movie started; false once it ended or was skipped
bool movieFrame(float t);
void feCursor();  // W4M MouseEntity pointer, once per frame inside BeginDrawing on a front-end screen
bool eggFound(const char *nameId, int coins);  // W4M WXFE.EasterEggFound popup; true once dismissed
}  // namespace Ui
