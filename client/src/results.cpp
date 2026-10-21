#include "results.h"
#include "ui.h"
#include <algorithm>
#include <random>

namespace {
constexpr int LAST_WEAPON = 0x1e;  // the weapon of choice is the first of W4M ids 1..0x1e (0x619a90) with the most shots

// one value per worm for an accolade type (RoundStatsSummary 0x618190: 0 Damage, 1 Kills, 2 Accuracy, 3 Greedy, 4 Boring, 5 Survivor, 6 TeamKills)
uint32_t value(const Results::Worm &w, int type) {
    switch (type) {
    case 0: return w.damage;
    case 1: return w.kills;
    case 2: return w.shots < 1 ? 0 : (w.damaging * 100 + w.shots / 2) / w.shots;
    case 3: return w.crates;
    case 4: return w.boring;
    case 5: return w.survived;
    default: return w.teamKills;
    }
}

// AccoladeRulesImpl (0x619f40..0x61a5b0): the one worm with the greatest (or least) value; none when it is shared, or a greatest of 0
bool pick(const Results::Scope &s, int type, bool positive, int &worm, uint32_t &v) {
    bool greatest = (type == 4 || type == 6) ? !positive : positive;  // Boring and TeamKills reward the least
    uint32_t best = greatest ? 0 : 0xffffffffu;
    bool found = false, tied = false;
    for (int i = 0; i < Results::WORMS; i++) {
        uint32_t x = value(s.worm[i], type);
        if (x == best) tied = true;
        if (greatest ? x > best : x < best) best = x, worm = i, found = true, tied = false;
    }
    v = best;
    return found && !tied;
}
}  // namespace

void Results::begin(const GameConfig &c) {
    *this = {};
    cfg = c;
}

void Results::nextRound() {
    current = {};
    awards.clear();
    over = false;
}

// docs/w4m/stats.md §27: every update goes to the round's RoundStats, the match gets them merged at the round's end
void Results::event(const Game &g, const GameEvent &e) {
    Scope &s = current;
    if (s.weaponShots.empty()) s.weaponShots.assign(g.teams, std::vector<int>(LAST_WEAPON + 1));
    const int cur = g.current;
    static bool damaged = false;  // the shot in flight has hurt something (stat 7 counts it once)
    static int lastHit = -1;      // the weapon of the last damage: the Starburst's suicide kills are nobody's
    switch (e.kind) {
    case GameEvent::Fire: {
        if (e.worm < 0) break;
        const int id = g.containerOf(e.weapon, false);
        damaged = false;
        if (id < 1 || id > 31 || id == 8 || id == 14 || id == 27) break;  // no Landmine, Flood, Sentry Gun; Skip Go posts as a boring turn
        s.worm[e.worm].shots++;
        if (id <= LAST_WEAPON) s.weaponShots[g.worms[e.worm].team][id]++;
        break;
    }
    case GameEvent::Hurt:
        if (e.weapon == 6 || e.worm < 0 || e.count <= 0) break;  // poison is nobody's damage
        s.worm[cur].damage += e.count, s.worm[cur].damaging += !damaged, damaged = true;
        break;
    case GameEvent::Death:
        if (e.worm < 0) break;
        s.worm[e.worm].survived = std::max<uint32_t>(s.worm[e.worm].survived, g.clock / 60);
        if (e.worm == cur ? lastHit == 20 : false) break;
        (e.worm == cur || g.worms[e.worm].team == g.worms[cur].team || g.alliance(g.worms[e.worm].team) == g.alliance(g.worms[cur].team) ? s.worm[cur].teamKills : s.worm[cur].kills)++;
        break;
    case GameEvent::Collect:
    case GameEvent::Mystery:
        s.worm[cur].crates++;  // the active worm's, not the picker's
        break;
    default: break;
    }
    if (e.kind == GameEvent::Fire && e.worm >= 0 && WEAPONS[e.weapon].name == "Skip Go") s.worm[e.worm].boring++;
    if (e.kind == GameEvent::Fire) lastHit = g.containerOf(e.weapon, false);
}

void Results::finish(const Game &g) {
    round++;
    winner = g.winner;
    winString = Ui::resultBanner(g);
    if (winner >= 0)
        for (int t = 0; t < g.teams && t < TEAMS; t++)
            if (g.alliance(t) == g.alliance(winner)) roundWins[t]++;
    over = winner >= 0 && roundWins[winner] >= std::max<int>(1, cfg.scheme.wins);
    const uint32_t secs = (uint32_t)g.clock / 60;
    for (int i = 0; i < (int)g.worms.size() && i < WORMS; i++)
        if (g.worms[i].hp > 0) current.worm[i].survived = std::max(current.worm[i].survived, secs);  // Stats.Populate.Round
    current.seconds = secs;
    // PresentAwards: the round into the match, summed except Survival (the greater)
    if (match.weaponShots.size() < current.weaponShots.size()) match.weaponShots = current.weaponShots;
    else
        for (size_t t = 0; t < current.weaponShots.size(); t++)
            for (int id = 0; id <= LAST_WEAPON; id++) match.weaponShots[t][id] += current.weaponShots[t][id];
    for (int i = 0; i < WORMS; i++) {
        Worm &m = match.worm[i], &c = current.worm[i];
        m.damage += c.damage, m.kills += c.kills, m.teamKills += c.teamKills, m.shots += c.shots, m.damaging += c.damaging;
        m.crates += c.crates, m.boring += c.boring, m.survived = std::max(m.survived, c.survived);
    }
    match.seconds += secs;
    summarise((uint32_t)std::random_device{}());
}

// RoundStatsSummary 0x618550 (the rows) and MatchStatsSummary 0x6173e0 (the pages)
void Results::summarise(uint32_t seed) {
    static const char *const SUFFIX[7] = {" (%u)", " (%u)", " (%u %%)", "", "", " (%um %us)", " (%u)"};
    std::mt19937 rng(seed);
    int order[7] = {0, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 7; i++) std::swap(order[i], order[rng() % 6]);
    int pos = 0, neg = 0, w;
    uint32_t v;
    for (int t = 0; t < 7; t++) {
        if (pos < 4 && pick(current, t, true, w, v)) pos++;
        if (neg < 4 && pick(current, t, false, w, v)) neg++;
    }
    if (pos + neg > 4) {
        if (pos < 2) neg = std::min(neg, 4 - pos);
        else if (neg >= 2) pos = neg = 2;
        else pos = 4 - neg;
    }
    awards.clear();
    for (int k = 0; awards.size() < 4 && pos + neg > 0 && k < 14; k++) {
        const int t = order[k % 7];
        const bool positive = pos > 0;
        if (!pick(current, t, positive, w, v)) continue;
        int team = 0, slot = w;  // worms are team-major
        while (team < cfg.teams - 1 && slot >= cfg.teamWorms(team)) slot -= cfg.teamWorms(team++);
        const char *key = TextFormat(positive ? "Stat.Accolade.Pve.%u" : "Stat.Accolade.Nve.%u", t);
        std::string label = Ui::tr(key, key), tail = t == 5 ? TextFormat(SUFFIX[t], v / 60, v % 60) : TextFormat(SUFFIX[t], v);
        awards.push_back({label + tail, Ui::wormName(cfg, team, slot), team, positive});
        (positive ? pos : neg)--;
    }
    pages.clear();
    for (int t = 0, first = 0; t < cfg.teams && t < TEAMS; first += cfg.teamWorms(t++)) {
        Page p = {Ui::teamName(cfg, t), t, 0, 0, 0, 0};
        uint32_t shots = 0, hits = 0;
        for (int i = first; i < first + cfg.teamWorms(t) && i < WORMS; i++) {
            const Worm &x = match.worm[i];
            p.damage += x.damage, p.kills += x.kills, shots += x.shots, hits += x.damaging;
        }
        p.accuracy = shots ? std::min<uint32_t>(100, hits * 100 / shots) : 0;
        int best = 0;
        if (t < (int)match.weaponShots.size())
            for (int id = 1; id <= LAST_WEAPON; id++)
                if (match.weaponShots[t][id] > best) best = match.weaponShots[t][id], p.weapon = id;
        pages.push_back(p);
    }
}
