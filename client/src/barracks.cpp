#include "barracks.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef __SWITCH__
#define ASSET_DIR "sdmc:/switch/worms4nx/assets/"
#else
#define ASSET_DIR "./assets/"
#endif

GameConfig::Team SavedTeam::slot(int count, int alliance, int handicap, const std::vector<WeaponDef> &factory) const {
    GameConfig::Team t;
    t.name = name, t.cpu = (uint8_t)std::clamp(skill, 0, 5), t.speech = speech, t.hat = hat, t.gloves = gloves, t.glasses = glasses, t.tash = tash;
    t.flag = flag, t.grave = (uint8_t)std::clamp(grave, 0, 255), t.wormNames = worms;
    t.worms = (uint8_t)std::clamp(count, 0, 6), t.alliance = (int8_t)(alliance < 0 ? -1 : alliance & 3), t.handicap = (int8_t)std::clamp(handicap, -1, 1);
    for (const std::vector<WeaponDef> *list : {&catalog().stock, &factory})
        for (const WeaponDef &w : *list) if (!weapon.empty() && w.name == weapon) t.secret = w;
    return t;
}

// teams.txt: per team "team <name>", 6 x "worm <name>", "skill N", "grave N", "flag", "speech", "weapon", "hat", "gloves", "glasses",
// "tash" lines (value = the rest of the line, may be empty), "end"
static std::vector<SavedTeam> parse(std::istream &in) {
    std::vector<SavedTeam> out;
    SavedTeam t;
    int worm = 0;
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t sp = l.find(' ');
        std::string k = l.substr(0, sp), v = sp == std::string::npos ? "" : l.substr(sp + 1);
        if (k == "team") t = SavedTeam{}, t.name = v, worm = 0;
        else if (k == "worm" && worm < 6) t.worms[worm++] = v;
        else if (k == "skill") t.skill = std::clamp(atoi(v.c_str()), 0, 5);
        else if (k == "grave") t.grave = std::max(0, atoi(v.c_str()));
        else if (k == "flag") t.flag = v;
        else if (k == "speech") t.speech = v;
        else if (k == "weapon") t.weapon = v;
        else if (k == "hat") t.hat = v;
        else if (k == "gloves") t.gloves = v;
        else if (k == "glasses") t.glasses = v;
        else if (k == "tash") t.tash = v;
        else if (k == "end" && !t.name.empty() && out.size() < (size_t)Barracks::MAX) out.push_back(t);
    }
    return out;
}

std::string Barracks::legacyHat(int hat) {
    std::vector<const Catalog::Item *> hats;
    for (const Catalog::Item &a : catalog().attachments) if (a.type == 0 || a.type == 4) hats.push_back(&a);
    std::sort(hats.begin(), hats.end(), [](const Catalog::Item *a, const Catalog::Item *b) { return a->model < b->model; });
    return hat > 0 && hat <= (int)hats.size() ? hats[hat - 1]->id : "";
}

// setup.txt "team <i> <cpu> <hat number | WXFE.A id | -> <voice | -> <name>" / "net ..." lines of our builds before teams.txt
static void migrate(Barracks &b, const char *setup) {
    std::ifstream in(setup);
    for (std::string l; std::getline(in, l) && b.teams.size() < (size_t)Barracks::MAX;) {
        int i = 0, cpu = 0, n = 0;
        char hat[64] = "", voice[64] = "";
        if ((sscanf(l.c_str(), "team %d %d %63s %63s %n", &i, &cpu, hat, voice, &n) != 4 && sscanf(l.c_str(), "net %d %d %63s %63s %n", &i, &cpu, hat, voice, &n) != 4) ||
            !l[n] || b.find(l.c_str() + n) >= 0)
            continue;
        SavedTeam t;  // no worm names or flag: the match falls back to ours, as before
        t.name = l.c_str() + n, t.skill = std::clamp(cpu, 0, 5), t.worms.assign(6, "");
        t.hat = !strcmp(hat, "-") ? "" : isdigit((unsigned char)hat[0]) ? Barracks::legacyHat(atoi(hat)) : hat;
        if (strcmp(voice, "-")) t.speech = strncmp(voice, "vo", 2) ? std::string("vo") + voice : voice;
        b.teams.push_back(t);
    }
}

void Barracks::load(const char *path, const char *defaults, const char *setup) {
    std::ifstream in(path);
    if (in) return (void)(teams = parse(in));
    std::ifstream d(defaults);
    teams = parse(d);
    if (setup) migrate(*this, setup);
}

bool Barracks::save(const char *path) const {
    std::ostringstream o;
    for (const SavedTeam &t : teams) {
        o << "team " << t.name << "\n";
        for (const std::string &w : t.worms) o << "worm " << w << "\n";
        o << "skill " << t.skill << "\ngrave " << t.grave << "\nflag " << t.flag << "\nspeech " << t.speech << "\nweapon " << t.weapon << "\nhat " << t.hat
          << "\ngloves " << t.gloves << "\nglasses " << t.glasses << "\ntash " << t.tash << "\nend\n";
    }
    std::ofstream f(path, std::ios::binary);
    return f << o.str() && f.flush();
}

int Barracks::find(const std::string &name) const {
    for (size_t i = 0; i < teams.size(); i++) if (teams[i].name == name) return (int)i;
    return -1;
}

int Barracks::humans() const { return (int)std::count_if(teams.begin(), teams.end(), [](const SavedTeam &t) { return !t.skill; }); }

int Barracks::create(const std::string &name, int skill, const std::string &randomNames, const std::string &flag, uint32_t seed) {
    if (teams.size() >= (size_t)MAX || name.empty() || find(name) >= 0) return -1;
    SavedTeam t;
    t.name = name, t.skill = std::clamp(skill, 0, 5), t.flag = skill ? "Flag.Terminator" : flag;
    std::vector<std::string> pool;
    std::stringstream ss(randomNames);
    for (std::string w; std::getline(ss, w, ',');) pool.push_back(w);
    for (int k = 0; k < 6 && !pool.empty(); k++) {  // 0x737d86: redraw until it differs from the other names
        do seed = seed * 1103515245u + 12345u;
        while (pool.size() > 6 && std::find(t.worms.begin(), t.worms.end(), pool[(seed >> 16) % pool.size()]) != t.worms.end());
        t.worms[k] = pool[(seed >> 16) % pool.size()];
    }
    teams.push_back(t);
    return (int)teams.size() - 1;
}

std::vector<int> Barracks::list(List kind, const std::function<bool(const std::string &)> &placed, bool cpuOk) const {
    std::vector<int> rows;
    for (int i = 0; i < (int)teams.size(); i++) {
        const SavedTeam &t = teams[i];
        if (kind == List::Human ? t.skill != 0 : kind == List::Delete ? !canDelete(i) : kind == List::Insert && ((placed && placed(t.name)) || (t.skill && !cpuOk))) continue;
        rows.push_back(i);
    }
    return rows;
}

bool Barracks::canDelete(int i) const { return i >= 0 && i < (int)teams.size() && (teams[i].skill || humans() > 1); }

bool Barracks::remove(int i) {
    if (!canDelete(i)) return false;
    teams.erase(teams.begin() + i);
    return true;
}

void Catalog::load(const char *dir) {
    auto rows = [&](const char *file, auto add) {
        std::ifstream in(std::string(dir) + file);
        for (std::string l; std::getline(in, l);) {
            std::vector<std::string> f;
            std::stringstream ss(l);
            for (std::string c; std::getline(ss, c, '\t');) f.push_back(c);
            add(f);
        }
    };
    *this = Catalog{};
    rows("attachments.txt", [&](const std::vector<std::string> &f) { if (f.size() >= 5) attachments.push_back({f[1], f[2], f[4], f.size() > 5 ? f[5] : "", atoi(f[0].c_str()), f.size() > 6 ? atoi(f[6].c_str()) : 2}); });
    rows("graves.txt", [&](const std::vector<std::string> &f) { if (f.size() >= 3) graves.push_back({f[1], f[0], f[2]}); });
    rows("flags.txt", [&](const std::vector<std::string> &f) { if (f.size() >= 3) flags.push_back({f[0], "flags/" + f[1], f[2]}); });
    rows("speech.txt", [&](const std::vector<std::string> &f) { if (f.size() >= 2) speech.push_back({f[0], "", f[1]}); });
    loadCustomWeapons((std::string(dir) + "stockweapons.json").c_str(), stock);
    for (const WeaponDef &w : stock) stockWeapons.push_back(w.name);
}

const Catalog::Item *Catalog::attachment(const std::string &id) const {
    for (const Item &a : attachments) if (a.id == id) return &a;
    return nullptr;
}

const Catalog::Item *Catalog::flag(const std::string &id) const {
    for (const Item &a : flags) if (a.id == id) return &a;
    return nullptr;
}

const char *Catalog::drawName(const std::string &id) const {
    const Item *a = id.empty() ? nullptr : attachment(id);
    if (!a) return nullptr;
    return a->type == 0 || a->type == 4 ? a->model.c_str() + a->model.find('/') + 1 : a->model.c_str();  // hats: Models keys them by file name
}

std::vector<std::string> Catalog::usable(int type, const std::vector<std::string> &unlocks, const std::string &current) const {
    std::vector<std::string> ids = {""};
    for (const Item &a : attachments) {
        bool ofType = type == 0 ? a.type == 0 || a.type == 4 : a.type == type;
        if (ofType && (a.state == 2 || a.id == current || (!a.lock.empty() && std::find(unlocks.begin(), unlocks.end(), a.lock) != unlocks.end()))) ids.push_back(a.id);
    }
    return ids;
}

const Catalog &catalog() {
    static Catalog c;
    static bool loaded = false;
    if (!loaded) c.load(ASSET_DIR), loaded = true;
    return c;
}
