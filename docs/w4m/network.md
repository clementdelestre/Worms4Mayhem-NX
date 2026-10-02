# W4M network

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 16. Network (online multiplayer)

Tags: [data] = strings, RTTI, schema or imports; [disasm] = confirmed in code; [assumed] = inferred, not verified. Times are logical milliseconds (`TaskManager::GetLogicalTime()` = `[[0x96d030]+0x38]`).

### Summary

- Transport: Steam P2P (`ISteamNetworking`) under Team17's **XomOnline** layer. Discovery and lobbies use Steam matchmaking. There is no GameSpy SDK; only a GameSpy-shaped legacy `GameBrowser` remains.
- Topology: client/server. The Steam lobby owner hosts a `XomOnlineServer` and every peer is a `XomOnlineClient`. Traffic travels on named channels (`group~topic`), and game traffic uses `$simchannel$`. Host migration is supported.
- Sync model: **deterministic input-message lockstep with delayed playback**. Every machine runs the full simulation. Only the machine of the player in play generates gameplay input messages. Those messages are timestamped with a future logical time, run locally at that time, and sent to the others, which replay them at the same logical time. Remote machines are throttled so they stay behind the active player's "lead time". At end of turn, a state checksum is compared and any mismatch aborts the game. No world state is replicated.

### Transport layer

- Imports [data]:
  - `steam_api.dll`: SteamNetworking, SteamMatchmaking, SteamFriends, SteamUser, SteamUserStats, SteamHTTP, SteamApps, SteamUtils, SteamRemoteStorage, plus Register/UnregisterCallback and CallResult.
  - `WSOCK32.dll` by ordinal: socket, bind, listen, accept, connect, send, recv, sendto, recvfrom, select, setsockopt, ioctlsocket, gethostname, gethostbyname, WSAStartup and others.
  - `WS2_32.dll`: WSASend, WSARecv, WSACreateEvent, WSAGetOverlappedResult.
- `IXConnection` implementations [data]:
  - `XSteamConnection` (vt 0x8aefec): P2P, with callback `P2PSessionConnectFail_t`. Its fail reasons are logged as "STEAM CONNECT FAIL - remote user has different app ID / doesn't own / no steam connection / not accepting".
  - `XSteamListener` (vt 0x8aeb9c): accepts `P2PSessionRequest_t` ("GOT THE MAGIC OnP2PSessionRequest").
  - `XSteamLocalLoopbackConnection` (vt 0x8af27c): the host's own client.
  - `XTcpConnection` (vt 0x8ae79c).
  - `XListener` (vt 0x89a27c).
- Addresses [data]: `XIp4Address` (`%d.%d.%d.%d:%d`) and `XSteamAddress` (vt 0x8af110). `XSteamManager` vt is 0x8ae34c.
- XomOnline `g_Config` @0x98adf8 [disasm, ctor 0x40369b]:
  - +0: port 1024, formatted as `0.0.0.0:%d` by the server listen code at 0x404b85.
  - +4 and +8: 500 and 500.
  - +0xc: listener class `XSteamListener` (class 0x8ae8e4).
  - +0x10: connection class `XSteamConnection` (class 0x8aebf8).
  - +0x18: 1000.
  - So on PC the "1024" is a port carried over XomOnline on Steam P2P, not a real UDP/TCP port [assumed].
- `XTcpConnection` is used by a debug "Team17 telnet server V0.1" on `0.0.0.0:23` (0x692db1) [data/disasm]. `XUdpPacketPort` (vt 0x8af4f0, broadcast/recvfrom) is only created through the class factory, and no PC call site was found [assumed: unused or LAN legacy].
- `/ENABLEPORTFORWARDING` logs "Universal Plug + Play port forwarding enabled." [data]. The Steam invite command line is `+connect_lobby` [data].
- XomOnlineMachine (one per peer link, `XomOnlineMachineImpl.cpp`) [data+disasm]:
  - Handshake states: UpdateConnect -> WaitName -> WaitKey -> WaitTag. The application tag must match ("XomOnline Default Application V1.0", otherwise "application tags don't match").
  - Commands: `XOMONLINEMACHINE_COMMAND_BEAT` (heartbeat) and `_CLOSE`.
  - Timeouts come from the machine config @0x98aeb0 (ctor 0x407c62): connect 10000 and 20000 ms (+0xc/+0x10, used at 0x408c0c), send-flush 250 ms (+0x14), 1000 (+0x18), heartbeat interval 1000 ms (+0x1c, 0x408b01/0x408962), and receive idle timeout 15000 ms (+0x20, deadline reset at 0x40894f on every receive).
  - Expiry gives "timed out-Machine1..5" and `HRESULT 0x80210001` (UpdateOpen 0x40803c).
  - Packets: header with a "big header" variant, `kPacketSizeLimit`, and a send store flushed with partial sends allowed.
- XomOnlineServer/Client [data]: commands `XOMONLINE_SERVERCLIENT_CMDADDCHANNEL/CMDREMCHANNEL` and `XOMONLINE_CLIENTSERVER_REQADDCHANNEL/REQREMCHANNEL`. Peers are identified by tag<->id pairs (id < 256). Clients route packets "from %d to %d" through the server. A channel has `SendToOwner` and `SendToGroup`.

### Online object model (XomOnline, Steam backend)

| class | vtable | role |
|---|---|---|
| `XomOnlinePlugInDriver` | 0x8bce3c | Top-level online state machine: title, sign-in, online main, QuickMatch, OptiMatch, RankedMatch, CreateLobby, Lobby, HostMigration. Error injection. Host migration 500 ms after the host is lost (0x409c57) [disasm] |
| `WXomOnlinePlugInService` | 0x86e15c | Game-side bridge (UI messages `xo.*`, `WXNET.Check*`, `Net.RankedMatchSetup`, `Net.PostWinMatchScreen`) |
| `steam_XomOnlineMatchImpl` (`XomOnlineMatch`) | 0x8bda2c | Creates or publishes the Steam lobby (`"%s's lobby"`). Lobby data keys: `player_limit` (<=4), `is_ranked`, `preview_data`, `host_name`, `Rank_Higher`, `Rank_Lower`, `xo.public_open/filled`, `xo.private_open/filled` |
| `steam_XomOnlineFinderImpl` (`XomOnlineFinder`) | 0x8bdfec | `RequestLobbyList` with filters (`LobbyMatchList_t`); JoinInviteMatch |
| `steam_XomOnlineSessionImpl` (`XomOnlineSession`) | 0x8bd504 | Enters the lobby (`LobbyEnter_t`). The lobby owner is the server ("local match, creating server"); others start a client. Opens channel `session`. Max 4 lobby members. CheckViability ("Session no longer viable") |
| `XomOnlineServer` / `XomOnlineClient` / `XomOnlineMachine` / `XomOnlineChannel` | 0x8bac94 / 0x8bb2fc / 0x8bb8ec / 0x8bdc94 | Transport described above |
| `steam_XomOnlinePlayer/PlayerSet/Gamer` | 0x8ba1b4 / 0x8be374 / 0x8bd69c | Players and their join/leave state machines |
| `steam_XomOnlineHostMigrationImpl` | 0x8be658 | New lobby owner becomes host ("migration started, becoming new host (%s)") |
| `SteamLobby` (entity, `WXSteamLobby.cpp`) | 0x8b72b0 | Callbacks LobbyChatUpdate, LobbyDataUpdate, PersonaStateChange; "AUTO SET PLAYERS READY" |
| `WXSteamLobbyBrowser` (service) | 0x8b73f8 | `GameLobbyJoinRequested_t` (Steam overlay invite), enters lobby |
| `PCPlatformManager` | 0x86d388 | `GameLobbyJoinRequested_t` ("Going to Online Lobby from out-of-game invite") |
| `GameBrowser` (`GameBrowserImpl.cpp`) | - | GameSpy-style legacy: `GS.*` messages, keys `gamever authresponse privateip publicip firewall hostport openstaging`, data resource `Net.ServerPort` (+0x20). `GS*` containers are GSRoom, GSPlayer, GSNetworkGame, GSTeam and others [data]; its PC use is [assumed] marginal |
| `NetService` (`NetService_Xbox.cpp`, shared with Xbox) | 0x89b3ec, ctor 0x705bce, instance `[0x979ddc]` | Game-level networking (below) |
| `NetThrottle` | 0x89b1fc, Update 0x7082ca | Time throttling of remote simulation |
| `ReplayMessageStoreService` | 0x8569ac, Update 0x542f00 | Timestamped message store: send/receive/replay of input |
| `NetNameStorageService` | 0x89d3f4 | Player-details lookups |
| `SteamVoiceService` | 0x8696fc | Push-to-talk; carried by `NetSteamVoice*Msg` |
| `NetworkDebugService` | 0x8565d4 | `Net_%s.log` |
| `SpectatorMasterLogicEntity` / `SpectatorCam` | 0x851e28 / 0x855ddc | Spectator input ("NetSpectatorInput enabled") |
| `NetworkIndicatorEntity` | 0x866c88 | HUD lag/busy indicator (`Net.Busy`, `Net.NetworkBusy`) |

### Net messages (`BaseNetMsg`, XOM-serialised with `NetStream.cpp`)

`NetService` ctor 0x705bce registers the classes with `NetStream::RegisterClass` 0x70daf0, at most 256 classes. The registration index is the class id written on the wire [disasm; the wire id is assumed].

| idx | class | fields (schema) | use |
|---|---|---|---|
| - | `BaseNetMsg` | MsgId u16 | base |
| 0 | `StringNetMsg` | Text | generic |
| 1 | `ClientGameLoadCompleteMsg` | - | client finished loading the level |
| 2 | `ClientFeLoadCompleteMsg` | - | client back in the frontend |
| 3 | `BuggerOffMsg` | - | kick |
| 4 | `ClientMsg` | DispatchTime u32 | base of timed messages |
| 5-10 | `NetStored{,String,Int,TwoInt,Float,TwoFloat}MessageArray` | Msg u32[], Time u32[], plus Text[] / Value1[] / Value2[] | **batched input messages with logical timestamps** |
| 11 | `ClientTimeSyncMsg` | (DispatchTime) | active player's clock, every 1000 ms; -1 at end of turn, -2 at end of game |
| 12 | `GameDataSetupMsg` | SchemeData ref, LevelName, LevelSeed, LogicalSeed | host -> all before load (0x70ad51 "sending level seed / logical seed") |
| 13 | `GameStateValidationMsg` | WormIndex, SourceOfValidation, Random, WormCheck[] (`WormDataChecksums`), TaskVerificationString, Alliance/Team/Worm inventory checksums, camera checksums and floats, RoundTime, HotSeattime, TurnTime | desync check |
| 14-18 | `ClientToClientMsg` (DestClient), `AllianceMsg`, `GlobalChat`, `GlobalAnonChat`, `GlobalActionChat` | SourceClient, Content | chat |
| 19-21 | `NetSteamVoiceData/Start/EndMsg` | VoiceData u8[] | voice |
| 22 | `LobbyDataChangeMsg` | LevelTheme, LevelName, LevelSeed | host changed the landscape in the lobby |
| 23-24 | `PlayerChat`, `AllianceChat` | SourceClient, Content | chat |
| 25 | `WormDataChecksums` | per worm: Energy/Pending/Initial/Current, Pos/Rot/Vel/LastColNorm checksums, SlopeAngle, WeaponAng, WeaponFuse, Weapon, Team, Poison, AfterTouch, GunWobble | element of validation |
| 26 | `ClientSurrenderMsg` | SurrenderingPlayerIndex | surrender (`Team.Surrender`) |
| 27 | `ClientReplayRoundMsg` | Reason | "NetClient returning to frontend - Replay round" |
| 28 | `TeamMessage` | Nick, Team ref, Action enum, Alliance, WormCount, Handicap, InvertX/Y/YFP, Sanctioned | lobby team add/remove/update |
| 29 | `ClientRemoveMsg` | ClientNick | player removed |
| 30-32 | `ClientSavedMapMsg`, `ClientHasDlcMsg` (DlcOwned), `ClientHasReadForRank` | | map, DLC and ranked handshakes |
| 33 | `HostInfoMsg` | HostNick | after connecting or migrating |
| 34 | `LatestMessagesMsg` | latest time per stored list (6 u32) | host migration resync |
| unreg. | `GameStartMsg`, `GameStateValidationHashMsg` (SourceOfValidation, Hash0-3), `PlayerMessage` (Nick, Player, Action), `BrowserWelcomeMessage` (Host, Level, Theme, TimeOfDay, LogicalSeed, Wormpot1-3, UniqueId), `BrowserReadyStateMessage` (Nick, IsReady), `AvatarMetadataMsg` | | GameBrowser path or unused [assumed] |

Other team/lobby containers: `NetworkTeamData : StoredTeamData` (+WormCount, Alliance) and `NetworkGame` (State enum, Timeout).

### Game-message IDs used by NetService

The IDs are globals at 0x979de0-0x979f18, filled by the ctor at 0x7f7d30 [disasm]:

| global | message |
|---|---|
| de0 | GameLogic.Win |
| de8 | GameLogic.Draw |
| df0 | GameLogic.GameLoadComplete |
| df8 | GameLogic.Turn.Started |
| e00 | GameLogic.Turn.Ended |
| e08 | GameLogic.EndTurn |
| e10 | Net.BeginGame |
| e18 | Net.Close |
| e20 | Net.DisableAllInput |
| e28 | Net.AddPlayer |
| e30 | Net.RemovePlayer |
| e38 | Net.RoundEnded |
| e40 | Net.PostWinMatchScreen |
| e48 | Net.RankedWinMatchScreen |
| e50 | GS.AddTeam |
| e58 | Net.LocalSelectTeam |
| e60 | Net.Lobby.PlayerSelected |
| e68 | Net.Lobby.ViewPlayerDetails |
| e70 | Net.Lobby.KickPlayer |
| e88 | GameLogic.StartGame |
| e98 | GameLogic.EndTurn.Immediate |
| ea0 | Net.ConnectOkay |
| ea8 | Net.ConnectFailed |
| eb0 | Net.Stalled |
| eb8 | Net.ShutdownComplete |
| ec0 | Team.Surrender |
| ed8 | Net.NetworkBusy |
| f10 | Net.AllPlayersLoaded |
| f18 | Net.AllRankedPlayersHaveRead |

`NetService::ProcessMessage` (0x70b943) dispatches on these IDs [disasm]:

| message | handler |
|---|---|
| GameLoadComplete | 0x70786b (logs "Logical Rand at beginning of game") |
| Turn.Started | 0x709827 |
| Turn.Ended | 0x707bc5 (sends the -1 time sync) |
| Net.BeginGame | 0x70933a |
| Net.Close | 0x707b42 |
| GS.AddTeam | 0x7074b7 |
| LocalSelectTeam | 0x7072c5 |

Other string-only messages: `Net.CreateServer`, `Net.ConnectToServer`, `Net.RequestHostConnect`, `Net.ClientConnected/Disconnected`, `Net.ConnectionTimer`, `Net.CullDisconnectedPlayers`, `Net.RoundEndedInError`, `Net.WaitingForPlayers`, `Net.SessionViable`, `Net.ClientAbortGame`, `Net.CloseClient`, `Net.Client.TimeToDie`, `Trigger.Net.Host/Join` (SamStartupService autostart) [data].

### Lobby flow (host/join)

- Frontend menus `WXNET.*` [data]:
  - Navigation menus: MainMenu, GameList, GameLobby, HostGame, CustomCreate, Optimatch, QuickGameResults, OptiGameResults, Invitation, SignInSignOut.
  - In-match screens: PreRound, WinRound, WinMatch, DrawMatch.
  - Lobby UI resources: `WXNET.Lobby.Player%d.Name/Team`, `WXNET.Team%d.Ready`, `WXNET.Lobby.Timer`, `WXNET.LobbyFull`, `WXNET.Lobby.InviteFriends`, `WXNET.GamePassword`.
  - Popup prefix `WXNETP.*`. Templates live in `MENUTWKXNET`.
- Host [data, order from strings]: QuickMatch finds no match -> host screen, or CreateGame -> `XomOnlineMatch` creates the Steam lobby -> the session (lobby owner) creates the `XomOnlineServer` on port 1024 and its own loopback client.
- Join [data]: the Finder lists lobbies or an invite arrives -> JoinLobby -> client connects over Steam P2P to the owner -> machine handshake -> channels.
- In the lobby [data]:
  - Teams: `TeamMessage` (add/remove, alliance, handicap).
  - Landscape: `LobbyDataChangeMsg`.
  - Others: chat, ready flags, kick (`Net.Lobby.KickPlayer` -> `BuggerOffMsg`, with the `WXFEP.ViewKick` popup), `HostInfoMsg`, and DLC/map checks (`WXNET.CheckScheme/CheckPlayers/CheckWormpot`).
- Start sequence (NetService state strings) [data]:
  1. "initialised and waiting for contact player".
  2. "Sim channel open, waiting for game start" (`$simchannel$`).
  3. Host: "all players connected, sending team and scheme data" -> `GameDataSetupMsg` (scheme, level, LevelSeed, LogicalSeed).
  4. "game about to start, waiting for final connections" -> session set non-joinable -> `Net.BeginGame`.
  5. Each client loads and sends `ClientGameLoadCompleteMsg`.
  6. "all players' games have loaded, zabingo!" -> `Net.AllPlayersLoaded` -> play.
  7. After Win/Draw: "waiting for unload" -> "all players unloaded, now waiting for game start" (next round).
- Random seed: the host's LogicalSeed seeds the shared logical RNG and LevelSeed seeds land generation [data]. Both are sent in `GameDataSetupMsg`.

### In-game synchronisation (lockstep of input messages)

- Recording [disasm]:
  1. `InputTranslationService` send helper 0x5056b0 (typed variants 0x5057a0 and others) drops the message when 0x505270 says so. That happens at logical time 0, or for two specific IDs (`[0x95b2f8]`, `[0x95b300]`) when NetService exists and 0x708fdc refuses them. It dispatches immediately when 0x505410 says so: offline, local-only messages, or "SendMessagesImmediately ... while paused in an online game".
  2. Every other message goes to `ReplayMessageStoreService::Schedule` 0x542740 with `tTime = (now/20+1)*20`, the next 20 ms boundary.
  3. Schedule rounds `tTime` up to 10 ms and asserts `tTime >= now` and `tTime - now < 30000`.
- Transmission [disasm]: the store Update 0x542f00 handles the six lists (plain, string, int, two-int, float, two-float). Each pass dispatches due messages locally (warning "A message is being sent too late: Scheduled/Actual time" 0x53ef50), keeps `m_FullTurn_*` copies, and 0x5403a0 batches new entries into `NetStored*MessageArray` and sends them through `NetService::SendMessage` 0x70621d.
- Remote playback [assumed from structure]: the receiver inserts the arrays into the same store and dispatches each message when its own logical time reaches `Time[i]`, so all machines run the same messages at the same tick.
- Time sync and throttle [disasm]:
  - While the local player is in play, NetService 0x707155 sends a `ClientTimeSyncMsg` every 1000 ms (`[0x947468]`). With a debug flag it also sends a `GameStateValidationMsg`.
  - `NetThrottle::SetLeadTime` 0x70594b stores the lead time at +0x24. A value of -1 means end of turn ("others run till end of turn") and -2 means end of game.
  - `NetThrottle::Update` 0x7082ca, when the lead time is valid and the local player is not in play:
    - It pauses when `local + 2000 > lead` (`[0x94746c]`, "PANIC, game paused") and unpauses when `local + 3000 < lead` (`[0x947470]`, "RELAX").
    - Otherwise it sets the sim speed to `clamp((lead - local)/5000, 0.3, 3)` (`[0x947474]`), smoothed as `0.95*old + 0.05*new`.
    - "local player now in play" or -1/-2 means full speed.
  - In practice spectators run about 2-5 s behind the active player.
- Who simulates: every peer runs the full deterministic sim. Only the current player's peer produces gameplay input. `Net.DisableAllInput`, sent by Lua `stdlib.lub` DoPostActivity at end of turn, stops net input and starts end-of-turn validation [data+disasm].
- Desync detection [data]:
  - "Send End Of Turn Validation Request @", then "doing end of turn validation message at time": `GameStateValidationMsg` (RNG `Random`, per-worm checksums, inventories, camera, timers) or `GameStateValidationHashMsg` (4 x u32).
  - The receiver queues the message until its own time matches ("Have a queued validation message with time ..., but current time is").
  - Results:
    - Success logs "End of Turn Validation OK."
    - Mismatch: `Net.Error.TurnEndValFailed`.
    - Time mismatch: `Net.Error.InvalidValTimestamp`.
    - Missing message: `Net.Error.MissingTurnEndVal`, tolerated if the player surrendered.
    - "NETW**K TIMES NOT IN SYNC" at 0x709134.
  - Duplicate validation messages are discarded.

### Timeouts, disconnects, migration

- NetService abort handler 0x70864c ("returning to frontend - Game ABORTED (HR=...)") maps HRESULTs to messages [disasm]:

  | HRESULT | message |
  |---|---|
  | 0x802100c9 | `Net.Connect` |
  | 0x802100cd | `Net.RemovedFromSession` |
  | 0x8021012c | `Net.OutOfSynch` |
  | 0x8021012d | `Net.NotViable` |
  | 0x8021012e | `Net.ExcessiveLag` |

  XomOnlineMachine socket down or timeout is 0x80210001.
- UI errors [data]: `xo.txtErrConnectTimeout*`, `xo.txtErrKicked*`, `xo.txtErrIncompatibleNat*`, `xo.txtErrNoSlots*`, `xo.txtHostMigration*`, `GS.Error.HostTimeOut`, `GS.Error.Add/Remove/RequestTeamsTimeout`.
- Host migration [data]:
  1. Host lost -> "Host Migration detected, pausing activities" and "Discarding latest messages in case they are incomplete".
  2. The new host (new Steam lobby owner) re-sends `LatestMessagesMsg`, "Resending all input messages from current turn" (0x70a0f9 -> 0x5408a0 re-sends the `m_FullTurn_*` lists), and "Resending End of Turn time sync and game state validation message".
  3. "Host Migration complete, resuming".
  4. If the host left during its own turn, its teams surrender ("surrendering non local teams"). Host lost in the lobby aborts the session.
- Surrender [data]: `Team.Surrender` -> `ClientSurrenderMsg`, and `SurrenderPlayerInTurn` when the current player is gone.
