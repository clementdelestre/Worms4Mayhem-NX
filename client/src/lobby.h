#pragma once
#include "sim.h"
#include <algorithm>
#include <cmath>

// Match setup shared by the Teams / Map / Rules screens: local play (one console, everything editable) and the network room.
struct Lobby {
    static constexpr int MAX_TEAMS = 4;
    GameConfig cfg;                  // teams, teamSetup (name, cpu, voice, hat, local pad), map, rules, scheme, wormpot, wormsPerTeam
    int owner[MAX_TEAMS] = {};       // console id of each team
    bool ready[MAX_TEAMS] = {};      // per team slot: its console is ready (network)
    int self = 0, host = 0;          // this console, the host's console
    bool isHost() const { return self == host; }
    bool canEdit(int team) const { return team >= 0 && team < cfg.teams && owner[team] == self; }  // name, control, voice, hat
    bool canEditMatch() const { return isHost(); }  // map, scheme, rules, worms per team, wormpot
    // a team is added at the end; removing one shifts the later teams up (colour = slot)
    bool addTeam() {
        if (cfg.teams >= MAX_TEAMS) return false;
        owner[cfg.teams++] = self;
        return true;
    }
    bool removeTeam(int k, int least) {
        if (cfg.teams <= least || !canEdit(k)) return false;
        GameConfig::Team gone = cfg.teamSetup[k];
        for (int i = k; i < MAX_TEAMS - 1; i++) cfg.teamSetup[i] = cfg.teamSetup[i + 1], owner[i] = owner[i + 1];
        cfg.teamSetup[MAX_TEAMS - 1] = gone, owner[MAX_TEAMS - 1] = self, cfg.teams--;
        return true;
    }
    // the pads that human teams of this console use, as a bit set
    unsigned padsUsed() const {
        unsigned m = 0;
        for (int k = 0; k < cfg.teams; k++) if (canEdit(k) && !cfg.teamSetup[k].cpu) m |= 1u << cfg.teamSetup[k].pad;
        return m;
    }
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
