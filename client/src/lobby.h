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
    // Contrôle: 0..3 a pad, 4..8 CPU level 1..5
    static int ctrlOf(const GameConfig::Team &t) { return t.cpu ? 3 + t.cpu : t.pad; }
    static void setCtrl(GameConfig::Team &t, int v) { t.cpu = v < 4 ? 0 : (uint8_t)(v - 3), t.pad = v < 4 ? (uint8_t)v : t.pad; }
    // One edit of team k's row (1 Control, 2 Voice, 3 Hat) by d steps; ctrls: how many Control values are offered; false when nothing may change
    bool cycle(int k, int row, int d, int ctrls, int voices, int hats) {
        if (!canEdit(k) || !d) return false;
        GameConfig::Team &t = cfg.teamSetup[k];
        auto wrap = [d](int v, int n) { return n ? ((v + d) % n + n) % n : 0; };
        if (row == 1) setCtrl(t, wrap(ctrlOf(t), ctrls));
        else if (row == 2 && voices) t.voice = (uint8_t)wrap(t.voice, voices);
        else if (row == 3 && hats) t.hat = (uint8_t)wrap(t.hat, hats + 1);
        else return false;
        return true;
    }
    // the pads that human teams of this console use, as a bit set
    unsigned padsUsed() const {
        unsigned m = 0;
        for (int k = 0; k < cfg.teams; k++) if (canEdit(k) && !cfg.teamSetup[k].cpu) m |= 1u << cfg.teamSetup[k].pad;
        return m;
    }
};

// The Teams screen's cursors, one per pad: each walks the MAX_TEAMS cards (rows 0 name, 1 Control, 2 Voice, 3 Hat; an empty card has row 0 only) on its own [ours]
struct TeamCursors {
    struct At { int card = 0, row = 0; } at[4];
    bool placed = false;  // the cursors were put on their first teams
    static int rows(int card, int teams) { return card < teams ? 4 : 1; }
    void row(int p, int d, int teams) { int n = rows(at[p].card, teams); at[p].row = ((at[p].row + d) % n + n) % n; }
    void card(int p, int d, int teams) {
        at[p].card = ((at[p].card + d) % Lobby::MAX_TEAMS + Lobby::MAX_TEAMS) % Lobby::MAX_TEAMS;
        at[p].row = std::min(at[p].row, rows(at[p].card, teams) - 1);
    }
    // team k was removed: the cursors on later cards follow their team up; every row is clamped to the new count
    void removed(int k, int teams) {
        for (At &a : at) {
            if (a.card > k) a.card--;
            a.row = std::min(a.row, rows(a.card, teams) - 1);
        }
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
