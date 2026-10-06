#pragma once
#include "json.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// W4M mission scripts (docs/missions.md "Scripts"): stdlib, lib_help and the level's chunk run as the original Lua 5.0 bytecode,
// their 14 C functions on a data store and containers loaded from assets/scripts (tools/w4m-maps), messages dispatched to the sim.
struct Game;
struct ScriptState;
bool scriptStart(Game &g, const std::string &dir, const std::string &script, const std::string &bank);  // Initialise; false: not loaded
void scriptStep(Game &g);                    // end of a tick: worm / crate events, the round clock, timers, queued callbacks
void scriptEvent(Game &g, const char *fn);   // the turn clock's Timer_* callbacks, run at once
void scriptNoActivity(Game &g);              // nothing active in Settle: stdlib GameLogic_NoActivity, if EndTurn / CheckActivity waits for it
uint32_t scriptChecksum(const Game &g);      // the script globals, data store, containers and timers
int scriptPostActivity(const Game &g);       // PostActivityTime, ticks
// What a run did: messages and Critical movie events not modelled yet (name, count), data keys the store lacks, Lua errors
struct ScriptReport { std::vector<std::pair<std::string, int>> ignored, missingKeys; int errors = 0; std::string lastError; };
ScriptReport scriptReport(const Game &g);
bool scriptDefines(const Game &g, const char *fn);  // the script has this global function (a callback it answers)
const char *scriptCrateGraphic(const Game &g, int index);  // a custom crate's Crate.CustomGraphic (a detail mesh name), null: none
// HUD state a script sets (HUDTWK keys, CommentService flag); clock: RoundTime > 0 its remaining ms, 0 the elapsed ms, -1 none (infinity);
// elapsedMs: ElapsedRoundTime
struct ScriptHud { bool counter = false, percent = false, tenths = false, roundClock = true, defaults = true, endless = false; int value = 0, roundTime = 0; int64_t clockMs = 0, elapsedMs = 0; };
ScriptHud scriptHud(const Game &g);
double scriptNum(const Game &g, const char *key, double def);  // a data key's value, def when the store lacks it
// The end (0x4fd27a): -1 GameOverLogicEntity's pace; 0 the EFMV.GameOverMovie plays; 1 the result now (that movie ended, or .Off)
int scriptOutro(const Game &g);
extern std::vector<std::string> scriptUnlocks;  // WXFE_UnlockableItems unlocked in the save (progress.txt), applied at scriptStart
struct ScriptEgg { std::string item, name; int coins = 0; };  // name: its DescriptionName text id; coins: its Value
ScriptEgg scriptEgg(const Game &g);  // the item WXMsg.EasterEggFound unlocked this game, item "" none
// Level movies (docs/missions.md "Movies"): the player runs in the sim; the client draws its camera, borders, subtitles and actors
void scriptSkipMovie(Game &g);       // Input::SKIP_MOVIE (W4M Input.QuitEFMV): the Critical events only, then the end
bool scriptMovieOn(const Game &g);   // EFMV.Active
bool scriptMovieCamera(const Game &g);  // a movie camera is the logical one (type 14): the turn and round clocks stand (0x50f15f)
struct MovieView {
    bool on = false;
    std::string name;
    float ms = 0;                     // the movie clock (ms) at this tick
    const Json *tracks = nullptr;     // [track][event] = [type, Time, Critical, fields...]
    std::vector<int> worm;            // per track: our worm cast (WORM<slot>), -1 none
    std::vector<std::string> actor;   // per track: its CastActor name
    int camT = -1, camE = -1, camAt = 0;  // the movie camera event and its start (ms); -1: none yet
    // its KnotLists after the last 10 ms update: position / look-at current knot, parameter, running; steps as in script.cpp
    int kpCur = 0, klCur = 0;
    float kpT = 0, klT = 0;
    bool kpOn = false, klOn = false, timed = false;
    std::vector<int> steps;
    bool borders = false, subtitles = false;  // EfmvBorderEntity up; subtitle mode (EFMV.Subtitles.On .. Off)
    float bordersOut = 0;             // 0..1 of the bars' slide off (EFMV.BorderOffTime), 0 while on
};
MovieView scriptMovie(const Game &g);
const Json *scriptMovieEvent(const Game &g, const char *movie, int code);  // a GameEvent::Movie's event (code = track << 16 | index)
