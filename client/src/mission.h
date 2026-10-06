#pragma once
#include "sim.h"
#include <string>
#include <vector>

// Single-player missions and challenges: JSON files (docs/missions.md) from romfs:/missions (ours) and
// assets/missions (imported from W4M by tools/w4m-maps). Team 0 is the player. Played through the sim, so deterministic.
// A W4M mission names its Lua script and databank (script.h); the JSON teams, objects and objectives serve the others.
struct MissionSpec {
    std::string id, name, kind, campaign, map, preview, brief, success, failure;
    int order = 0, par = 0;  // par: W4M target time (s), 0 = none
    std::string script, bank, scriptDir;  // W4M: <scriptDir><script>.lub and <bank>.json (assets/scripts)
    Scheme scheme;
    bool endless = false;    // turn_time 0: the turn never runs out (challenges)
    bool sequence = false;   // targets / crates appear one at a time, in file order
    bool placeObjects = false;  // also the map's mine / oil drum markers
    float rainProb = -1;  // >= 0: Particle.Rain.Prob set by the mission script (weather odds, render only)
    struct Place { std::string marker; Vector3 pos{}; bool set = false; };  // marker name (map JSON "markers") or position
    struct WormSpec { std::string name; int hp = 100; Place at; };
    struct TeamSpec { std::string name; uint8_t cpu = 0; bool idle = false, weaponsSet = false; std::vector<std::pair<std::string, int>> weapons; std::vector<WormSpec> worms; };
    std::vector<TeamSpec> teams;
    struct ObjectSpec { Object::Type type = Object::Crate; Place at; std::string weapon; bool drop = false; };  // drop: onto the ground below
    std::vector<ObjectSpec> objects;
    // Win: every objective met. Lose: player team wiped out or any fail condition.
    struct Goal {
        enum Type { KillAll, Kill, Reach, Collect, Destroy, PoisonAll, Survive, Hurt, Time, Turns, WormDies, Unknown } type = Unknown;
        int team = 1, worm = 0, count = 1, seconds = 0, turns = 0;
        Place at;
        float radius = 2;
    };
    std::vector<Goal> objectives, fail;
};

bool loadMission(const std::string &path, MissionSpec &out);
// All missions whose map is available, sorted by kind, campaign, order.
std::vector<MissionSpec> listMissions(const char *romfsDir, const char *dataDir);
GameConfig missionConfig(const MissionSpec &m, uint32_t seed);  // cfg.mission points at m: keep m alive during the match
std::string goalText(const MissionSpec &m, const MissionSpec::Goal &g, const Game *game);  // "Destroy the targets 3/10"
Vector3 placeOf(const Game &g, const MissionSpec::Place &p);  // marker / position resolved on the loaded map
void spawnObject(Game &g, Object::Type t, Vector3 pos, int weapon, bool drop, int tag);  // a mission crate, target, mine or drum
void placeMapObjects(Game &g);           // W4M GameLogic.PlaceObjects: mines and oil drums on the map's markers
void missionEnd(Game &g, bool won);      // result, GameOver

// progress.txt: "<mission id> <completed 0/1> <best ticks>" per line
struct Progress {
    struct Entry { bool done = false; int best = 0; };
    std::vector<std::pair<std::string, Entry>> entries;
    Entry get(const std::string &id) const;
    void record(const std::string &id, bool done, int ticks);
    bool unlocked(const std::vector<MissionSpec> &list, size_t i) const;  // missions: previous one of the campaign done
    void load(const char *path);
    void save(const char *path) const;
};
