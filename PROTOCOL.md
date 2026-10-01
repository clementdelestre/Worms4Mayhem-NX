# Worms4NX network protocol (v1)

TCP, default port 7777. The server owns lobby state and relays inputs; it never simulates.
Every client runs `Game` and stays in sync because `start(seed, teams, perTeam)` plus the same
per-tick `Input` stream gives the same state.

## Framing

`u16 len | u8 type | payload` — `len` counts `type + payload` (max 65535). All integers
little-endian. `str` = `u8 len | bytes` (UTF-8, max 255). `Input` = 4 bytes
`i8 turn, i8 walk, i8 aim, u8 buttons` (same layout as `struct Input` in `sim.h`).

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
| 0x20 | Start      | C/S | `u32 seed, u8 teams, u8 wormsPerTeam, teams × u32 ownerPlayerId, str map, u32 rules, u8 n, n × (str name, u8 cpu, u8 voice, u8 hat), u8 k, k × u8 scheme` (server relays the bytes after the owners untouched) |
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
- **Start scheme**: the `Scheme` struct of `sim.h` as raw bytes in field order (turn, retreat, hot seat time,
  round minutes, worm energy, crate %, weapon/health/utility crate shares, crate hp, mines, barrels, mine fuse,
  sudden death type, fall damage, wind, weapon set). Fields are only ever appended: a reader keeps defaults for
  bytes it does not get (missing block = default scheme) and ignores extra ones.
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
