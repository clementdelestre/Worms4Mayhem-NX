# Worms4NX network protocol (version 1)

The protocol version is `wire::VERSION` in `client/src/wire.h`, sent in Hello and in the LAN beacon. It is 1 until a server is
deployed; from then on, bump it on any wire or sim change. The standalone server (`server/src/lib.rs` `VERSION`) and the embedded
LAN relay (`lanhost.cpp`, `wire::VERSION`) both speak version 1.

TCP, default port 7777. The server owns lobby state and relays inputs; it never simulates.
Every client runs `Game` and stays in sync because `start(seed, teams, perTeam)` plus the same
per-tick `Input` stream gives the same state.

## Framing

`u16 len | u8 type | payload` — `len` counts `type + payload` (max 65535). All integers
little-endian. `str` = `u8 len | bytes` (UTF-8, max 255). `Input` = 6 bytes
`i8 turn, i8 walk, i8 aim, u8 buttons, u8 flags, u8 zoom` (same layout as `struct Input` in `sim.h`, read by `wire::R::input`).

### Input (`struct Input`, `sim.h`; applied by `Game::step`, built by `Controls::read` / `tick`, `Ai::think`)

The active player's per-tick input is the only game data on the wire; replays (`.w4r` "W4R8"; "W4R7" before the Weapon Factory graphic; "W4R6" before the Weapon Factory container fields, refused with a team weapon; "W4R3" before the Weapon Factory sliders) store the same 6 bytes; a "W4R2" replay has 5 and loads with `zoom` 255, a "W4R1" one has 4 and loads with `flags` 0.

| field / bit | value | meaning (status: ours unless noted) |
|---|---|---|
| `turn` | −127..127 | yaw rate, 127 = 2.5 rad/s; with `HEADING`: the wanted yaw, π·turn/128 rad, reached in one tick (W4M 0x5b107c, disasm); on the jetpack the turn is capped at `JET_TURN` 0.92 rad/s (W4M 0x561e40); Blimp view: camera yaw; girder: GirderCam yaw |
| `walk` | −127..127 | forward share of `WALK_SPEED` 3.0625 m/s; jetpack: forward thrust when > 1; Blimp view: moves `Game::cursor` forward; girder: steps the preview |
| `aim` | −127..127 | pitch rate, 127 = 1.5 rad/s (−1.2..1.45 rad); on the rope (or with an object hooked): reel, 127 = 6 m/s, 1 m..`ropeMax`; Blimp view: moves the cursor right (or tilts it with `PITCH`); girder: side step (or raise / lower with `PITCH`); with `NEXT_WEAPON`: the pick |
| `FIRE` 1 | held | fire; powered weapons (`powered()`: shells, homing) charge while held, 1.5 s to full (W4M Tweaks.MaxPowerUpTime 1500 ms, data), and fire on release; after the launch a press detonates (`detonate`: sheep, old woman, super sheep...); jetpack: thrust while held (W4M FireUtil); rope / parachute with a secondary: drops it |
| `JUMP` 2 | held | jump; a second press within 18 ticks (300 ms) is a backflip, held = vertical jump (W4M DetectJump 0x5aefa0, StartJump 0x5acd40, disasm); on the rope: let go; jetpack in flight: drop the secondary (W4M Fire.Second) |
| `NEXT_WEAPON` 4 | press | `aim` 0: next selectable weapon (`Game::nextWeapon`, ours); `aim` = index + 1: pick that weapon (`Input::pick`, `Game::pick`, W4M WeaponSelected 0x565d30); `aim` carries no rate that tick (`Controls::tick`) |
| `HEADING` 8 | flag | `turn` is an absolute yaw (camera-relative stick, W4M 0x5ab3d0) |
| `FUSE_UP` 16 / `FUSE_DOWN` 32 | press | ±1 s on the team's fuse (1..5 s, default 3) for `user_fuse` weapons (W4M FuseUp, data); kept per team in `Game::fuses` |
| `TARGET` 64 | flag | Blimp view (W4M IsometricCam 0x52a5e0): `turn` / `walk` / `aim` drive `Game::cursor` and the worm stays put; also the girder preview mode |
| `PITCH` 128 | flag | with `TARGET`: this tick's `aim` tilts the Blimp camera (or raises the girder) instead of moving sideways; the client alternates the two on every other tick at twice the rate when both are held. Without `TARGET`, on a landed jetpack holding a secondary: lay it (W4M Fire.Second) |
| `flags`: `SKIP_COUNT` 2 | flag | the local active player pressed X while the Settle damage count ran (observed in W4M by the user, 2026-10-03); the sim ends the display at once, deaths follow as usual |
| `flags`: `CAMERA` 1 | flag | the active player used a follow-camera key this tick (right stick, d-pad zoom, A D X Z, wheel; `Controls::read`); the sim only reads it to end the hot seat (W4M InGame group `Camera.*`, 0x4e1610, disasm) |
| `flags`: `DRAW` 8 | flag | Pause > Draw Round > Yes (W4M `WXFEP.ConfirmDraw` -> `GameLogic.DrawImmediately`, disasm 0x4cd3a0 / 0x4fd105): the sim ends the match at once with no winner (`winner` -1, GameOver). Only a local Multiplayer match offers it (W4M shows the row for GameType `Multiplayer`, not `Network`), so it reaches replays, never the wire in practice; the byte layout is unchanged |
| `zoom` | 0..255 | the sender's logical camera zoom × 255 (HeadCam zoom in the aim view, 255 elsewhere; `Controls::read`); the gun wobble is scaled by min(1.5 zoom, 1) (W4M GunWobbleObject 0x55fbed reads the CMS current camera's +0x5c, disasm) |

`Game::step` takes `pressed = buttons & ~prevButtons` for the edge-triggered bits; `prevButtons` is in the checksum. A hot seat
(`Scheme::hotSeat`) ends on any input but `TARGET` alone, `flags` included (W4M: every control group but Menu, CameraSelect,
Spectator, NetworkSpectator and ControllerRemoved sends `Input.SomeInputFrom`, which TimerLogicEntity 0x50fce0 takes as the end of the
hot seat; 0x504ee0, disasm).

## Messages

C = client → server, S = server → client.

| type | name       | dir | payload |
|------|------------|-----|---------|
| 0x01 | Hello      | C   | `u16 version, str name, u64 token` (token 0 = new player) |
| 0x02 | Welcome    | S   | `u32 playerId, u64 token` |
| 0x03 | Error      | S   | `str message` |
| 0x10 | ListRooms  | C   | — |
| 0x11 | RoomList   | S   | `u8 n, n × (u32 id, str name, u8 players, u8 maxPlayers, u8 started)` |
| 0x12 | CreateRoom | C   | `str name, u8 maxPlayers` (2..8); creator becomes host and joins |
| 0x13 | JoinRoom   | C   | `u32 roomId` |
| 0x14 | RoomState  | S   | `u32 roomId, u32 hostId, u8 n, n × (u32 playerId, str name, u8 online)` — players in slot order |
| 0x15 | Leave      | C   | — |
| 0x16 | Teams      | C/S | C: `u8 n, n × team`; S: `u32 consoleId, u8 n, n × team`; `team` = `str name, u8 cpu, str speech, str hat, str gloves, str glasses, str tash, str flag, u8 grave, u8 k, k × str wormName, u8 worms, i8 alliance, i8 handicap, u8 s, s × Weapon` (W4M NetworkTeamData + the slot settings, docs/match-setup.md "Saved teams": cpu 0 = human, 1..5 = AI level; ids are W4M's, "" = none / default; worms 0 = wormsPerTeam, alliance -1 = the lowest group no earlier team holds, handicap -1..1; `s` 1: the team's Weapon Factory weapon, W4M Tn_CustomWeapon). The sender's whole team list (several per console, humans and CPU); the relay keeps it per console and forwards it to the others (not back to the sender) |
| 0x17 | Lobby      | C/S | host → relay → the others: `u8 n, n × (u32 consoleId, u8 teamIdx), str map, u32 rules, u8 k, k × u8 scheme, u32 wormpot, u8 wormsPerTeam, u8 r, r × u32 readyConsoleId`. The n pairs are the room's team slots in order (slot = colour); `teamIdx` indexes that console's Teams list |
| 0x18 | Ready      | C/S | C: `u8 ready`; S → host only: `u32 consoleId, u8 ready` |
| 0x20 | Start      | C/S | `u32 seed, u8 teams, u8 wormsPerTeam, teams × u32 ownerPlayerId, str map, u32 rules, u8 n, n × team, u8 k, k × u8 scheme, u32 wormpot` (server relays the bytes after the owners untouched) |
| 0x21 | Inputs     | C/S | `u32 firstTick, u8 n, n × Input` |
| 0x22 | TurnEnd    | C   | `u32 tick, u32 checksum` |
| 0x23 | Desync     | S   | `u32 tick` |
| 0x24 | Replay     | S   | `u32 ticks` — length of the input log about to be replayed |
| 0x30 | Chat       | C/S | C: `str text`; S: `u32 fromPlayerId, str text` |
| 0x31 | Ping       | C   | `u32 nonce` |
| 0x32 | Pong       | S   | `u32 nonce` |

## Rules

- **Hello** must be first; version mismatch → Error and close.
- **RoomState** is broadcast to the room on every change (join, leave, host change, online flag).
  When the host leaves, the next player in slot order becomes host; when it drops mid-match, the first
  online player does (the host plays the CPU teams and the idle turns of owners offline > 30 s). Empty rooms are deleted.
- **Start**: host only. The server stores it, resets the match log and broadcasts it to the
  whole room *including the host*; everyone (host too) starts the game on receipt.
  Sending Start again restarts the match.
- **Start rules** (`u32 rules`, `enum Rule` in `sim.h`): 1 King, 4 Vampire, 16 Low gravity,
  64 Sudden death, 128 No delays (test: the preset's W4M weapon delays are ignored).
- **Start scheme**: the `Scheme` struct of `sim.h` as raw bytes in field order (turn, retreat, hot seat time,
  round minutes, worm energy, crate %, weapon/health/utility crate shares, crate hp, mines, barrels, mine fuse,
  sudden death type, fall damage, wind, weapon set, water speed, mystery crate share, mine factory on). Fields are only ever appended: a reader keeps defaults for
  bytes it does not get (missing block = default scheme) and ignores extra ones. The block is preceded by its length (`u8 k`, 20 today).
  Weapon delays are not sent: every client derives them in `Game::start` from the preset whose bytes equal the scheme (`SCHEMES`), unless rule 128.
- **Start wormpot** (optional, after the scheme block; absent = 0): `wormpot` holds the three
  reels: byte r = reel r's W4M mode id (`WormpotMode` of `sim.h`, 0 or 1 = empty).
- **Weapon** (a team's Weapon Factory weapon, in its `team` record) = `str name, u8 kind` (clamped to `Kind::ChangeWorm` on read), 5 × f32 (speed, fuse, bounce, poison,
  cluster spread), 8 × f32 Weapon Factory sliders (land damage, worm radius, worm damage, blast strength; then the cluster's), 4 × i32 (count, clusters, shots, crate weight), u8 wind, str model, str icon,
  then the W4M WeaponFactoryContainer fields: i8 launch, i8 detonate, i8 cluster detonate, i32 fuse time, u8 flags (bit 0 powers up, bit 1 cluster poison),
  3 × f32 (max power, collision, cluster collision), str cluster model, str ariel, str graphic` (graphic: W4M WeaponGraphicsResourceID, the held meshes; f32 = IEEE-754 bits as u32; -1 launch / detonate: our editor's weapon).
  Every client puts them in the one Weapon Factory slot after its `weapons.json` table, the playing
  team's at each turn start (`Game::wieldSecret`), so the table (part of the checksum) is the same everywhere. `factoryPayload` builds the rest on every
  peer from the launch type's `weapons.json` weapon (W4M 0x598160 clone) and the sliders (docs/w4m/weapons.md "Template clone").
- **Inputs**: the match is one input stream indexed by tick (tick 0 = first `step` after
  `start`). The owner of the active team sends an `Input` for *every* tick it steps, batched
  (~3 ticks per frame). The server requires `firstTick == ticks logged so far`; otherwise it
  drops the frame and answers Error then a resync (Start, Replay, log, as on reconnect), since that
  client already stepped inputs the log does not hold. Accepted inputs are appended to the log and relays to the rest of the room. Remote clients step tick
  `t` only once they hold input `t`; turn hand-off is implicit: the next owner starts sending
  when its own sim reaches its turn.
- **TurnEnd**: every client sends it when a turn ends (tick = ticks stepped so far). The
  server keeps the first checksum per tick; any different one → Desync broadcast to the room.
- **Reconnect**: a dropped player stays in a started match (`online = 0`). Hello with the
  old token reattaches: Welcome (same id), RoomState, Start, Replay, then the whole input log as
  Inputs frames from tick 0. The client restarts the game and fast-forwards through them; it never
  sends its own input for a tick below the Replay count (the log may arrive over several reads).
  In a lobby that has not started, a disconnect is a Leave.
- **Teams / Lobby / Ready** (before Start): a console sends its Teams on every edit and after every RoomState; only the host may
  send CPU teams (`cpu` > 0; else Error). The relay stores the last Teams of each console and the host's last Lobby (host only; else
  Error) and sends them (every console's Teams, then the Lobby) to a console that joins or reconnects. Ready is forwarded to the host,
  which folds it into the next Lobby (`ready` list); the host's own Ready takes the same path. The host keeps the slot order: join order
  of the consoles, each console's teams in list order, 4 teams max (extras are dropped), teams of consoles that left removed, then sends
  Lobby when it changes. A console that leaves the room (or drops from a lobby) takes its Teams with it: clients drop the Teams
  of every console missing from RoomState. When the host changes, the last Lobby stays and the new host re-sends it as needed.
- **Start** is built by the host from the Lobby: teams in slot order, `owners[team]` = the console of each team, scheme / wormpot /
  wormsPerTeam / map / rules from the Lobby. Everything else the clients read from Start, not from the Lobby.

## LAN

No external server: the hosting console runs the same relay for one room (`client/src/lanhost.cpp`) on TCP 7777
and its own client joins it over 127.0.0.1. Every second it sends a UDP beacon to port 7778 (255.255.255.255,
the subnet broadcast on Switch, and 127.0.0.1): `"W4NX", u16 version, u16 tcpPort, u32 session, u8 players,
u8 maxPlayers, u8 started, str roomName` (raw datagram, no frame header). Clients list beacons heard in the
last 3 s (deduplicated by session), connect to the sender's address and join room 1.
