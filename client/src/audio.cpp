#include "audio.h"
#include "raylib.h"

#include <algorithm>
#include <string>
#include <vector>

#ifdef __SWITCH__
#define ASSET_ROOT "sdmc:/switch/worms4nx/assets/"
#define ROMFS_ROOT "romfs:/"
#else
#define ASSET_ROOT "./assets/"
#define ROMFS_ROOT "./romfs/"
#endif

namespace Audio {
namespace {

// importer drops extracted W4M sounds under ASSET_ROOT; bundled CC0 defaults live in ROMFS_ROOT
const char *SFX_NAMES[(int)Sfx::Count] = {
    "explosion", "big_explosion", "fire", "bounce", "splash", "jump", "sheep", "holy", "turn_start", "tick",
    "shotgun",   "airstrike",     "donkey", "rope", "teleport",
    "bat_swing", "fire_punch", "prod", "sniper", "bow", "homing", "old_woman", "scouser", "sentry_place", "sentry_fire",
    "dynamite", "gas", "abduction", "flood", "parachute", "mine_beep", "crate_land", "pickup", "super_sheep",
};
const char *VOICE_NAMES[(int)Voice::Count] = {"fire", "hurt", "death", "victory", "jump", "idle"};
constexpr int MAX_VARIANTS = 12;

struct Variants {
    Sound s[MAX_VARIANTS];
    int n = 0;
};

struct Bank {
    std::string dir;
    bool loaded = false;
    Variants lines[(int)Voice::Count];
};

Variants sfx[(int)Sfx::Count];
std::vector<Bank> banks;
std::vector<int> teamBank;  // team -> bank, -1 = default
Music theme;
std::string track;
bool musicLoaded = false, musicOn = false;

// base.ogg, base_2.ogg, ... until the first gap; base is copied since TextFormat recycles its buffers
Variants loadVariants(std::string base) {
    Variants v;
    for (; v.n < MAX_VARIANTS; v.n++) {
        const char *p = v.n ? TextFormat("%s_%d.ogg", base.c_str(), v.n + 1) : TextFormat("%s.ogg", base.c_str());
        if (!FileExists(p)) break;
        v.s[v.n] = LoadSound(p);
    }
    return v;
}

void unload(Variants &v) {
    for (int i = 0; i < v.n; i++) UnloadSound(v.s[i]);
    v.n = 0;
}

void playRandom(Variants &v, float volume) {
    if (!v.n) return;
    Sound &s = v.s[GetRandomValue(0, v.n - 1)];
    SetSoundVolume(s, volume);
    PlaySound(s);
}

std::vector<std::string> bankDirs(const char *root) {
    std::vector<std::string> dirs;
    const char *voices = TextFormat("%svoices", root);
    if (!DirectoryExists(voices)) return dirs;
    FilePathList l = LoadDirectoryFilesEx(voices, "DIRS*", false);
    for (unsigned i = 0; i < l.count; i++) dirs.push_back(l.paths[i]);
    UnloadDirectoryFiles(l);
    std::sort(dirs.begin(), dirs.end());  // stable team -> bank mapping across runs
    return dirs;
}

bool openMusic(const char *name) {
    for (const char *root : {ASSET_ROOT, ROMFS_ROOT}) {
        const char *p = TextFormat("%smusic/%s.ogg", root, name);
        if (!FileExists(p)) continue;
        Music m = LoadMusicStream(p);
        if (!IsMusicValid(m)) continue;
        if (musicLoaded) UnloadMusicStream(theme);
        theme = m;
        theme.looping = true;
        musicLoaded = true;
        track = name;
        return true;
    }
    return false;
}

}  // namespace

void init() {
    InitAudioDevice();
    for (int i = 0; i < (int)Sfx::Count; i++) {
        sfx[i] = loadVariants(TextFormat("%ssfx/%s", ASSET_ROOT, SFX_NAMES[i]));
        if (!sfx[i].n) sfx[i] = loadVariants(TextFormat("%ssfx/%s", ROMFS_ROOT, SFX_NAMES[i]));
    }
    std::vector<std::string> dirs = bankDirs(ASSET_ROOT);
    if (dirs.empty()) dirs = bankDirs(ROMFS_ROOT);
    for (auto &d : dirs) banks.push_back(Bank{d, false, {}});
    openMusic("theme");
}

void shutdown() {
    for (auto &v : sfx) unload(v);
    for (auto &b : banks) for (auto &v : b.lines) unload(v);
    banks.clear();
    if (musicLoaded) UnloadMusicStream(theme);
    musicLoaded = false;
    CloseAudioDevice();
}

void update() {
    if (musicLoaded && musicOn) UpdateMusicStream(theme);
}

void play(Sfx id, float volume) {
    Variants *v = &sfx[(int)id];
    if (!v->n && id > Sfx::Tick) v = &sfx[(int)Sfx::Fire];
    playRandom(*v, volume);
}

// banks load lazily (~30 decoded upfront would cost ~300 MB); preloadVoices() moves the hitch to match start
static Bank &bankOf(int team) {
    int k = team < (int)teamBank.size() && teamBank[team] >= 0 ? teamBank[team] : team;
    Bank &b = banks[k % banks.size()];
    if (!b.loaded) {
        for (int i = 0; i < (int)Voice::Count; i++) b.lines[i] = loadVariants(TextFormat("%s/%s", b.dir.c_str(), VOICE_NAMES[i]));
        b.loaded = true;
    }
    return b;
}

void voice(int team, Voice id) {
    if (!banks.empty()) playRandom(bankOf(team).lines[(int)id], 1.0f);
}

void preloadVoices(int teams) {
    for (int t = 0; t < teams && !banks.empty(); t++) bankOf(t);
}

int voiceBanks() { return (int)banks.size(); }

const char *voiceBankName(int bank) { return bank >= 0 && bank < (int)banks.size() ? GetFileName(banks[bank].dir.c_str()) : ""; }

void setTeamVoice(int team, int bank) {
    if (team >= (int)teamBank.size()) teamBank.resize(team + 1, -1);
    teamBank[team] = bank;
}

void music(bool on, const char *name) {
    if (name && track != name && !openMusic(name) && track != "theme") openMusic("theme");
    musicOn = on;
    if (!musicLoaded) return;
    if (on && !IsMusicStreamPlaying(theme)) PlayMusicStream(theme);
    else if (!on) StopMusicStream(theme);
}

}  // namespace Audio
