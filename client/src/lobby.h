#pragma once
#include "sim.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Match setup shared by the Teams / Map / Rules screens: local play (one console, everything editable) and the network room.
struct Lobby {
    static constexpr int MAX_TEAMS = 4;
    GameConfig cfg;                  // teams, teamSetup (GameConfig::Team), map, rules, scheme, wormpot, wormsPerTeam
    int owner[MAX_TEAMS] = {};       // console id of each team
    bool ready[MAX_TEAMS] = {};      // per team slot: its console is ready (network)
    int self = 0, host = 0;          // this console, the host's console
    bool isHost() const { return self == host; }
    bool canEdit(int team) const { return team >= 0 && team < cfg.teams && owner[team] == self; }  // its team's settings, remove
    bool canEditMatch() const { return isHost(); }  // map, scheme, rules, worms per team, wormpot
    // Slots: local play keeps cfg.teams = MAX_TEAMS and fills slots sparsely (`used`); a room's teams are the slots 0..teams-1
    unsigned used = 0;
    bool playing(int k) const { return k >= 0 && k < MAX_TEAMS && (used >> k & 1); }
    int count() const { return __builtin_popcount(used); }
    // slot k is a human team of this console played by pad p
    bool ownedBy(int k, int p) const { return playing(k) && canEdit(k) && !cfg.teamSetup[k].cpu && cfg.teamSetup[k].pad == p; }
    bool place(int k, int p, const GameConfig::Team &t) {  // pad p puts the saved team t in the free slot k
        if (!canEdit(k) || playing(k)) return false;
        used |= 1u << k, cfg.teamSetup[k] = t, cfg.teamSetup[k].pad = (uint8_t)p;
        return true;
    }
    bool release(int k) {  // a human or CPU slot of this console becomes free
        if (!playing(k) || !canEdit(k)) return false;
        used &= ~(1u << k);
        return true;
    }
    // W4M slot rows (docs/w4m/frontend.md "Filled slot settings"): the worms of the match stay <= MAX_MATCH_WORMS (0x73620f)
    int wormsIn(int except = -1) const {
        int n = 0;
        for (int k = 0; k < MAX_TEAMS; k++) if (playing(k) && k != except) n += cfg.teamWorms(k);
        return n;
    }
    int newWorms() const { return std::min(4, MAX_MATCH_WORMS - wormsIn()); }  // a team enters with min(4, 16 - the worms already in); < 1: the match is full
    int maxWorms(int k) const { return std::min(6, MAX_MATCH_WORMS - wormsIn(k)); }
    int freeAlliance() const {  // AddTeam 0x737633: the lowest group no team in holds
        unsigned held = 0;
        for (int k = 0; k < MAX_TEAMS; k++) if (playing(k) && cfg.teamSetup[k].alliance >= 0) held |= 1u << cfg.teamSetup[k].alliance;
        int g = 0;
        while (g < 3 && (held >> g & 1)) g++;
        return g;
    }
    // the pads that human teams of this console use, as a bit set
    unsigned padsUsed() const {
        unsigned m = 0;
        for (int k = 0; k < MAX_TEAMS; k++) if (playing(k) && canEdit(k) && !cfg.teamSetup[k].cpu) m |= 1u << cfg.teamSetup[k].pad;
        return m;
    }
    // the match: the playing slots in order, colour = rank
    GameConfig match() const {
        GameConfig c = cfg;
        c.teams = 0, c.teamSetup.clear();
        for (int k = 0; k < MAX_TEAMS; k++) if (playing(k)) c.teamSetup.push_back(cfg.teamSetup[k]), c.teams++;
        return c;
    }
};

// Average of two body colours (a Joy-Con pair): red + blue = purple
inline Color mixColors(Color a, Color b) { return {(unsigned char)((a.r + b.r) / 2), (unsigned char)((a.g + b.g) / 2), (unsigned char)((a.b + b.b) / 2), 255}; }

// The Teams screen's tokens, one per pad: the slot each pad is on, moved independently [ours]
struct TeamCursors {
    int slot[4] = {};
    bool placed = false;  // the tokens were put on their first slots
    void move(int p, int d) { slot[p] = std::clamp(slot[p] + d, 0, Lobby::MAX_TEAMS - 1); }
    void removed(int k) { for (int &s : slot) if (s > k) s--; }  // a room's later slots shift up
};

// W4M lobby timer (docs/w4m/network.md "Lobby flow"): 30 s from the moment a second player is there, restarted by each join, off under 2 players;
// when it ends everyone is set ready.
struct ReadyTimer {
    double end = -1;  // deadline (clock s), -1 none, -2 already ended
    int seen = 0;
    // true once, when the deadline passes
    bool update(double now, int players, double length = 30) {
        if (players < 2) end = -1;
        else if (players > seen) end = now + length;
        seen = players;
        if (end >= 0 && now >= end) return end = -2, true;
        return false;
    }
    int seconds(double now) const { return end < 0 ? -1 : std::max(0, (int)std::ceil(end - now)); }  // -1: shown as "--"
};
