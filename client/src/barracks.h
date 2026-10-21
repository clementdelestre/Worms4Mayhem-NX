#pragma once
// W4M saved teams, DATA.TeamBarracks (docs/w4m/frontend.md "Saved teams", docs/match-setup.md "Saved teams"): teams.txt beside
// setup.txt, seeded from W4M's defaults; and the team editor's W4M lists (tools/w4m-re/setuplists.py, assets/*.txt).
#include "sim.h"
#include <string>
#include <vector>

struct SavedTeam {  // W4M StoredTeamData
    std::string name;
    std::vector<std::string> worms = std::vector<std::string>(6);
    int skill = 0;  // 0 human, 1..5 CPU level, fixed at creation
    int grave = 0;  // graves.txt row
    std::string flag, speech = "voclassi", weapon;  // weapon: CustomWeapon, the team's Weapon Factory weapon
    std::string hat, gloves, glasses, tash;          // WXFE.A.* ids, "" none
    // the team in a match slot: worms 1..6 (0 = the match default), alliance 0..3 (-1 = AddTeam's), handicap -1..1
    GameConfig::Team slot(int count = 0, int alliance = -1, int handicap = 0) const;
};

struct Barracks {
    static constexpr int MAX = 20;  // the Create Team row goes at 20 teams (0x4bf9f0, 0x742134 "Too many teams")
    std::vector<SavedTeam> teams;
    // path missing: the W4M defaults, then once the teams of an old setup.txt (team / net lines) not already there
    void load(const char *path, const char *defaults, const char *setup = nullptr);
    bool save(const char *path) const;
    int find(const std::string &name) const;  // -1 if none
    int humans() const;
    // W4M SetUpNew 0x742134: a unique name, 6 distinct names from FE.RandomWormNames (comma list), the language's flag
    // (Flag.Terminator for a CPU), voclassi; -1 when full, the name is empty or taken
    int create(const std::string &name, int skill, const std::string &randomNames, const std::string &flag, uint32_t seed);
    bool canDelete(int i) const;  // WXDeleteTeam 0x4c4790: never the last human team
    bool remove(int i);
    static std::string legacyHat(int hat);  // our older setup.txt hat number (1 + rank in the sorted hat files) -> its id
};

// The editor's lists, W4M order. Item id: attachment / flag / speech id; model: assets/models path (attachments, graves) or ui png (flags)
struct Catalog {
    struct Item { std::string id, model, text; int type = 0; };
    std::vector<Item> attachments;  // WORMATTACHMENTS: type 0 hat, 1 gloves, 2 glasses, 3 tash, 4 the Terminator hat
    std::vector<Item> graves;       // SelectGraveList, by GraveIndex (id = its Grave.* mesh)
    std::vector<Item> flags;        // SelectFlagList
    std::vector<Item> speech;       // SelectSoundbankList
    std::vector<std::string> stockWeapons;  // LOCAL DATA.LockedWeapons, stock Weapon Factory weapons
    void load(const char *assetDir);
    const Item *attachment(const std::string &id) const;  // null if none
    const Item *flag(const std::string &id) const;
    const char *drawName(const std::string &id) const;  // the Models name of an attachment, null for "" or unknown
};
const Catalog &catalog();  // loaded from assets on first use
