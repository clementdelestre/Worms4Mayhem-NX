// W4M Assisted Shot (PayloadWeaponLogicEntity 0x583d40, called from the payload factory 0x585330 for a human team's Parabolic payload,
// the Landmine excepted): the launch is tried rotated by every step of the scheme's AssistedShotTweaks and the best-scored one is kept.
#include "ai.h"
#include "script.h"
#include "raymath.h"

namespace {
// LOCAL.XOM AssistedShot.Beginner / Intermediate / Pro [data]; ChangeInPower in W4M units/ms, the angles in rad
struct Tweaks { float pitch, yaw, power; int powerChecks, pitchChecks, yawChecks; };
constexpr Tweaks LEVELS[3] = {{0.06f, 0.06f, 0.017f, 1, 2, 2}, {0.04f, 0.04f, 0.015f, 1, 1, 1}, {0.015f, 0.015f, 0.01f, 1, 1, 1}};
constexpr float UNITS = 20, MS_TO_S = 1000;  // W4M units per m; units/ms to m/s: x 1000 / 20

float d2(Vector3 a, Vector3 b) { return Vector3DistanceSqr(a, b) * UNITS * UNITS; }  // squared W4M units
}  // namespace

Vector3 Game::assistShot(const WeaponDef &wd, Vector3 from, Vector3 dir, Vector3 vel) const {
    int lvl = scriptAssistLevel(*this);
    const Tweaks &tw = LEVELS[lvl < 0 ? assistLvl : lvl];
    const float speed = Vector3Length(vel), lo = launchSpeed(wd, 0), hi = wd.speed, step = tw.power * MS_TO_S / UNITS;

    // 0x584261..0x58434b: the unmodified speed, then per check lower and higher ones inside [BasePower, BasePower + MaxPower]
    std::vector<float> powers{speed};
    for (int i = 0; i < tw.powerChecks; i++) {
        const float off = i * step;
        if (lo <= speed - off) powers.push_back(speed - off);
        if (off + speed <= hi) powers.push_back(off + speed);
    }

    struct Target { Vector3 pos; float hp; bool foe; };
    std::vector<Target> targets;  // 0x4a46f0 / 0x4a48e0: every standing worm's feet, the shooter's own included
    const int mine = alliance(worms[current].team);
    for (const Worm &w : worms)
        if (w.alive && !surrendered[w.team]) targets.push_back({{w.pos.x, w.pos.y - R, w.pos.z}, (float)std::max(0, w.hp), alliance(w.team) != mine});
    std::vector<Vector3> mines, drums;
    for (const Object &o : objects)
        if (!o.dead && o.type == Object::Mine) mines.push_back(o.pos);
        else if (!o.dead && o.type == Object::Barrel) drums.push_back(o.pos);

    // 0x58472c..0x584e53: the score of a launch whose first contact is `e`
    auto score = [&](Vector3 e) {
        float s = 0, dmg = 0;
        for (const Target &t : targets) {
            const float dsq = d2(e, t.pos);
            if (!(dsq < 10000)) continue;
            bool line = false;  // a mine in front of the target as seen from the contact, within 200 units of it
            for (Vector3 m : mines) {
                const Vector3 a = Vector3Subtract(t.pos, e), b = Vector3Subtract(m, t.pos);
                if (Vector3LengthSqr(a) == 0 || Vector3LengthSqr(b) == 0) continue;
                if (Vector3DotProduct(Vector3Normalize(a), Vector3Normalize(b)) >= 0.8f && d2(t.pos, m) < 40000) line = true;
            }
            const float k = (100 - sqrtf(dsq)) * t.hp * 0.01f * 0.01f;
            if (line) s += t.foe ? 0.6f : -0.6f;
            dmg += t.foe ? k : -k;
        }
        const double blast = (double)dmg * 0.6;
        s = (float)(s + blast);
        float chain = 0;
        for (Vector3 dr : drums) {  // each drum within 100 units of the contact adds the target score again (0x584e2e: [esp+0x64], not a drum term)
            const float dsq = d2(e, dr);
            if (!(dsq < 10000)) continue;
            chain += (100 - sqrtf(dsq)) * 0.01f;
            for (const Target &t : targets) {
                const float tsq = d2(dr, t.pos);
                if (!(tsq < 10000)) continue;
                const float k = (100 - sqrtf(tsq)) * t.hp * 0.01f * 0.01f;
                chain += t.foe ? k : -k;
            }
            s = (float)(s + blast);
        }
        return chain * 0.2f + s;
    };

    // 0x584575..0x58468f: the launch direction rotated by A (the ChangeInYaw sweep, about z) and B (ChangeInPitch, about y), speed p;
    // the unmodified launch (score 0) is the first record and a later one wins only with a strictly higher score
    const float aStep = tw.yaw / tw.yawChecks, bStep = tw.pitch / tw.pitchChecks;
    Vector3 best = vel;
    float bestScore = 0;
    for (float p : powers)
        for (float a = -aStep * tw.yawChecks; a <= tw.yaw; a += aStep) {
            const float ca = cosf(a), sa = sinf(a);
            for (float b = -bStep * tw.pitchChecks; b <= tw.pitch; b += bStep) {
                const float cb = cosf(b), sb = sinf(b);
                const Vector3 d = {dir.x * (cb * ca) + dir.y * sa + dir.z * -sb, dir.y * ca + dir.x * -sa, dir.x * sb + dir.z * cb};
                const Vector3 v = Vector3Scale(d, p);
                if (const float s = score(Ai::firstEvent(*this, wd, from, v)); bestScore < s) bestScore = s, best = v;
            }
        }
    return best;
}
