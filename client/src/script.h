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
