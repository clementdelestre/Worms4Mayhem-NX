#pragma once
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
// HUD state a script sets (HUDTWK keys, CommentService flag); clock: RoundTime > 0 its remaining ms, 0 the elapsed ms, -1 none (infinity)
struct ScriptHud { bool counter = false, percent = false, tenths = false, roundClock = true, defaults = true, endless = false; int value = 0, roundTime = 0; int64_t clockMs = 0; };
ScriptHud scriptHud(const Game &g);
double scriptNum(const Game &g, const char *key, double def);  // a data key's value, def when the store lacks it
bool scriptOutro(const Game &g);  // EFMV.GameOverMovie (or .Off) set: the end skips GameOverLogicEntity (0x4fd27a)
