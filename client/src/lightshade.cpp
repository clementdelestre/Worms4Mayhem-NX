#include "lightshade.h"
#include "acting.h"
#include "raymath.h"
#include <algorithm>
#include <unordered_map>

namespace {
// W4M light entry (0x50 bytes): pos, offset +0x2c, fade +0x1c (-1 until the first update), target +0x24, the hit cell's handle +0x3e,
// pool: the 6-level pooled light of an entity made with flag 0 (0x47bac0), not its own directional light
struct Entry { uint32_t key; Vector3 pos; float off; bool pool; float cur = -1, tgt = 1; bool fresh = true, hit = false; Vector3 in{}; };
std::vector<Entry> entries;
std::unordered_map<uint32_t, size_t> at;
size_t cursor = 0, forced = 0;  // the round-robin cursors [0x9558c0] / [0x9558bc]
int last = 0;
float acc = 0;

void put(uint32_t key, Vector3 pos, float off, bool pool, std::vector<Entry> &out) {
    auto it = at.find(key);
    Entry e = it == at.end() ? Entry{key, pos, off, pool} : entries[it->second];
    e.pos = pos, e.off = off, e.pool = pool;
    out.push_back(e);
}

// 0x47a1a0 (cur, target, 1, 0.1): halfway to the target within 0.1; the first value (-1, 0x47f3e0) is the target itself
void smooth(Entry &e) { e.cur = e.cur < 0 ? e.tgt : e.cur + std::clamp((e.tgt - e.cur) / 2, -0.1f, 0.1f); }

void cast(const Terrain &t, Entry &e, bool back) {
    const Terrain::Shade s = t.sunShade(e.pos, e.off, back);
    e.tgt = s.k, e.hit = s.hit, e.in = s.in;
}

// a ray is due when the hit cell is gone (the cell bit 0x471740 clears on a blast, 0x471710)
bool due(const Terrain &t, const Entry &e) { return e.hit && !t.solid(e.in); }
}  // namespace

float LightShade::k(uint32_t key) {
    auto it = at.find(key);
    if (it == at.end() || entries[it->second].cur < 0) return 1;
    const Entry &e = entries[it->second];
    return e.pool ? (float)std::clamp((int)(e.cur * 6), 0, 5) / 5 : e.cur;  // 0x47c800: pool level = int(intensity x fade x 6) clamped to 0..5, light level / 5
}

void LightShade::step(const Game &g, float dt, const std::vector<Spot> &extra) {
    if (g.clock < last) entries.clear(), at.clear(), cursor = forced = 0;
    last = g.clock;
    if ((acc += dt) < 0.02f) return;  // the per-frame task queue's 20 ms (0x68d57a)
    acc = fmodf(acc, 0.02f);
    // the entities with a light, W4M's creators of 0x48ebe0: worm 0x5a49a0 (own light, offset 10 0x5a4ac8), crates 0x5c4d10 / oil drums
    // 0x5d0f40 (pooled), mines 0x57cad0 / sentries 0x56b2d0 / graves 0x5ce7e0 / shots 0x57cad0 / the mine factory 0x5cf1e0 / bubble machines 0x54e720 / the girder preview 0x5588c0
    // (own, offset 0), the saucer 0x5463b0 (pooled)
    std::vector<Entry> now;
    for (size_t i = 0; i < g.worms.size(); i++)
        if (g.worms[i].alive || g.worms[i].counted > 0) put(key(Worm, (int)i), {g.worms[i].pos.x, g.worms[i].pos.y - Game::R, g.worms[i].pos.z}, 10, false, now);
    for (size_t i = 0; i < g.objects.size(); i++) {
        const ::Object &o = g.objects[i];
        if (o.type != ::Object::Target) put(key(Object, (int)i), o.pos, 0, o.type == ::Object::Crate || o.type == ::Object::Barrel, now);
    }
    for (size_t i = 0; i < g.graves.size(); i++) put(key(Grave, (int)i), g.graves[i].pos, 0, false, now);
    for (size_t i = 0; i < g.shots.size(); i++) put(key(Shot, (int)i), g.shots[i].pos, 0, false, now);
    if (g.girderOn && g.phase == Phase::Aim) put(key(Girder, 0), g.girder, 0, false, now);
    if (g.factory.on) put(key(Factory, 0), g.factory.pos, 0, false, now);
    for (size_t i = 0; i < g.bubbles.size(); i++) put(key(Bubble, (int)i), g.bubbles[i].pos, 0, false, now);
    for (const Spot &s : extra) put(s.key, s.pos, 0, s.pool, now);
    // the details' pooled lights (0x5cccf0, offset and position: their mesh's bounding sphere, 0x5cd4f5); one without a model yet is a slot only
    size_t phantom = 0;
    const Terrain &t = g.terrain;
    for (size_t i = 0; i < t.objects.size(); i++) {
        const Terrain::Object &o = t.objects[i];
        if (o.model < (int)t.objC.size() && t.objR[o.model] > 0) put(key(Detail, (int)i), Vector3Transform(t.objC[o.model], o.m), t.objR[o.model] * Vector3Length({o.m.m0, o.m.m1, o.m.m2}) * 20, true, now);
        else phantom++;
    }
    entries.swap(now);
    at.clear();
    for (size_t i = 0; i < entries.size(); i++) at[entries[i].key] = i;
    const size_t slots = entries.size() + phantom;
    // 0x47cbf0: the new entries' first update, a ray each, from the opposite side (0x47b760)
    for (Entry &e : entries)
        if (e.fresh) cast(t, e, true), e.fresh = false, smooth(e);
    // then 30 slots from the cursor, one ray at most, for an entry whose hit cell is gone; and if none was cast, one forced ray in turn
    bool cast1 = false;
    for (size_t n = 0, start = cursor; slots && n < 30; n++) {
        cursor = (cursor + 1) % slots;
        if (cursor < entries.size()) {
            Entry &e = entries[cursor];
            if (!cast1 && due(t, e)) cast(t, e, false), cast1 = true;
            smooth(e);
        }
        if (cursor == start) break;
    }
    for (size_t n = 0; slots && !cast1 && n < slots; n++) {
        forced = (forced + 1) % slots;
        if (forced < entries.size()) cast(t, entries[forced], false), smooth(entries[forced]), cast1 = true;
    }
}
