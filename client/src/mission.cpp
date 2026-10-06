#include "mission.h"
#include "json.h"
#include "script.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <tuple>

bool loadMission(const std::string &path, MissionSpec &m) {
    char *txt = LoadFileText(path.c_str());
    Json j;
    bool ok = txt && Json::parse(txt, j) && j.type == Json::Obj && j["script"].type == Json::Str;
    UnloadFileText(txt);
    if (!ok) { TraceLog(LOG_WARNING, "mission %s: invalid", path.c_str()); return false; }
    m = MissionSpec{};
    m.id = GetFileNameWithoutExt(path.c_str());
    m.name = j["name"].s(m.id), m.kind = j["kind"].s("mission"), m.campaign = j["campaign"].s(), m.map = j["map"].s();
    m.preview = j["preview"].s(), m.brief = j["brief"].s(), m.success = j["success"].s(), m.failure = j["failure"].s();
    m.level = j["level"].s(), m.nameId = j["name_id"].s(), m.briefId = j["brief_id"].s(), m.successId = j["success_id"].s();
    m.order = (int)j["order"].f(0), m.par = (int)j["par"].f(0);
    m.script = j["script"].s(), m.bank = j["bank"].s(), m.scriptDir = path.substr(0, path.rfind('/')) + "/../scripts/";  // raylib GetDirectoryPath prefixes "./" to sdmc:/ paths
    for (const SchemePreset &p : SCHEMES) if (j["scheme"].s("Standard") == p.name) m.scheme = p.s;
    Scheme &s = m.scheme;
    s.mines = (uint8_t)j["mines"].f(0), s.barrels = (uint8_t)j["barrels"].f(0), s.crateChance = (uint8_t)j["crate_chance"].f(s.crateChance);
    s.turnTime = (uint8_t)Clamp(j["turn_time"].f(s.turnTime), 1, 255);
    s.retreatTime = (uint8_t)j["retreat_time"].f(s.retreatTime), s.hotSeat = (uint8_t)j["hot_seat"].f(s.hotSeat);
    s.wind = (uint8_t)Clamp(j["wind"].f(s.wind), 0, 3), s.fallDamage = (uint8_t)j["fall_damage"].f(s.fallDamage);
    s.roundTime = (uint8_t)Clamp(j["round_time"].f(60), 1, 255);
    m.rainProb = j["rain_prob"].f(-1);
    return true;
}

std::vector<MissionSpec> listMissions(const char *romfsDir, const char *dataDir) {
    std::vector<MissionSpec> out;
    if (std::string dir = std::string(dataDir) + "assets/missions"; DirectoryExists(dir.c_str())) {
        FilePathList files = LoadDirectoryFilesEx(dir.c_str(), ".json", false);
        for (unsigned i = 0; i < files.count; i++) {
            MissionSpec m;
            if (!loadMission(files.paths[i], m)) continue;
            bool map = m.map.empty() || FileExists(TextFormat("%sassets/maps/%s.json", dataDir, m.map.c_str())) || FileExists(TextFormat("%smaps/%s.json", romfsDir, m.map.c_str()));
            if (map && std::none_of(out.begin(), out.end(), [&](const MissionSpec &o) { return o.id == m.id; })) out.push_back(m);
        }
        UnloadDirectoryFiles(files);
    }
    std::sort(out.begin(), out.end(), [](const MissionSpec &a, const MissionSpec &b) {
        auto key = [](const MissionSpec &m) { return std::make_tuple(m.kind != "mission", m.campaign, m.order, m.name); };
        return key(a) < key(b);
    });
    return out;
}

GameConfig missionConfig(const MissionSpec &m, uint32_t seed) {
    GameConfig c;
    c.seed = seed, c.map = m.map, c.scheme = m.scheme, c.mission = &m;
    c.teams = 0, c.wormsPerTeam = 1;  // the script builds the teams (WormManager.Reinitialise)
    return c;
}

void placeMapObjects(Game &g) {  // 0x4fb490: detail objects named exactly "mine" (a CreateMine), "oildrum", "minefactory"; "Mine1" is a PlaceMine spot
    for (const Terrain::Marker &k : g.terrain.markers)
        if (k.name == "mine") g.objects.push_back(g.newMine(k.pos));
        else if (k.name == "oildrum") g.objects.push_back({Object::Barrel, k.pos, {0, 0, 0}, -1, -1, false, false});
        else if (k.name == "minefactory") g.factoryCreate(k.pos);  // 0x4f64f0
}

void missionStart(Game &g) {
    const MissionSpec &m = *g.cfg.mission;
    g.phase = Phase::Settle;
    if (!scriptStart(g, m.scriptDir, m.script, m.bank)) TraceLog(LOG_WARNING, "mission %s: script not started", m.id.c_str()), missionEnd(g, false);
}

static bool teamDead(const Game &g, int t) {
    for (const Worm &w : g.worms) if (w.team == t && w.alive) return false;
    return true;
}

void missionStep(Game &g) {
    if (g.phase != Phase::GameOver) g.run.ticks++;  // not under the game-over movie
    if (g.script) scriptStep(g);
}

void missionEnd(Game &g, bool won) {
    if (g.phase == Phase::GameOver) return;
    g.run.result = won ? 1 : -1;
    g.phase = Phase::GameOver;
    g.winner = -1;
    for (int t = 0; t < g.teams && g.winner < 0; t++) if (!teamDead(g, t) && (t == 0) == won) g.winner = t;
    g.events.push_back({GameEvent::GameOver, g.worms.empty() ? Vector3{} : g.worms[g.current].pos, -1, -1});
}

Progress::Entry Progress::get(const std::string &id) const {
    for (const auto &e : entries) if (e.first == id) return e.second;
    return {};
}

void Progress::record(const std::string &id, bool done, int ticks) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const auto &e) { return e.first == id; });
    if (it == entries.end()) entries.push_back({id, {}}), it = entries.end() - 1;
    if (done && (!it->second.done || ticks < it->second.best)) it->second.best = ticks;
    it->second.done = it->second.done || done;
}

bool Progress::unlocked(const std::vector<MissionSpec> &list, size_t i) const {
    if (list[i].kind != "mission") return true;
    for (size_t k = i; k-- > 0;)
        if (list[k].kind == "mission" && list[k].campaign == list[i].campaign) return get(list[k].id).done;
    return true;
}

void Progress::load(const char *path) {
    entries.clear(), unlocks.clear();
    char *txt = LoadFileText(path);
    if (!txt) return;
    for (char *line = strtok(txt, "\n"); line; line = strtok(nullptr, "\n")) {
        char id[128];
        int done = 0, best = 0;
        if (sscanf(line, "%127s %d %d", id, &done, &best) == 3) entries.push_back({id, {done != 0, best}});
        else if (sscanf(line, "unlock %127s", id) == 1) unlocks.push_back(id);
    }
    UnloadFileText(txt);
}

void Progress::save(const char *path) const {
    std::string s;
    for (const auto &e : entries) s += TextFormat("%s %d %d\n", e.first.c_str(), e.second.done, e.second.best);
    for (const std::string &u : unlocks) s += "unlock " + u + "\n";
    SaveFileText(path, s.data());
}
