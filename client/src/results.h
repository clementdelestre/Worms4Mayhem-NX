#pragma once
#include "sim.h"
#include <string>
#include <vector>

// Rounds and statistics of a versus match (docs/w4m/stats.md, docs/w4m/frontend.md "Match flow"): what W4M's end screens show.
// One lives across the rounds of a match (main.cpp); the screens are in results.inc (ui.cpp).
struct Results {
    static constexpr int TEAMS = 4, WORMS = MAX_MATCH_WORMS;
    // one worm's counters of a scope (W4M StoredStatsCollective store ids, "Stat.Name.<id>")
    struct Worm {
        uint32_t damage = 0, kills = 0, teamKills = 0, shots = 0, damaging = 0, crates = 0, boring = 0, survived = 0;
    };
    struct Scope {
        Worm worm[WORMS];
        std::vector<std::vector<int>> weaponShots;  // [team][W4M weapon id]: shots of each weapon (the weapon of choice)
        uint32_t seconds = 0;                       // ElapsedMatchTime / ElapsedRoundTime
    };
    GameConfig cfg;              // the match's setup: team names, alliances, scheme
    int round = 0;               // GameOver.RoundNumber: rounds finished
    int roundWins[TEAMS] = {};   // per team (team stats record +0x14)
    Scope match, current;        // MatchStats over every round, RoundStats of the round being played
    bool over = false;           // the last round finished the match
    std::string winString;       // FE.WinString of the last round
    int winner = -1;             // its winning team, -1 a draw

    // WXFE.WinRound rows (RoundStatsSummaryLogicEntity 0x618550): award, its worm's name, the team, positive or not
    struct Award { std::string label, who; int team; bool positive; };
    std::vector<Award> awards;
    // WXFE.MatchStatsList.Team<n> (MatchStatsSummaryLogicEntity 0x6173e0): one team's page at the match end
    struct Page { std::string name; int team; uint32_t damage, kills, accuracy; int weapon; };
    std::vector<Page> pages;

    void begin(const GameConfig &c);                // a new match: Stats.ResetMatch, no round played
    void event(const Game &g, const GameEvent &e);  // each sim event of the round being played
    void finish(const Game &g);                     // the round ended (Phase::GameOver): round wins, WinString, awards, pages
    void nextRound();                               // StartGame$NextRound: Stats.ResetRound
    // the accolades / pages of the scopes as they stand (finish() calls them; tests build the data by hand)
    void summarise(uint32_t seed);
};

namespace Ui {
// WXFE.WinRound / WinMatch / PreRound (WXNET.WinMatch / DrawMatch when net): one screen, drawn inside Begin/EndDrawing
struct ResultsScreen {
    enum Kind { WinRound, WinMatch, DrawMatch, PreRound } kind = WinMatch;
    enum Action { None, Leave, NextRound };  // Leave: FCS.LeaveWinMatchScreen / ConfirmMatchQuit Yes (the main menu or the room); NextRound: StartGame$NextRound
    bool net = false;
    float shown = -1, leaving = -1, popShown = 0, pageT = 0;  // now() s: the menu / its leaving / ConfirmMatchQuit / the stats page appeared
    int after = None;       // what the leaving ends in: Action, or PreRound (FE.ChangeMenu)
    int page = 0, sel = 0;  // team stats page, highlighted item
    int confirm = -1;       // WXFEP.ConfirmMatchQuit: -1 closed, else the highlighted row (0 No, 1 Yes)
    int land = -1;          // WXFEP.SelectLandscape2 (PreRound's LandscapeControl): -1 closed, else the highlighted map
    float landShown = 0;
};
int results(ResultsScreen &st, struct Results &r, const std::vector<std::string> &maps);  // maps: the landscapes PreRound offers; a pick sets r.cfg.map
}  // namespace Ui
