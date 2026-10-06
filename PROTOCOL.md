# Worms4NX network protocol (version 1)

The protocol version is `wire::VERSION` in `client/src/wire.h`, sent in Hello and in the LAN beacon. It is 1 until a server is
deployed; from then on, bump it on any wire or sim change. The standalone server (`server/src/lib.rs` `VERSION`) and the embedded
LAN relay (`lanhost.cpp`, `wire::VERSION`) both speak version 1.

TCP, default port 7777. The server owns lobby state and relays inputs; it never simulates.
Every client runs `Game` and stays in sync because `start(seed, teams, perTeam)` plus the same
per-tick `Input` stream gives the same state.

## Framing

`u16 len | u8 type | payload` — `len` counts `type + payload` (max 65535). All integers
little-endian. `str` = `u8 len | bytes` (UTF-8, max 255). `Input` = 5 bytes
`i8 turn, i8 walk, i8 aim, u8 buttons, u8 flags` (same layout as `struct Input` in `sim.h`, read by `wire::R::input`).

### Input (`struct Input`, `sim.h`; applied by `Game::step`, built by `Controls::read` / `tick`, `Ai::think`)

The active player's per-tick input is the only game data on the wire; replays (`.w4r` "W4R2") store the same 5 bytes; a "W4R1" replay has 4 and loads with `flags` 0.

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
| 0x20 | Start      | C/S | `u32 seed, u8 teams, u8 wormsPerTeam, teams × u32 ownerPlayerId, str map, u32 rules, u8 n, n × (str name, u8 cpu, u8 voice, u8 hat), u8 k, k × u8 scheme, u32 wormpot, u8 c, c × Weapon` (server relays the bytes after the owners untouched) |
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
- **Start rules** (`u32 rules`, `enum Rule` in `sim.h`): 1 King, 2 Highlander, 4 Vampire, 8 Karma, 16 Low gravity, 32 Rope race,
  64 Sudden death, 128 No delays (test: the preset's W4M weapon delays are ignored).
- **Start scheme**: the `Scheme` struct of `sim.h` as raw bytes in field order (turn, retreat, hot seat time,
  round minutes, worm energy, crate %, weapon/health/utility crate shares, crate hp, mines, barrels, mine fuse,
  sudden death type, fall damage, wind, weapon set, water speed, mystery crate share, mine factory on). Fields are only ever appended: a reader keeps defaults for
  bytes it does not get (missing block = default scheme) and ignores extra ones. The block is preceded by its length (`u8 k`, 20 today).
  Weapon delays are not sent: every client derives them in `Game::start` from the preset whose bytes equal the scheme (`SCHEMES`), unless rule 128.
- **Start wormpot / custom weapons** (optional, after the scheme block; absent = 0 / none): `wormpot` holds the three
  reels: byte r = reel r's W4M mode id (`WormpotMode` of `sim.h`, 0 or 1 = empty). `Weapon` = `str name, u8 kind` (clamped to `Kind::ChangeWorm` on read), 9 × f32 (radius, damage, speed, fuse, bounce,
  cluster radius, cluster damage, poison, cluster spread), 4 × i32 (count, clusters, shots, crate weight), u8 wind, str model, str icon`
  (f32 = IEEE-754 bits as u32). These are the host's Weapon Factory weapons: every client appends them to its
  `weapons.json` table at start, so the table (part of the checksum) is the same everywhere. Fields not on the wire
  (`post_launch`, `retreat`, blast keys, `user_fuse`...) are reset by `Game::start` on every peer: PostLaunchDelay 500 ms (homing 0),
  retreat = the scheme's (W4M kWeaponFactoryWeapon / kWeaponFactoryHoming, data).
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
- **Teams**: one human team per console. Each client announces its team as a Chat whose text starts with
  `\x01`: `"\x01<voice> <hat> <cpuTeams> <team name>"` (cpuTeams: CPU teams the host will add), resent on every
  RoomState; clients hide these chats. The host's Start has one team per room player (slot order, owner = that
  player), then its CPU teams (`cpu` > 0, owner = host); 4 teams max.

## LAN

No external server: the hosting console runs the same relay for one room (`client/src/lanhost.cpp`) on TCP 7777
and its own client joins it over 127.0.0.1. Every second it sends a UDP beacon to port 7778 (255.255.255.255,
the subnet broadcast on Switch, and 127.0.0.1): `"W4NX", u16 version, u16 tcpPort, u32 session, u8 players,
u8 maxPlayers, u8 started, str roomName` (raw datagram, no frame header). Clients list beacons heard in the
last 3 s (deduplicated by session), connect to the sender's address and join room 1.
