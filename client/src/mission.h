#pragma once
#include "sim.h"
#include <string>
#include <vector>

// Single-player missions and challenges: JSON files (docs/missions.md) in assets/missions, imported from W4M by tools/w4m-maps.
// Each names its Lua script and databank (script.h), which set up and drive the level. Played through the sim, so deterministic.
struct MissionSpec {
    std::string id, name, kind, campaign, map, preview, brief, success, failure;
    std::string level;                       // W4M WXFE_LevelDetails name ("Story.DinerMight")
    std::string nameId, briefId, successId;  // W4M text keys (assets/lang): Frontend_Name, Frontend_Briefing, the complete body
    int order = 0, par = 0;  // par: W4M BonusTime (s), 0 = none
    std::string script, bank, scriptDir;  // W4M: <scriptDir><script>.lub and <bank>.json (assets/scripts)
    Scheme scheme;
    float rainProb = -1;  // >= 0: Particle.Rain.Prob set by the mission script (weather odds, render only)
};

bool loadMission(const std::string &path, MissionSpec &out);
// All missions whose map is available, sorted by kind, campaign, order.
std::vector<MissionSpec> listMissions(const char *romfsDir, const char *dataDir);
GameConfig missionConfig(const MissionSpec &m, uint32_t seed);  // cfg.mission points at m: keep m alive during the match
void placeMapObjects(Game &g);           // W4M GameLogic.PlaceObjects: mines and oil drums on the map's markers
void missionEnd(Game &g, bool won);      // result, GameOver

// progress.txt: "<mission id> <completed 0/1> <best ticks>" per line, "unlock <item>" per unlocked W4M WXFE_UnlockableItem
struct Progress {
    struct Entry { bool done = false; int best = 0; };
    std::vector<std::pair<std::string, Entry>> entries;
    std::vector<std::string> unlocks;
    Entry get(const std::string &id) const;
    void record(const std::string &id, bool done, int ticks);
    bool unlocked(const std::vector<MissionSpec> &list, size_t i) const;  // missions: previous one of the campaign done
    void load(const char *path);
    void save(const char *path) const;
};
