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
    std::string objectives;                  // W4M Objectives: "<key>.A/.B/.C" are the in-game briefing lines
    int order = 0, par = 0;  // par: W4M BonusTime (s), 0 = none
    int record = 0;          // DEFSAVE HighScoreData WinnerTime (s) of Level_Number, shown until a better time is set
    std::string recordBy;    // its WinnerName
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

// The profile's progress.txt: "unlock <item>" per unlocked W4M WXFE_UnlockableItem, "intro", "welcome" (W4M Lock.* and movie flags are the profile's); each team's
// mission entries "<mission id> <completed 0/1> <best ticks>" in progress_<team>.txt (W4M StoredTeamData StoryMissionsCompleted: Story progress is per team)
struct Progress {
    struct Entry { bool done = false; int best = 0; };
    std::vector<std::pair<std::string, Entry>> entries;
    std::vector<std::string> unlocks;
    bool storyIntro = false;  // the Story intro movie has played
    bool welcome = false;     // the Welcome movie has played (W4M profile +0x88)
    bool storyDone(const std::vector<MissionSpec> &list) const;  // a story mission is completed
    Entry get(const std::string &id) const;
    void record(const std::string &id, bool done, int ticks);
    bool unlocked(const std::vector<MissionSpec> &list, size_t i) const;  // missions: previous one of the campaign done
    void open(const char *profile, const char *team = nullptr);  // loads the profile file and the team's entries (null: no team)
    void save() const;                                          // writes the files `open` read
    static std::string teamFile(const char *dir, const std::string &team);  // <dir>progress_<team>.txt, unsafe name bytes as %XX
    static void migrate(const char *profile, const char *team);  // the entries of an older single progress.txt become `team`'s file (if absent)

private:
    std::string profilePath, teamPath;
};
