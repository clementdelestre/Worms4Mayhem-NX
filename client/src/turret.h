#pragma once
#include "sim.h"

// W4M NewSentryGunGraphicEntity (0x56bb40): the deployed Sentry Gun's mesh, aim, clips, tracers and sounds, driven by the sim's turret states
namespace Turret {
void event(const Game &g, const GameEvent &e);  // GameLogic.Turn.Started: the resting turrets wake up
void update(const Game &g, float dt, Vector3 hand);  // once per frame; hand: the active worm's WeaponLocator, where a new turret starts
bool draw(const Game &g, const Object &o);  // the posed mesh; the caller picks its team colour clip
void clear();
}
