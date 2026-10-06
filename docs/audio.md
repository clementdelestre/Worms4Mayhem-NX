# Audio (ours)

What `client/src/audio.h` / `audio.cpp` play, and where each sound comes from. W4M side: `WormsX.fev` (FMOD Ex FEV1) decoded by
`tools/w4m-re/fev.py` (`docs/w4m/audio.md` §12); the sample files are converted by `tools/w4m-import` (`docs/import.md`).
Status: the gains, loop flags, 3D ranges and max playbacks are **data** (FEV, hand-copied into `DEFS`); the table row says when not.

## Rules

- **Files**: `sfx/<file>.ogg`, `<file>_2.ogg`… (up to 12 variants, one picked at random, FEV wave weights when given, e.g. ScouserHeld
  100/300/100). `assets/` (imported) is tried first, then `romfs/` (CC0). A missing file is silent. Per-weapon fire sounds
  (`Shotgun` .. `SuperSheepFire`) fall back to `Fire` when not imported.
- **Gain**: event + sound definition + category dB, × the call's volume.
- **3D**: FMOD linear rolloff: full inside min, silent past max (20 units = 1 m); FEV "log" events (`Def::log`: SheepHeld,
  FireWorksExplosion) min / distance, constant past max [data: FEV rolloff; FMOD Ex log model]; pan from the listener's right; the
  listener is the drawn camera (`Audio::listen`). 2D events ignore the position.
- **Trigger delay**: `Def::delay` (sounddef +64/+66, data): each sound starts min + rand % (max − min) ms late (fmod_event 0x10038400,
  disasm), queued in `Audio::update`: MissileLoop 1000, OldWomenFootsteps 120, SheepHeld 1000–3000, ScouserHeld 200–1200, OldWomanHeld 200–1600.
- **Fade-in**: a `play` of an event with an FEV fade (`Def::fade`) ramps its volume over it (Teleport 0.35 s, RainLoop 2 s).
- **Max playbacks**: each variant is loaded `max` times (aliases); past `max` voices playing, the oldest is cut (FMOD steal oldest).
- **Kinds of call**: `play` (one shot), `hold` (one W4M event instance from the rising edge, cut at the falling edge: held weapons,
  charge sounds; a spawning def (`Def::spawn`, FEV "oneshot" instance) starts its next sound once the last has ended), `loop` (replayed while on, FEV fade in / out when it has one), `equip` (W4M WeaponAccessoryEntity 0x5950c0: the weapon's
  WEAPTWK EquipSfx; none for Sheep, Starburst, Fire Punch, Prod, Armour, Binoculars, Teleport, Change Worm, Skip Go; Weapon Factory
  weapons use the Bazooka's, as kWeaponFactoryWeapon).
- **Voices**: `voices/<bank>/<line>.ogg`, one bank per team (`setTeamVoice`, picked in team setup; default team % banks), banks
  under `assets/voices/` else `romfs/voices/`, loaded lazily (`preloadVoices` at match start). Speech is 3D 0.5–50 m at 0 dB, one
  playback per line; SadSigh and Yawn −2.5 dB, 0.5–22.5 m (FEV `Speech/*`). A bank without a line falls back along `FALLBACK`
  (the CC0 banks hold only fire, hurt, death, victory, jump, idle). Lines and their acting triggers: `worm-reactions.md`.
- **Music**: `music/<track>.ogg`, the map theme or `theme` (frontend), looped but `victory`; −6 dB for theme and victory, −9 for
  arabian, wildwest, suddendeath, −12 for the others (FEV sound definition + category music); fades in over 1 s (W4M Music.FadeIn
  0x7290b4 +0.01 a frame). `theme` (frontendmusic/femusic) fades out over 2 s (its FEV fade-out) when stopped or replaced. The `cheer`
  crowd loop stops when the track changes.
- **Match launch and loading** [ours, after docs/w4m/frontend.md §17 "Pre-match and loading screen"]: `Loading::frame` plays
  FeGrenade 0.25 s into the pre-match intro, with the logo's pop-in (WXFE.PreStart's Audio_Incoming); at the loading screen
  (1.75 s) `theme` stops with its 2 s fade-out (FrontEndService shutdown); the map track fades in once the match shows. The
  loading clock is the wall clock (`GetFrameTime`, unclamped), as W4M's renders take the elapsed ms, so a long frame cannot
  put the animation behind the sounds. `Audio::update` runs every loop iteration, Loading included; the blocking GL steps
  start only after the theme's fade-out (`Loading::ready`), so no stream is playing while they hold the main thread.

## Sounds (`enum class Sfx`)

Generated from `SFX_NAMES` / `DEFS` (audio.cpp), the `SFX` table of `tools/w4m-import/src/main.rs` and the `Sfx::` uses in
`client/src/*.cpp` (function names). Fe* are frontend sounds (W4M kAUDIO_*), played from `ui.cpp` under the alias `S::`.

| Sfx | file | W4M event (FEV) | dB | loop | 3D range m | max | imported from (bank: subsounds) | played from |
|---|---|---|---|---|---|---|---|---|
| Explosion | `explosion` | global/ExplosionRegular | -3 |  | 0.5–50 | 4 | global: ExplosionRegular1, ExplosionRegular2, ExplosionRegular3 | main.cpp `onEvent`; fx.cpp EmitterSoundFX of WXP_ExplosionX_InnerCloud / WXP_Explosion_Small_Cloud |
| BigExplosion | `big_explosion` | weapons/ExplosionLarge | -12 |  | 0.5–40 | 1 | weapons: ExplosionLarge1 | main.cpp `onEvent`; fx.cpp EmitterSoundFX of WXP_ExplosionX_InnercloudLarge |
| Fire | `fire` | weapons/RocketRelease | -6 |  | 2D | 1 | weapons: RocketRelease | main.cpp `onEvent` |
| Bounce | `bounce` | weapons/GrenadeBounce | -2 |  | 0.5–60 | 1 | weapons: GrenadeImpact1, GrenadeImpact2, GrenadeImpact3 | main.cpp `onEvent` |
| Splash | `splash` | weapons/SplashHeavy | 0 |  | 0.5–70 | 2 | weapons: SplashHeavy1, SplashHeavy2, SplashHeavy3 | main.cpp `onEvent` |
| Jump | `jump` | (none: CC0 jump) | 0 |  | 2D | 1 | — | main.cpp `onEvent` |
| Sheep | `sheep` | weapons/SheepBaa | -3 |  | 0.5–25 | 1 | weapons: SheepBaa | main.cpp `onEvent` |
| Holy | `holy` | weapons/Hallelujah | 0 |  | 2D | 1 | weapons: Hallelujah | main.cpp `onEvent` |
| TurnStart | `turn_start` | weapons/HudAlert | -10 |  | 2D | 1 | weapons: HudAlert | main.cpp `onEvent` |
| Tick | `tick` | weapons/ClockFast | -2 | yes | 2D | 1 | weapons: ClockFast | main.cpp `main` (HudClockEntity 0x5efd80: loops at 5 s and under) |
| Shotgun | `shotgun` | weapons/ShotgunFire | -5 |  | 2D | 1 | weapons: Shotgun1, Shotgun2 | main.cpp `onEvent` |
| Airstrike | `airstrike` | weapons/Bomber | -9 | yes, fade 0.5 s | 0.5–60 | 1 | weapons: Bomber | main.cpp `onEvent`, main.cpp `drawBomber` |
| Donkey | `donkey` | weapons/ConcreteDonkeyRelease | -1 |  | 0.5–100 | 1 | weapons: DonkeyBray | main.cpp `onEvent` |
| Rope | `rope` | weapons/NinjaRopeFire | 0 |  | 0.5–25 | 1 | weapons: NinjaRopeFire | main.cpp `onEvent` |
| Teleport | `teleport` | weapons/Teleport | -8 |  | 0.5–25 | 1 | weapons: TeleportOut | main.cpp `onEvent` |
| BatSwing | `bat_swing` | weapons/BaseballBarSwing | 0 |  | 2D | 1 | weapons: BaseballBatSwing | main.cpp `onEvent` |
| FirePunch | `fire_punch` | weapons/FirePunch | -6 |  | 0.5–25 | 1 | weapons: FirePunch | main.cpp `onEvent` |
| Prod | `prod` | weapons/Prod | -4 |  | 2D | 1 | weapons: Prod | main.cpp `onEvent` |
| Sniper | `sniper` | weapons/SniperRifleFire | -3 |  | 2D | 1 | weapons: SniperFire | main.cpp `onEvent` |
| Bow | `bow` | weapons/BowRelease | 0 |  | 0.5–60 | 1 | weapons: BomTwang | main.cpp `onEvent` |
| Homing | `homing` | weapons/MissileLoop | 0 |  | 5–75 | 1 | weapons: MissileWhistle | main.cpp `main` (`Audio::hold` on the live Bazooka / Homing shot, see Notes) |
| OldWomanFire | `old_woman` | weapons/OldWomenLaunch | 0 |  | 0.5–60 | 1 | weapons: OldWomanLaunch, OldWomanMutter1, OldWomanMutter2, OldWomanMutter3, OldWomanMutter4, OldWomanMutter5 | main.cpp `onEvent` |
| ScouserFire | `scouser` | weapons/ScouserLaunch | 0 |  | 0.5–60 | 1 | weapons: ScouserLaunch, ScouserJump1, ScouserJump2, ScouserJump3 | main.cpp `onEvent` |
| SentryPlace | `sentry_place` | weapons/SentryGunHeld | -8 | yes | 0.5–20 | 1 | weapons: SentryGunHeld | main.cpp `onEvent` |
| SentryFire | `sentry_fire` | weapons/SentryGun | -3 | yes | 0.5–100 | 1 | weapons: SentryGunLoop | main.cpp `onEvent` |
| Dynamite | `dynamite` | weapons/FuseLoop | -4 | yes | 0.5–25 | 1 | weapons: Fuse | main.cpp `onEvent`, main.cpp `main` |
| Gas | `gas` | weapons/GasLoop | -16 |  | 0.5–15 | 1 | weapons: GasLoop | main.cpp `onEvent` |
| Abduction | `abduction` | weapons/AlienUfoBeamStart | 0 |  | 0.5–100 | 1 | weapons: AlienUFOBeamStart | main.cpp `onEvent`, main.cpp `updateUfo` |
| Flood | `flood` | weapons/RainLoop | -6 | yes | 2D | 1 | weapons: RainLoopAmb | fx.cpp `rainUpdate`: `Audio::loop` while a kRain runs (weather, map, Flood rain); the Flood's fire plays nothing (WEAPTWK has no LaunchSfx) |
| Parachute | `parachute` | weapons/ParachuteLoop | -2 |  | 0.5–25 | 1 | weapons: ParachuteOpen | main.cpp `onEvent` |
| MineBeep | `mine_beep` | weapons/MineArmLoop | 0 | yes | 5–25 | 1 | weapons: MineArmLoop | main.cpp `main` |
| CrateLand | `crate_land` | weapons/CrateSpawn | -4 |  | 0.5–60 | 1 | weapons: CrateSpawn | main.cpp `onEvent` |
| Pickup | `pickup` | weapons/PickupWeapon | -11 |  | 0.5–25 | 1 | weapons: PickUpAmmo, PickUpHealth, PickUpUtility | main.cpp `onEvent` |
| SuperSheepFire | `super_sheep` | weapons/WingFlap | -3 |  | 0.5–25 | 1 | weapons: WingFlap1, WingFlap2, WingFlap3 | main.cpp `onEvent` |
| Step | `step` | weapons/OldWomenFootsteps | -6 |  | 0.5–60 | 1 | weapons: OldWomenFootstep1, OldWomenFootstep2, OldWomenFootstep3, OldWomenFootStep4, OldWomenFootstep5 | main.cpp `animateWorms` |
| Land | `land` | weapons/Thud | 0 |  | 0.5–25 | 2 | weapons: Thud1, Thud2, Thud3, Thud4 | main.cpp `animateWorms` |
| HpTick | `hp_tick` | global/click3 | 0 |  | 2D | 1 | global: Click3 | ui.cpp `Hud::trackHp`; `Pause::update` (popups' MenuGoingAway `WXMsg.PlaySample$click3`: the pause menu left for the briefing, the briefing closed) |
| CrateImpactHealth | `crate_impact_health` | weapons/CrateImpactHealth | -2 |  | 0.5–60 | 1 | weapons: CrateHitHealth | main.cpp `onEvent` |
| CrateImpactWeapon | `crate_impact_weapon` | weapons/CrateImpactWeapon | -2 |  | 0.5–60 | 1 | weapons: CrateImpactWeapons | main.cpp `onEvent` |
| CrateImpactUtil | `crate_impact_util` | weapons/CrateImpactUtil | -3 |  | 0.5–60 | 1 | weapons: CrateImpactUtil | main.cpp `onEvent` |
| Cheer | `cheer` | cheer/cheer | -12.9 | yes | 2D | 1 | cheer: CrowdCheer | main.cpp `main` (game over won: `won()`, as music/victory) |
| FeHighlight | `fe_highlight` | global/Highlight | 0 |  | 2D | 1 | global: Highlight | ui.cpp `P`, ui.cpp `Hud::input` |
| FeChange | `fe_change` | global/click2 | 0 |  | 2D | 1 | global: Click2 | ui.cpp `P` |
| FeClick | `fe_click` | frontendsfx/click | 0 |  | 2D | 1 | frontendsfx: Click | ui.cpp `P`, ui.cpp `Frontend::frame`, ui.cpp `Hud::input` |
| FeCancel | `fe_cancel` | frontendsfx/Cancel | 0 |  | 2D | 1 | frontendsfx: Cancel | ui.cpp `P`, ui.cpp `Hud::input` |
| FeError | `fe_error` | global/FEError | 0 |  | 2D | 1 | global: FEError | main.cpp `main`, ui.cpp `Hud::input` |
| FeType | `fe_type` | global/Typewriter | 0 |  | 2D | 1 | global: Typewriter | ui.cpp `Frontend::frame` |
| FePage | `fe_page` | frontendsfx/PageTurn | 0 |  | 2D | 1 | frontendsfx: PageTurn | ui.cpp `missionMenu` |
| FePopupIn | `fe_popup_in` | global/In_ScaleY | 0 |  | 2D | 1 | global: In_ScaleY | ui.cpp `enterSfx`, ui.cpp `Hud::input`, ui.cpp `Pause::update` |
| FePopupOut | `fe_popup_out` | frontendsfx/Out_ScaleY | 0 |  | 2D | 1 | frontendsfx: Out_ScaleY | ui.cpp `enterSfx`, ui.cpp `Hud::input`, ui.cpp `Pause::update` |
| FeNextIn | `fe_next_in` | frontendsfx/In_Next | 0 |  | 2D | 1 | frontendsfx: In_Next | ui.cpp `enterSfx` |
| FeNextOut | `fe_next_out` | frontendsfx/Out_Next | 0 |  | 2D | 1 | frontendsfx: Out_Next | ui.cpp `Frontend::go` |
| FePrevIn | `fe_prev_in` | frontendsfx/In_Prev | 0 |  | 2D | 1 | frontendsfx: In_Prev | ui.cpp `enterSfx`, ui.cpp `Pause::update` |
| FePrevOut | `fe_prev_out` | frontendsfx/Out_Prev | 0 |  | 2D | 1 | frontendsfx: Out_Prev | ui.cpp `Frontend::go` |
| FeBounce | `fe_bounce` | frontendsfx/In_BigBounce | 0 |  | 2D | 1 | frontendsfx: In_Bigbounce | ui.cpp `enterSfx` |
| FeSlide | `fe_slide` | frontendsfx/In_SlideX | 0 |  | 2D | 1 | frontendsfx: In_SlideX | ui.cpp `enterSfx` |
| FeNet | `fe_net` | frontendsfx/In_Net | 0 |  | 2D | 1 | frontendsfx: In_Net | ui.cpp `enterSfx` |
| FeCustom | `fe_custom` | frontendsfx/In_Custom | 0 |  | 2D | 1 | frontendsfx: In_Custom | ui.cpp `enterSfx` |
| FeSoundVid | `fe_soundvid` | frontendsfx/In_SoundVid | 0 |  | 2D | 1 | frontendsfx: In_SoundVid | ui.cpp `enterSfx` |
| FeController | `fe_controller` | global/In_Controller | 0 |  | 2D | 1 | global: In_Controller | ui.cpp `enterSfx`, ui.cpp `Pause::update` |
| FeFactory | `fe_factory` | frontendsfx/In_WeaponFactory | 0 |  | 2D | 1 | frontendsfx: In_WeaponFactory | ui.cpp `enterSfx` |
| FeBookIn | `fe_book_in` | frontendsfx/In_Book | 0 |  | 2D | 1 | frontendsfx: In_Book | ui.cpp `missionMenu` |
| FeBookOut | `fe_book_out` | frontendsfx/Out_Book | -INFINITY |  | 2D | 1 | frontendsfx: Out_Book | ui.cpp `missionMenu` |
| FeGrenade | `fe_grenade` | frontendsfx/grenade | 0 |  | 2D | 1 | frontendsfx: Grenade | loading.cpp `Loading::frame` |
| FeWormpot | `fe_wormpot` | frontendsfx/In_Wormpot | 0 |  | 2D | 1 | frontendsfx: In_WormPot | ui.cpp `enterSfx` |
| FeSpeech | `fe_speech` | frontendsfx/In_Speech | 0 |  | 2D | 1 | frontendsfx: In_Speech | ui.cpp `missionMenu` |
| WormpotSpin | `wormpot_spin` | frontendsfx/WormPotLoop | 0 | yes | 2D | 1 | frontendsfx: WormPotLoop | ui.cpp `Frontend::wormpot` |
| WormpotStop | `wormpot_stop` | frontendsfx/WormPotStop | 0 |  | 2D | 1 | frontendsfx: WormPotStop | ui.cpp `Frontend::wormpot` |
| HolyBoom | `holy_boom` | weapons/HolyGrenadeExplosion | -1 |  | 2D | 1 | weapons: HolyGrenadeEx | main.cpp `onEvent` |
| HolyHeld | `holy_held` | weapons/HolyGrenadeHeld | -10 | yes | 0.5–25 | 1 | weapons: HolyGrenadeHeld | main.cpp `main` |
| BombWhistle | `bomb_whistle` | weapons/BombWhistle | -14 |  | 0.5–40 | 6 | weapons: BombWhistle | main.cpp `onEvent` |
| CowFall | `cow_fall` | weapons/CowFall | -6 |  | 0.5–25 | 2 | weapons: CowFall, CowFall2, CowFall3 | main.cpp `onEvent` |
| PowerRocket | `power_rocket` | weapons/RocketPowerUp | -6 |  | 0.5–60 | 1 | weapons: RocketPowerUp | main.cpp `main` |
| PowerHoming | `power_homing` | weapons/HomingMissilePowerUp | -6 |  | 0.5–25 | 1 | weapons: HomingMissilePowerUp | main.cpp `main` |
| PowerBow | `power_bow` | weapons/BowCreak | -2 |  | 0.5–25 | 1 | weapons: BowCreak | main.cpp `main` |
| EquipAir | `equip_air` | weapons/AirEquip | -12 |  | 0.5–500 | 1 | weapons: AirEquip | `Audio::equip` for Airstrike, Super Airstrike, Fatkins Strike, Concrete Donkey, Alien Abduction |
| EquipBazooka | `equip_bazooka` | weapons/BazookaEquip | -12 |  | 0.5–500 | 1 | weapons: BazookaEquip | `Audio::equip` for Bazooka, Homing Missile, Sentry Gun |
| EquipBubble | `equip_bubble` | weapons/BubbleEquip | -12 |  | 0.5–500 | 1 | weapons: BubbleEquip | `Audio::equip` for Bubble Trouble |
| EquipDefault | `equip_default` | weapons/DefaultEquip | -12 |  | 0.5–500 | 1 | weapons: DefaultEquip | `Audio::equip` for Grenade, Cluster Grenade, Banana Bomb, Holy Hand Grenade, Poison Arrow, Dynamite, Gas Canister, Landmine, Old Woman, Baseball Bat, Tail Nail, Ninja Rope, Jetpack, Parachute, Girder, Surrender |
| EquipPotion | `equip_potion` | weapons/PotionEquip | -11 |  | 2D | 1 | weapons: PotionEquip | `Audio::equip` for Icarus Potion |
| EquipScouser | `equip_scouser` | weapons/ScouserArm | -7 |  | 0.5–25 | 1 | weapons: ScouserArm | `Audio::equip` for Inflatable Scouser |
| EquipShotgun | `equip_shotgun` | weapons/ShotgunEquip | -12 |  | 0.5–500 | 1 | weapons: ShotgunEquip | `Audio::equip` for Shotgun |
| EquipSniper | `equip_sniper` | weapons/SniperEquip | -12 |  | 0.5–500 | 1 | weapons: SniperEquip | `Audio::equip` for Sniper Rifle |
| EquipUmbrella | `equip_umbrella` | weapons/UmbrellaOpen | -12 |  | 2D | 1 | weapons: UmbrellaOpen | `Audio::equip` for Flood |
| HeldSheep | `held_sheep` | weapons/SheepHeld | -9.2 |  | 0.5–25 | 1 | weapons: SheepBaa | main.cpp `main` |
| HeldSentry | `held_sentry` | weapons/SentryGunHeld | -8 | yes, fade 0.35 s | 0.5–20 | 1 | weapons: SentryGunHeld | main.cpp `main` |
| HeldScouser | `held_scouser` | weapons/ScouserHeld | 0 |  | 0.5–60 | 1 | weapons: ScouserHeld1, ScouserHeld2, ScouserHeld3 | main.cpp `main` |
| HeldOldWoman | `held_old_woman` | weapons/OldWomanHeld | -7 |  | 0.5–60 | 1 | weapons: OldWomenHeld1, OldWomanHeld2, OldWomanHeld3 | main.cpp `main` |
| LockOn | `lock_on` | weapons/LockOn | 0 |  | 2D | 1 | weapons: TargetAquired | main.cpp `main` |
| UfoAppearing | `ufo_appearing` | weapons/AlienUfoAppearing | 0 |  | 0.5–100 | 1 | weapons: AlienUFOAppearing | main.cpp `updateUfo` |
| UfoActive | `ufo_active` | weapons/AlienUfoActive | 0 |  | 0.5–100 | 1 | weapons: AlienUFOActive | main.cpp `updateUfo` |
| UfoBeamLoop | `ufo_beam` | weapons/AlienUfoBeamLoop | 0 | yes, fade 0.5 s | 0.5–100 | 1 | weapons: AlienUFOBeamLoop | main.cpp `updateUfo` |
| UfoEngine | `ufo_engine` | weapons/AlienUfoEngineLoop | 0 | yes, fade 0.5 s | 0.5–100 | 1 | weapons: AlienUFOEngine | main.cpp `updateUfo` |
| UfoTakeOff | `ufo_takeoff` | weapons/AlienUFOTakeOff | 0 |  | 2D | 1 | weapons: AlienUFOTakeOff | main.cpp `updateUfo` |
| BatImpact | `bat_impact` | weapons/BaseballBatImpact | 0 |  | 0.5–25 | 1 | weapons: BaseballBatImpact | main.cpp `onEvent` |
| BowImpact | `bow_impact` | weapons/BowImpact | 0 |  | 0.5–100 | 1 | weapons: BowImpact | main.cpp `onEvent` (`GameEvent::Arm`: the Poison Arrow's ArmSfxLoop, on every impact) |
| ExplosionBoxed | `explosion_boxed` | weapons/ExplosionBoxed | -2 |  | 2D | 4 | weapons: ExplosionBoxed1 | main.cpp `onEvent` (the arrow's Boom: DetonationSfx is empty, WXP_ExploArrow_RingDk's EmitterSoundFX) |
| BubbleInflate | `bubble_inflate` | weapons/BubbleMachineInflate | -2 |  | 0.5–25 | 1 | weapons: BubbleMachinePlace | main.cpp `onEvent` |
| BubbleWobble | `bubble_wobble` | weapons/BubbleMachineWobble | -2 |  | 0.5–25 | 1 | weapons: BubbleMachineWobble | main.cpp `onEvent` |
| BubbleLoop | `bubble_loop` | weapons/BubbleMachineLoop | -22 |  | 0.5–20 | 1 | weapons: Bubble1, Bubble2, Bubble3, Bubble4, Bubble5, Bubble6 | main.cpp `drawBubbles` |
| Fireworks | `fireworks` | global/FireWorksExplosion | 0 |  | 0.5–25 log | 1 | global: Firework1, Firework2, Firework3 (mode 2) | fx.cpp `tickEmitters` (EmitterSoundFX of WXPF_Whiteout, WXPF_RedGlow / RedBigGlow, WXP_StarburstTrailsB, at the emitter start) |
| Buffalo | `buffalo` | weapons/BuffaloOfLies | -2 |  | 0.5–25 | 1 | weapons: BuffaloOfLies | main.cpp `onEvent` (GameEvent::Mystery: the mystery crate reveal, BuffaloOfLiesGraphicEntity 0x551020) |
| Debris | `debris` | weapons/Debris | -12 |  | 0.5–100 | 1 | weapons: Debris1–4 (random without repeat) | main.cpp `onEvent` (GameEvent::Debris from `Game::blastLand`: W4M Land Explosion handler 0x473530 plays it at the blast once Land.Changed is set, 0x4736b2 [disasm]); pitch x 2^(4 u), u uniform in ±0.025 (FEV +08, `Def::pitchRand`) |
| Jetpack | `jetpack` | weapons/JetPack | -3 | loop | 0.5–60 | 1 | weapons: JetPack | main.cpp `jetAudio`: started at takeoff, volume ramp of JetpackUtilityLogicEntity 0x562530 (docs/weapons-audit.md Jetpack), stopped on landing / dry [disasm] |
| JetpackEnd | `jetpack_end` | weapons/JetPackEnd | -3 |  | 0.5–60 | 1 | weapons: JetPackEnd | main.cpp `jetAudio`: the loop's volume falls under 0.3 after a full burn, 3 s apart (0x562679) [disasm] |
| FireLoop | `fire_loop` | weapons/FireLoop | -10 | loop, fade out 0.5 s | 0.5–25 | 4 | weapons: FireLoop | fx.cpp map emitters (EmitterSoundFX of WXP_BonfireBase, WXP_SmallFlames, Campfire_1 `weapons/Fireloop`: FEV names fold case, fmod_event 0x10005110): `Audio::emitter`, one voice per emitter, steal oldest past 4 [data] |
| SteamLoop | `steam_loop` | weapons/SteamLoop | -15 | loop, fade out 0.5 s | 0.5–25 | 4 | weapons: SteamLoop | fx.cpp map emitters (WXP_Smoke_SteamyThick) |
| FliesLoop | `flies_loop` | weapons/FliesLoop | -12 | loop | 0.05–10 | 1 | weapons: FliesLoop | fx.cpp map emitters (WXP_OldLadyFlies) |
| ElecArc | `elec_arc` | weapons/ElecArc | -6 | loop | 0.5–15 | 1 | weapons: ElecArc | fx.cpp map emitters (WXPL_ElectricSpark) |
| ElectricArching | `electric_arcing` | weapons/ElectricArching | 0 | loop | 0.5–25 | 1 | weapons: ElectricArcing | fx.cpp map emitters (SndRadioFx) |
| StormCloud | `storm_cloud` | weapons/StormCloud | 0 | fade out 0.5 s | 0.5–80 | 4 | weapons: ThunderClap_1-5 | fx.cpp map emitters (WXPL_TowerClouds): played once when the emitter starts (0x5bb88e) |
| HoseIntoWater | `hose_into_water` | weapons/HoseIntoWater | 0 | loop | 0.05–35 | 1 | weapons: TapIntoWater | fx.cpp map emitters (Hose_Part1) |
| FloodRain | `flood_rain` | weapons/FloodRainLoop (RainLoop layer) | -2 | loop, `Def::env`: 0 → 1 over the 2 s fade-in, × 0.5, to 0 from 7.97 to 12.0 s | 2D | 1 | weapons: RainLoop | fx.cpp `tickEmitters` (EmitterSoundFX of WXP_StormClouds): `Audio::emitter`, cut with the emitter (FEV fade-out 0) [data] |
| FloodThunder | `flood_thunder` | weapons/FloodRainLoop (Thunder layer) | 0 |  | 2D | 1 | weapons: Thunder | started with FloodRain (`Def::layer`), sounddef delay 1500 ms, stopped with it [data] |
| FatkinsBounce | `fatkins_bounce` | weapons/FatkinsBounce | -3 |  | 2D | 1 | weapons: FatkinsBounce1, FatkinsBounce2 (mode 2) | main.cpp `onEvent` Bounce (WEAPTWK BounceSfx) |
| BananaBounce | `banana_bounce` | weapons/BananaBombImpact | -6 |  | 0.5–60 | 1 | weapons: BananaBombImpact | main.cpp `onEvent` Bounce (WEAPTWK BounceSfx of kWeaponBananaBomb) |
| TickSlow | `tick_slow` | weapons/ClockSlow | -2 | yes | 2D | 1 | weapons: ClockSlow | main.cpp `main` (6–15 s, volume min(1, (15 − s) 0.11), 0x5efc40) |
| FeScaleHit | `fe_scalehit` | global/In_Scalehitxy | 0 |  | 2D | 1 | global: In_Scalehitxy | ui.cpp `Pause::update`: WXFEP.MissionBriefing's Audio_Incoming (kAUDIO_In_ScaleHitXY), at its 200 ms Delay_Incoming [data] |

Notes from the code comments: `Jump` has no W4M event (CC0 file only); `Homing` (MissileLoop) loops in FEV but its Time envelope
ends it at 5.03 s, so it is one pass of the 5.85 s clip [ours]: started with a 1 s trigger delay when a Bazooka or Homing Missile shot exists, placed on the shot every frame, stopped at 5.03 s of flight or when the shot is gone (no 500 ms fade-out, the blast covers it); the Fire event plays RocketRelease (2D, -6 dB) for both [data + disasm, docs/w4m/audio.md "MissileLoop owners"]; `Parachute` is the Open layer of ParachuteLoop; `Pickup` uses PickupWeapon's −11 dB for all
three crate kinds (W4M PickupUtil −11, PickupHealthCrate −6);
`FeBookOut` has event volume 0 in W4M (silent); `BubbleLoop` plays one of Bubble1–6 per 500 ms spawn of WXP_Bubbles_Small (FEV spawn 500..500 on a oneshot instance, fmod_event 0x1001a3ec; the emitter starts its event once, 0x5bdcf4).

## Level movies (EFMV) and lip sync

W4M side: docs/w4m/acting.md §19 "Speech, lip-sync, sound banks". Files: docs/import.md (`efmv/`, `voices/*/lip.txt`).
- **Bank** [data]: `Audio::efmvLevel(mission bank)` at match load (main.cpp loader thread) loads `efmv/<level>/` (directory found
  case-insensitively, as FMOD's lookup) and `efmv/Failures/`; any other match unloads them. Gains, loop, 3D linear range and play
  mode per event from `events.txt`; max playbacks 1. Only English exists [data].
- **Hooks** [ours, on the movie player's events, docs/missions.md]: `GameEvent::MovieSound` -> `efmvSfx` (Looping: the one looping
  instance, stopped by `movie(false)`; else fire-and-forget, an FEV loop then never stops, as W4M); `MovieStart` / `MovieEnd` ->
  `movie(true / false)`: music × remaining / 500 ms, then × (1 − remaining / 2000 ms) [disasm 0x605720]. A missing event logs
  `AUDIO: EFMV sfx <name>: no such event` and is silent, as W4M.
- **Speech** [disasm, docs/w4m §19]: TriggerSpeech reaches the cast worm through `Acting::movie` -> `say(worm, "*Line")` ->
  `efmvSpeech` (the speaker's previous line cut, 0x59cc70). The `GameEvent::Speech` hook is not needed.
- **Time envelopes** [data]: `efmvUpdate` sets the volume at the parameter's position (rate × seconds, held at 1): the whistles' cuts,
  WaterLap2. The y values are 0 or 1, so the y-to-gain mapping only matters on WaterLap2's 0.4 s ramps (linear, assumed).
  JukeBoxTune's pitch envelope is not played: no movie triggers that event [data].
- **Failure narration** [disasm 0x5f90a0]: `Ui::onNarrator = Audio::narrator`, called by the subtitle line display (ui.cpp
  `drawSubtitle`) for a `Miss.Generic.LoseN` line in subtitle mode.
- **Lip sync** [disasm, ours in acting.cpp `Actor::Mouth`, `speak`, `mouthStep`]: every worm line (acting lines and main.cpp's
  Fire / Death / Hurt / Jump / TurnStart through `Acting::speak`) starts the mouth with the played variant's LIP rows (`Audio::voice`
  returns them); `mouthStep` runs W4M's update once per 20 ms step (`Acting::update`, clock + 0.5 × ticks); `Acting::clip` hands the
  viseme clips, weights and `open` to `Models::Layers`. models.cpp `lipLayer` adds each viseme's offset from Base on the face bones
  after the emote (XAnim sum), `teethTime` picks the `Teeth` clip time for the mouth XChildSelector. The worm glb exports the 8
  viseme clips (w4m-models `WORM_CLIPS`). Render only: nothing reaches the sim or the checksum.
- **Checks**: `W4NX_LIP="<viseme> <weight> <open>" ./worms4nx --animshot Base` draws a viseme (A open with the teeth image 2, MBP
  closed, O round, Cons teeth image 1); `W4NX_HIDDEN=1 W4NX_SHOTEND=900 ./worms4nx --ui missionhud MineAllMine` logs
  `AUDIO: EFMV MineMine_Player1_01 0.0 dB, lip 21 rows` per line (`ACTING: worm N speaks` at debug level).

## Voice lines [ours, per the coordinator]
- A line is dropped while any line of the same voice bank still plays: no queue, no gap (`voice()` in audio.cpp).

Map emitter sounds (fx.cpp, docs/w4m/render.md §3): the other EmitterSoundFX of map effects (weapons/ChurchBell, MetalCreak, CreakingTree, FlyingSaucer, HourGlass, Mummy, OldRobot, RustlingLeaves, Tentacles, Windmill, WooWooBeams, BomberEngine) are FEV events without a sound definition: silent in W4M [data, fev.py]. kRain emitters never start their EmitterSoundFX (weapons/FloodRainLoop, weapons/RainLoop); the rain's sound is RainGraphicEntity's weapons/RainLoop (`Flood` above, `Audio::loop` while a rain runs) [disasm 0x4829cc].
Weapon effects started by name (`GameEvent::fx`, `Fx::start`) play their EmitterSoundFX the same way: an effect-carrying Boom plays no
sound of its own in `onEvent`. The Fatkins Detonate's DetonationSfx weapons/ExplosionLarge is the same event as its
WXP_ExplosionX_InnercloudLarge's (max playbacks 1, steal oldest), started a 20 ms update later: only the emitter's is played [data; the
steal: assumed FMOD behaviour 1].
