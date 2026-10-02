# Weapons spec, aligned with Worms 4: Mayhem

Reference: the game data (`Data/Tweak/WEAPTWK.XOM`, `TWEAK.XOM`, help texts in `English.xom`), cross-checked with the Worms wiki and Steam guides. When sources disagree, the game data wins. Details, sources and implementation status are in [weapons-audit.md](weapons-audit.md).

Common rules:
- Listed damage is the **maximum**, at the centre of the explosion. It falls off with distance.
- **Retreat time** starts at the shot, while the projectile flies, once the weapon's `PostLaunchDelay` has elapsed (0.5 s for the bazooka and grenades, 1 s for the shotgun, 2 s for the sniper). It equals the game style's `LandTime` (5 s in Standard, 0 in Pro and Strategy, 8 in Family), unless the weapon forces it (`RetreatTimeOverride`: 5 s for the dynamite and mine, 0 for strikes, abduction, flood, old woman, Scouser and Starburst; values from `weapons.json`). The player can move but no longer attack. Steering a missile or a piloted animal locks the worm.
- Some weapons are **locked** for the first turns (game style `Delay`, counted in the team's turns): in Standard, air strike 5, banana bomb 8, holy grenade 3, missile 2, super sheep 5, Icarus potion 2, binoculars 2, Weapon Factory weapons 3.
- The turn ends only once every projectile has landed and every explosion has finished. HP are then counted down.

## 1. Ballistic and direct fire

**Bazooka**
- First-person aim. Hold fire to charge the power gauge.
- Affected by wind. Explodes on impact.
- Max damage: 50 HP.

**Homing Missile**
- Not affected by wind.
- Target picked in sky view (fire locks the target), then normal aim and charged shot.
- Not affected by gravity: 1.25 s in a straight line, 4 s homing on the target, then 5 s in a straight line; it vanishes after 10.25 s.
- Max damage: 50 HP.

**Sniper Rifle**
- The zoom scope shows only in aim mode (L or ZL held).
- Straight trajectory, not affected by wind or gravity. One shot.
- Damage: 40 HP, with strong knockback.

**Shotgun**
- Free first-person aim. 2 shots per turn, and the worm can move between them.
- Damage: 25 HP per shot.

**Poison Arrow**
- First-person bow aim. Affected by wind.
- Impact damage: 25 HP (`WormImpactDamage`, the only weapon that has one; no explosion).
- The target is poisoned and loses 10 HP per turn, never dropping below 1 HP.
- Poison does not spread to other worms. Only a health crate cures it.

## 2. Grenades and explosives

**Grenade**
- First-person aim. Fuse adjustable from 1 to 5 s with the D-pad up/down, 3 s by default, remembered per team.
- Bounces, and explodes exactly when the countdown ends.
- Max damage: 55 HP.

**Cluster Grenade**
- Fuse adjustable from 1 to 5 s.
- On explosion, it releases 4 fragments (`NumBomblets` 4; the help text and the wiki say 5).
- Damage: 15 HP for the explosion, 15 HP per fragment.

**Holy Hand Grenade**
- First-person aim. No fuse setting.
- Bounces very little. Once stopped, it plays the "Hallelujah" then explodes 2 s later.
- Max damage: 80 HP, over a large radius.

**Banana Bomb**
- Fuse adjustable from 1 to 5 s. Bounces a lot.
- On explosion, it releases 5 secondary bananas.
- Damage: 50 HP for the explosion, 60 HP per banana.

**Dynamite**
- No aim: it is dropped at the worm's feet.
- Fixed 7 s fuse; the seconds left show over the stick for the last 5 s (W4M IsFuseDisplayed 1, PayloadGraphicEntity 0x57b1e0). The player can run away meanwhile (walk, jump), but no longer attack.
- The turn ends at the explosion.
- Max damage: 75 HP.

**Gas Canister**
- **Fixed** 5 s fuse (not adjustable).
- The explosion does no direct damage, but releases a 5 m radius cloud that lasts 8 s and drifts with the wind. Any worm passing through is poisoned (10 HP per turn).

**Mine**
- Placed on the ground, it arms, then explodes when a worm comes near, after a 1 to 5 s delay.
- 10 % of mines are duds: when the delay ends, they make a small puff of smoke and stay inert for good.
- Max damage: 40 HP.

> **Removed from the spec:** the Sticky Bomb does not exist in W4M. It comes from other games in the series.

## 3. Melee

For each hit below: direct contact, facing the target.

**Prod**
- No damage (0 HP). Pushes the target slightly.

**Baseball Bat**
- 30 HP damage.
- Sends the target flying in the aimed direction, at high speed.

**Fire Punch**
- 30 HP damage.
- The worm leaps straight up, fist on fire, digging through the terrain above it.
- The target is thrown into the air.

**Tail Nail**
- 15 HP damage. Drives the target into the ground.
- The nailed target can no longer walk or jump, nor use a utility, an animal or a melee weapon. It can turn and fire.
- It is freed when an explosion digs out the ground at its feet.

> **Removed from the spec:** the Sword and Shield does not exist in W4M. Damage reduction belongs to the Armour (see §5).

## 4. Air strikes and specials

**Air Strike**
- Impact point picked in sky view; the plane crosses the view from left to right, which is oriented by turning the camera.
- A visible bomber drops 6 bombs in a line, one every 333 ms, 2.5 m apart, from 3 gaps before the target to 2 past it; they keep the plane's speed and fall under reduced gravity. The guides say 5.
- Damage: 25 HP per bomb.

**Bovine Blitz (Super Airstrike)**
- Pick the pass point; the plane comes in along the view's direction, then it is **piloted** with the stick for 14 s.
- Each fire press drops a cow, 3 at most, at least 0.8 s apart. The cows descend slowly (parachute) and explode on impact.
- Max damage: 80 HP per cow.

**Fatkins Strike**
- Drop point picked.
- A bomber drops him; the giant worm falls on the target and bounces 3 times. Each bounce makes a large crater.
- Max damage: 75 HP.

**Concrete Donkey**
- Drop point picked.
- It falls from the sky and pounds the terrain repeatedly (it rises 3.5 m in 0.75 s between hits), until it sinks in the water or after 8 s (`LifeTime`).
- Max damage: 80 HP per impact.

**Alien Abduction**
- Target picked in sky view. The saucer arrives, sucks up every living worm within 9.35 m (horizontal) of its axis, nearest first, then spits them out one by one at their starting point (±2 m).
- Each worm spat out loses half its HP and becomes "abducted": at each end of turn where it was not hurt, its HP are rerolled at random between 0 and 99 (0 kills it), and it teleports now and then while it moves. Poison or a health crate cures it.
- Nobody in range: the saucer leaves (AbductFail).

**Sheep**
- Launched on the ground, it walks straight ahead, jumping over obstacles.
- Fire makes it explode, otherwise it explodes on its own.
- Max damage: 75 HP.

**Super Sheep**
- Launched on the ground, it walks. A fire press turns it into a flying sheep, piloted with the stick in third person. If it has not taken off after 5 s, it explodes.
- Another press, or contact with the terrain or a worm, makes it explode. Otherwise it explodes on its own after 25 s of flight.
- Max damage: 75 HP.

**Old Woman**
- She walks. She is steered with the stick and explodes on command with fire.
- Each worm she bumps, teammates included (except the thrower), loses 5 ammo, one at a time, each from a random weapon (preferably one the thief lacks). The thrower's team gets them; an infinite stock is not touched. After each theft she stands still for 0.8 s, then walks back the way she came.
- Fully under water she stops and explodes 2 s later.
- 30 s fuse.
- Max damage: 75 HP.

**Inflatable Scouser**
- He walks and is steered with the stick. He swallows the first worm he touches, inflates and carries it into the air with the wind.
- After 5 s, he bursts: 40 HP for the swallowed worm, which falls. If he swallowed nothing, he bursts without damage.

**Flood**
- The water level rises by about 2.15 m.

**Sentry Gun**
- Placed on the ground, it stays active during enemy turns.
- It fires a burst at a visible enemy worm within 15 m.
- Damage: 25 HP per burst. Reload: 10 s.

## 5. Utilities and movement

**Jetpack**
- Fuel is used only while thrusting: 7,500 ms (7.5 s of thrust), not the "30" of the original spec.
- Landing ends the flight like a fall: a fast landing hurts.
- Does not end the turn. In flight (and landed, while fuel remains), only the dynamite, mine and sheep can be taken in hand besides the jetpack, as a secondary weapon, dropped with the other trigger. Any other weapon picked ends the jetpack.
- After the attack, retreat time starts. The worm can still fly with the remaining fuel.

**Ninja Rope (grapple)**
- First-person aim to anchor the grapple. The worm swings, and reels in or out at 10 m/s, between 0.5 m and 22.5 m.
- 5 grapple shots per turn.
- The grapple also hooks crates, mines and barrels; they hang on the rope from the worm.
- On the rope, only the dynamite, mine and sheep can be dropped (secondary weapon, with fire); any other weapon lets go of the rope.
- Does not end the turn.

**Parachute**
- Opened manually while falling. If selected, it opens on its own during a dangerous fall.
- Drifts with the wind. It closes on touching the ground.

**Girder**
- Opaque 3D preview (white, red if the spot is taken), moved in 0.3 m steps, at most 15 m from the worm on each axis. No rotation on PC. The placed girder (a 4 × 4 m slab on two legs) is ordinary terrain: explosions dig into it. 45 girders at most per match.
- It ends the turn, unless the game style forbids it ("girders don't end the turn" option).

**Select Worm, Skip Go, Surrender**
- As in the game. Skip Go: the turn clock stops, the worm stays frozen 3 s (W4M PostLaunchDelay 3000), then the turn ends.

**Armour**
- The worm takes only 25 % of explosion and bullet damage, and half the knockback, for the rest of its life. It applies as soon as the crate is collected.
- Poison, falls, melee and the Scouser are not reduced. Does not end the turn.

**Binoculars**: in first-person view, firing at a worm or the terrain computes the bazooka shot (no wind), shown after 4 s; uses nothing, does not end the turn.

**Bubble Trouble**: 0.4 s after fire, a 2.1 m radius bubble is set next to the worm and drops until it meets land. Shots from outside bounce off it, bullets stop on it unless the shooter is inside, worms walk through it, a worm inside ignores outside explosions; an inside explosion pops it. It lasts 6 ends of turn. It does not end the turn, once per turn.

**Icarus Potion**: cures poison and the abduction, but heals nothing. After a jump, each Jump press flaps the wings, within a 250 ms window every 500 ms; the stick steers. No fall damage in flight.

**Double Damage, Crate Spy** (crates only): double explosions and hits for the rest of the turn / show the team the content of crates for the rest of the match.

The bridge kit and the Buffalo of Lies are only dead data in W4M (no logic): not done.

> **Removed from the spec:** the teleporter is not a player utility in W4M; the game has fixed telepads on some maps. We keep it as a bonus, but it is not offered by default.
