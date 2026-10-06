#include "mission.h"
#include "json.h"
#include "script.h"
#include "raymath.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <tuple>

static const char *GOALS[] = {"kill_all", "kill", "reach", "collect", "destroy", "poison_all", "survive", "hurt", "time", "turns", "worm_dies"};

static MissionSpec::Place place(const Json &j) {
    MissionSpec::Place p;
    if (j.type == Json::Str) p.marker = j.s();
    else if (j.type == Json::Arr) p.pos = {j[0].f(), j[1].f(), j[2].f()}, p.set = true;
    return p;
}

static void goals(const Json &list, std::vector<MissionSpec::Goal> &out) {
    for (const Json &j : list.arr) {
        MissionSpec::Goal g;
        for (int k = 0; k < (int)(sizeof GOALS / sizeof *GOALS); k++) if (j["type"].s() == GOALS[k]) g.type = (MissionSpec::Goal::Type)k;
        if (g.type == MissionSpec::Goal::Unknown) { TraceLog(LOG_WARNING, "mission: unknown objective '%s'", j["type"].s().c_str()); continue; }
        g.team = (int)j["team"].f(1), g.worm = (int)j["worm"].f(0), g.count = (int)j["count"].f(1);
        g.seconds = (int)j["seconds"].f(0), g.turns = (int)j["turns"].f(0), g.radius = j["radius"].f(2), g.at = place(j["pos"]);
        if ((g.type == MissionSpec::Goal::Time && g.seconds <= 0) || (g.type == MissionSpec::Goal::Turns && g.turns <= 0)) continue;
        out.push_back(g);
    }
}

bool loadMission(const std::string &path, MissionSpec &m) {
    char *txt = LoadFileText(path.c_str());
    Json j;
    bool ok = txt && Json::parse(txt, j) && j.type == Json::Obj && (j["teams"].size() > 0 || j["script"].type == Json::Str);
    UnloadFileText(txt);
    if (!ok) { TraceLog(LOG_WARNING, "mission %s: invalid", path.c_str()); return false; }
    m = MissionSpec{};
    m.id = GetFileNameWithoutExt(path.c_str());
    m.name = j["name"].s(m.id), m.kind = j["kind"].s("mission"), m.campaign = j["campaign"].s("Worms4NX"), m.map = j["map"].s();
    m.preview = j["preview"].s(), m.brief = j["brief"].s(), m.success = j["success"].s(), m.failure = j["failure"].s();
    m.order = (int)j["order"].f(0), m.par = (int)j["par"].f(0);
    m.script = j["script"].s(), m.bank = j["bank"].s(), m.scriptDir = path.substr(0, path.rfind('/')) + "/../scripts/";  // raylib GetDirectoryPath prefixes "./" to sdmc:/ paths
    for (const SchemePreset &p : SCHEMES) if (j["scheme"].s("Standard") == p.name) m.scheme = p.s;
    Scheme &s = m.scheme;
    s.mines = (uint8_t)j["mines"].f(0), s.barrels = (uint8_t)j["barrels"].f(0), s.crateChance = (uint8_t)j["crate_chance"].f(s.crateChance);
    int turn = (int)j["turn_time"].f(s.turnTime);
    m.endless = turn <= 0, s.turnTime = (uint8_t)Clamp(turn, 1, 255);
    s.retreatTime = (uint8_t)j["retreat_time"].f(s.retreatTime), s.hotSeat = (uint8_t)j["hot_seat"].f(s.hotSeat);
    s.wind = (uint8_t)Clamp(j["wind"].f(s.wind), 0, 3), s.fallDamage = (uint8_t)j["fall_damage"].f(s.fallDamage);
    s.roundTime = (uint8_t)Clamp(j["round_time"].f(60), 1, 255);
    m.sequence = j["sequence"].is(), m.placeObjects = j["place_objects"].is(), m.rainProb = j["rain_prob"].f(-1);
    for (const Json &t : j["teams"].arr) {
        MissionSpec::TeamSpec ts;
        ts.name = t["name"].s(), ts.cpu = (uint8_t)Clamp(t["cpu"].f(0), 0, 5), ts.idle = t["idle"].is();
        ts.weaponsSet = t["weapons"].type == Json::Obj;
        for (auto &kv : t["weapons"].obj) ts.weapons.push_back({kv.first, (int)kv.second.f(0)});
        for (const Json &w : t["worms"].arr) ts.worms.push_back({w["name"].s(), (int)Clamp(w["hp"].f(s.health), 1, 999), place(w["pos"])});
        if (!ts.worms.empty()) m.teams.push_back(ts);
    }
    for (const Json &o : j["objects"].arr) {
        MissionSpec::ObjectSpec os;
        std::string t = o["type"].s("crate");
        os.type = t == "target" ? Object::Target : t == "mine" ? Object::Mine : t == "barrel" || t == "oildrum" ? Object::Barrel : Object::Crate;
        os.at = place(o["pos"]), os.weapon = o["weapon"].s("health"), os.drop = o["drop"].is(os.at.set);
        m.objects.push_back(os);
    }
    goals(j["objectives"], m.objectives);
    goals(j["fail"], m.fail);
    if (m.script.empty() && (m.teams.empty() || m.teams.size() > 4 || m.objectives.empty())) { TraceLog(LOG_WARNING, "mission %s: needs 1-4 teams and an objective", path.c_str()); return false; }
    return true;
}

std::vector<MissionSpec> listMissions(const char *romfsDir, const char *dataDir) {
    std::vector<MissionSpec> out;
    for (std::string dir : {std::string(romfsDir) + "missions", std::string(dataDir) + "assets/missions"}) {
        if (!DirectoryExists(dir.c_str())) continue;
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
        auto key = [](const MissionSpec &m) { return std::make_tuple(m.kind != "mission", m.campaign != "Worms4NX", m.campaign, m.order, m.name); };
        return key(a) < key(b);
    });
    return out;
}

GameConfig missionConfig(const MissionSpec &m, uint32_t seed) {
    GameConfig c;
    c.seed = seed, c.map = m.map, c.scheme = m.scheme, c.mission = &m;
    c.teams = (int)m.teams.size(), c.wormsPerTeam = 1;
    for (const auto &t : m.teams) {
        c.wormsPerTeam = std::max(c.wormsPerTeam, (int)t.worms.size());
        c.teamSetup.push_back({t.name, t.cpu, (uint8_t)c.teamSetup.size(), 0});
    }
    return c;
}

Vector3 placeOf(const Game &g, const MissionSpec::Place &p) {
    if (p.set) return p.pos;
    for (const Terrain::Marker &k : g.terrain.markers) if (k.name == p.marker) return k.pos;
    return {Terrain::NX * Terrain::VOX / 2, (Terrain::NY - 2) * Terrain::VOX, Terrain::NZ * Terrain::VOX / 2};
}

static int weaponIndex(const std::string &name) {
    for (size_t i = 0; i < WEAPONS.size(); i++) if (!strcasecmp(WEAPONS[i].name.c_str(), name.c_str())) return (int)i;
    return -1;
}

void spawnObject(Game &g, Object::Type t, Vector3 pos, int weapon, bool drop, int tag) {
    Object o = {t, pos, {0, 0, 0}, t == Object::Crate ? weapon : -1, -1, false, false};
    o.tag = tag;
    if (tag >= 0) o.pinned = true, o.teamDestroy = t == Object::Crate ? 5 : -1, o.teamCollect = t == Object::Target ? 5 : -1, o.hp = 0;  // ours: crates stay, a hit pops a target
    Vector3 hit;
    if (drop && g.terrain.raycast({Vector3Add(o.pos, {0, 0.8f, 0}), {0, -1, 0}}, 60, &hit)) o.pos = hit;
    o.pos.y += t == Object::Target ? (drop ? 1.5f : 0) : t == Object::Crate ? 0.45f : 0.3f;  // markers sit on the ground
    g.objects.push_back(o);
}

static void placeObject(Game &g, size_t i) {
    const MissionSpec::ObjectSpec &s = g.cfg.mission->objects[i];
    bool tracked = s.type == Object::Crate || s.type == Object::Target;
    spawnObject(g, s.type, placeOf(g, s.at), weaponIndex(s.weapon), s.drop, tracked ? (int)i : -1);
    g.run.state[i] = tracked ? 1 : 2;
}

void placeMapObjects(Game &g) {  // 0x4fb490: detail objects named exactly "mine" (a CreateMine), "oildrum", "minefactory"; "Mine1" is a PlaceMine spot
    for (const Terrain::Marker &k : g.terrain.markers)
        if (k.name == "mine") g.objects.push_back(g.newMine(k.pos));
        else if (k.name == "oildrum") g.objects.push_back({Object::Barrel, k.pos, {0, 0, 0}, -1, -1, false, false});
        else if (k.name == "minefactory") g.factoryCreate(k.pos);  // 0x4f64f0
}

void missionStart(Game &g) {
    const MissionSpec &m = *g.cfg.mission;
    if (!m.script.empty()) {
        g.phase = Phase::Settle;
        if (!scriptStart(g, m.scriptDir, m.script, m.bank)) TraceLog(LOG_WARNING, "mission %s: script not started", m.id.c_str()), missionEnd(g, false);
        return;
    }
    const float cx = Terrain::NX * Terrain::VOX / 2, cz = Terrain::NZ * Terrain::VOX / 2;
    for (int t = 0; t < g.teams && t < (int)m.teams.size(); t++) {
        const MissionSpec::TeamSpec &ts = m.teams[t];
        g.idle[t] = ts.idle;
        if (ts.weaponsSet) {
            std::fill(g.ammo[t].begin(), g.ammo[t].end(), 0);
            for (const auto &w : ts.weapons) {
                int k = weaponIndex(w.first);
                if (k >= 0) g.ammo[t][k] = w.second;
                else TraceLog(LOG_WARNING, "mission %s: unknown weapon '%s'", m.id.c_str(), w.first.c_str());
            }
        }
        for (int k = 0; k < g.perTeam; k++) {
            Worm &w = g.worms[t * g.perTeam + k];
            if (k >= (int)ts.worms.size()) { w.alive = false, w.hp = 0, w.pos = {cx, -50, cz}; continue; }  // unused slot: never drawn
            const MissionSpec::WormSpec &ws = ts.worms[k];
            w.hp = ws.hp;
            if (ws.at.marker.empty() && !ws.at.set) continue;  // random spawn
            Vector3 p = placeOf(g, ws.at), hit;
            if (g.terrain.raycast({Vector3Add(p, {0, 0.8f, 0}), {0, -1, 0}}, 40, &hit)) p = {hit.x, hit.y + Game::R + 0.3f, hit.z};
            w.pos = p, w.yaw = atan2f(cx - p.x, cz - p.z);
        }
    }
    g.run.state.assign(m.objects.size(), 0);
    g.run.met.assign(m.objectives.size(), 0);
    bool first = true;
    for (size_t i = 0; i < m.objects.size(); i++) {
        bool seq = m.sequence && (m.objects[i].type == Object::Crate || m.objects[i].type == Object::Target);
        if (!seq || first) placeObject(g, i);
        first = first && !seq;
    }
    if (m.placeObjects) placeMapObjects(g);
}

static bool teamDead(const Game &g, int t) {
    for (const Worm &w : g.worms) if (w.team == t && w.alive) return false;
    return true;
}

void missionStep(Game &g) {
    const MissionSpec &m = *g.cfg.mission;
    MissionRun &r = g.run;
    if (g.phase != Phase::GameOver) r.ticks++;  // not under the game-over movie
    if (g.script) return scriptStep(g);
    if (m.endless && g.phase == Phase::Aim) g.timer = std::max(g.timer, 99 * 60);
    bool hurt = false, collect = false;
    for (const GameEvent &e : g.events) {
        bool mine = e.worm >= 0 && g.worms[e.worm].team == 0;
        if (e.kind == GameEvent::TurnStart && mine) r.turns++;
        if (e.kind == GameEvent::Hurt && mine) hurt = true;
        if (e.kind == GameEvent::Collect && mine) collect = true;
    }
    // tracked objects only vanish when collected (pinned crates can't be blown up) or destroyed (targets)
    bool active = false;
    for (size_t i = 0; i < r.state.size(); i++) {
        if (r.state[i] != 1) continue;
        if (std::any_of(g.objects.begin(), g.objects.end(), [&](const Object &o) { return o.tag == (int)i; })) { active = true; continue; }
        r.state[i] = 2;
        if (m.objects[i].type == Object::Target) r.destroyed++;
        else if (collect) r.collected++;
    }
    if (m.sequence && !active)
        for (size_t i = 0; i < r.state.size(); i++)
            if (!r.state[i]) { placeObject(g, i); break; }

    auto wormDead = [&](int t, int k) { return t >= g.teams || k >= g.perTeam || !g.worms[t * g.perTeam + k].alive; };
    using G = MissionSpec::Goal;
    bool win = true;
    for (size_t i = 0; i < m.objectives.size(); i++) {
        const G &o = m.objectives[i];
        bool met = r.met[i];
        switch (o.type) {
        case G::KillAll: met = true; for (int t = 1; t < g.teams; t++) met = met && teamDead(g, t); break;
        case G::Kill: met = wormDead(o.team, o.worm); break;
        case G::Reach:
            for (const Worm &w : g.worms) met = met || (w.team == 0 && w.alive && Vector3Distance(w.pos, placeOf(g, o.at)) < o.radius);
            break;
        case G::Collect: met = r.collected >= o.count; break;
        case G::Destroy: met = r.destroyed >= o.count; break;
        case G::PoisonAll: met = true; for (const Worm &w : g.worms) met = met && (w.team == 0 || !w.alive || w.poison > 0); break;
        case G::Survive: met = o.seconds ? r.ticks >= o.seconds * 60 : r.turns > o.turns; break;
        default: break;
        }
        r.met[i] = met;
        win = win && met;
    }
    bool lose = teamDead(g, 0);
    for (const G &f : m.fail) {
        switch (f.type) {
        case G::Hurt: lose = lose || hurt; break;
        case G::Time: lose = lose || r.ticks > f.seconds * 60; break;
        case G::Turns: lose = lose || r.turns > f.turns; break;
        case G::WormDies: lose = lose || wormDead(f.team, f.worm); break;
        default: break;
        }
    }
    if (win || lose) missionEnd(g, !lose);
}

void missionEnd(Game &g, bool won) {
    if (g.phase == Phase::GameOver) return;
    g.run.result = won ? 1 : -1;
    g.phase = Phase::GameOver;
    g.winner = -1;
    for (int t = 0; t < g.teams && g.winner < 0; t++) if (!teamDead(g, t) && (t == 0) == won) g.winner = t;
    g.events.push_back({GameEvent::GameOver, g.worms.empty() ? Vector3{} : g.worms[g.current].pos, -1, -1});
}

std::string goalText(const MissionSpec &m, const MissionSpec::Goal &o, const Game *g) {
    using G = MissionSpec::Goal;
    auto wormName = [&](int t, int k) {
        std::string n = t < (int)m.teams.size() && k < (int)m.teams[t].worms.size() ? m.teams[t].worms[k].name : "";
        return n.empty() ? std::string(t ? "the enemy leader" : "your worm") : n;
    };
    auto clockText = [](int s) { return std::string(TextFormat("%d:%02d", s / 60, s % 60)); };
    int left = 0, total = 0;
    if (g && (o.type == G::KillAll || o.type == G::PoisonAll))
        for (const Worm &w : g->worms) if (w.team != 0 && (w.alive || o.type == G::KillAll)) total++, left += w.alive && (o.type == G::KillAll || !w.poison);
    switch (o.type) {
    case G::KillAll: return g ? TextFormat("Eliminate every enemy worm (%d left)", left) : "Eliminate every enemy worm";
    case G::Kill: return "Take out " + wormName(o.team, o.worm);
    case G::Reach: return "Reach the marked spot";
    case G::Collect: return g ? TextFormat("Collect the crates %d/%d", std::min(g->run.collected, o.count), o.count) : TextFormat("Collect %d crate%s", o.count, o.count > 1 ? "s" : "");
    case G::Destroy: return g ? TextFormat("Destroy the targets %d/%d", std::min(g->run.destroyed, o.count), o.count) : TextFormat("Destroy %d target%s", o.count, o.count > 1 ? "s" : "");
    case G::PoisonAll: return g ? TextFormat("Poison every worm (%d left)", left) : "Poison every worm";
    case G::Survive: return o.seconds ? "Survive for " + clockText(o.seconds) : TextFormat("Survive %d turns", o.turns);
    case G::Hurt: return "Don't get hurt";
    case G::Time: return "Time limit " + clockText(o.seconds);
    case G::Turns: return TextFormat("Within %d turns", o.turns);
    case G::WormDies: return "Keep " + wormName(o.team, o.worm) + " alive";
    default: return "";
    }
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
