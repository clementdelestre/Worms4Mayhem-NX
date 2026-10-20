# Match setup (Teams / Map / Rules), local and network

Layout from the user's mockup (~/Downloads/worms4nx-local-match.html, 1280x720) [user-requested]. W4M splits the same steps: `WXFE.CreateAGame` team
slots (`WXFEP.InsertTeam`), then `WXFEP.SelectLandscape2` (docs/w4m/frontend.md) [data, per the mockup notes]. Colours, fonts and texts: the paper look of
the mockup drawn with plain shapes [ours]; the texts are W4M's own (`tr()` keys below), the labels without a key are ours (en / fr).

## State (`Lobby`, client/src/lobby.h) [ours]
- `Lobby::cfg` (a `GameConfig`): teams, `teamSetup[]` (name, cpu, voice, hat, local pad), map, rules, scheme, wormpot, wormsPerTeam.
- `owner[team]` (console id), `ready[]` (per team slot: its console is ready), `self`, `host`. `canEdit(team)`: the team's owner is this console (name, control, voice, hat, add / remove).
  `canEditMatch()`: this console is the host (map, scheme, rules, worms per team, Wormpot). Local play: one console, `self == host`, every team its own.
- The three screens (`Frontend::setupTeams` / `setupMap` / `setupRules`) draw from the Lobby; START copies it into the match `GameConfig`.
  In a network room the same screens run on a view of `Net::Lobby` (below); there is no other setup screen.
- `GameConfig::Team::pad` (0..3): the pad that plays the team. Local only: not on the wire, not in the checksum. `main.cpp` `teamPad()`: the pad when
  connected, else 0 (desktop keyboard = pad 0); input, HUD and rumble of a team go through it. One pad may play several teams.

## setup.txt [ours]
`teams`, `worms`, `rules`, `reels`, `map`, `scheme`, then per team `team <i> <cpu> <hat> <voice|-> <name>` and `pad <i> <pad>`. A file without `pad` lines:
team i on pad i when that pad is connected, else pad 0. `rules` is masked to the rows the Rules screen shows (King, Vampire, Low gravity, Sudden death).

## Screen 1, Teams [user-requested]
- Pad strip: the 4 Switch player slots: LEDs (pad i lights i + 1 of 4), `padStyle()` name when present, else "Appuie sur L + R pour rejoindre", and one dot
  per team the pad plays (team colour).
- 4 cards, colour = team slot (`TEAM_COLORS`). Rows: name (A edits), Contrôle (Manette 1..4, then `FETXT.CPU1..5`), Voix (`FETXTSH.Voice`), Chapeau.
  Empty slot: "+ Ajouter une équipe". D-pad up / down: row; left / right: change the value (on the name row and empty slots: another card; L / R too).
- X adds a team (empty slot) or removes the focused one (2 teams least). Y on any pad: that pad plays the focused team.
- A human team whose pad is absent: red control chip and a red flag line. `+` then opens the system controller applet (Switch) with the number of
  distinct pads used, `-` opens it any time; START does too while a pad is absent. Then `+` goes to Terrain.
- Each card: the worm model (`worm.glb`, clip `Base`, team tint, hat on `HatLocator`) drawn live in a `RenderTexture` (256x346), turning slowly, free in
  the card's bottom-right corner and overflowing it.

## Screen 2, Terrain [user-requested]
Big level picture left, plain grid of the maps right (3 columns, 5 rows visible, scrolls with the selection; no theme filter), D-pad moves, Y a random map,
`+` next, B back. The grid lists the random island, the W4M Prebuilt landscapes and the Worms 3D maps (user-requested to stay selectable) [data, below].

## Screen 3, Règles [user-requested]
Rows: Schéma (left / right: preset, A: scheme editor), Vers par équipe (1..4), separator, Worm vital (`RULE_KING`), Vampire (`RULE_VAMPIRE`), Gravité basse
(`RULE_LOW_GRAVITY`), Mort subite (`RULE_SUDDEN_DEATH`), separator, Wormpot (A: reels). The right panel explains the selected row with W4M's text:
`FETXT.HTPSubtopic24` (scheme, title `FETXT.GameStyle`), `FETXT.WPotHelp.VitalWorm`, `.Vampire`, `.Lowgravity`, `FETXT.HTPSubtopic7`, `FETIP.Wormpot`; Vers
par équipe has none. W4M's `/*token*/` markup is dropped and space runs collapse when wrapping. `+` starts the match (network: below).
`RULE_NO_DELAYS` (tests) has no row. The house rules Highlander, Karma and Rope race are removed [user-requested]; their bits (2, 8, 32) stay unused.

## Hat names and map list [data + ours]
- Hat names: `WORMATTACHMENTS.XOM` `WXFE.A.Hat.*` holds `MeshID` (`Hat.Baseball.C`) and `MeshTextNameID` (`FETXT.Hat.Baseball.Camo`); the hat file is the
  lower-cased MeshID without `Hat.`, `.` -> `_` (`baseball_c.glb`) [data]. `tools/w4m-re/setuplists.py` writes `assets/hats.txt` (file, key); `Ui::hatLabel` shows
  the W4M text. The jetpack helmet (`jetpack.glb`) is no `WXFE.A.Hat` entry: it is not in the hat list (`Models::start`) [ours].
- Map list: `WXFEP.SelectLandscape2` (MENUTWKX) is built by TablePopulationService 0x4cf620 as four tabs: `FETXT.Random2` (random island), `FETXT.Prebuilt`,
  `FETXT.Saved`, `FETXT.Worms3DSection` [disasm 0x4cf620, REA decompile]. The Prebuilt landscapes are the 74 SCRIPTS.XOM `WXFE_LevelDetails` of `Level_Type` 0
  (`Multi.*`; `Level_FileName`, e.g. `Multi_CarpetCapers`, `ChallengeCrate` for Desert Oasis, `Alien-w3d`, `RandomArabian`..., which are procedural) [data]. Our list
  (`Ui::multiplayerMaps`, `assets/multimaps.txt` from `setuplists.py`) keeps those that exist as imported maps, the random island and every `*-w3d` map
  (user-requested, docs/w4m/README.md). How W4M maps its tabs to the 4 `Level_Type`s of Destruction / Statue / Fort / Survivor lists (types 1, 2, 11, 3) was not traced [assumed: not offered here].

## Network room [ours unless tagged]
- Flow: Réseau local / En ligne (Network menu) -> Salon (`Ui::salon`: Créer un salon / Rejoindre un salon; Rejoindre lists the LAN games heard or the server's rooms,
  R refreshes) -> the 3 screens above. W4M had the same order: GameList / HostGame / GameLobby (docs/w4m/network.md "Lobby flow") [data]. B on Teams leaves the room.
- The Lobby is a view of `Net::Lobby` (`Frontend::fromNet`): `owner[k]` = the slot's console, `self` / `host` = this console / the room's host, `ready[k]` = its console is
  ready. Slot order = colour, join order. This console's teams (`myTeams`, with their local pads) are the source of its own cards; a change goes out as TEAMS at most
  every 0.1 s (`Frontend::toNet`), so remote edits (name, hat, voice) show live on every console. A console edits only its own teams (`canEdit`); CPU control is
  offered to the host only. X adds a team to this console (4 in the room) or removes the focused own one (1 least); several local humans keep one pad each (`GameConfig::Team::pad`,
  re-attached to the match config at Start by `Frontend::minePad`).
- Cards show the owner console's name ("(vous)" for this one) and the W4M ready grenade: `Nav Start` of `fe2/nav_normal` when the console is ready, `fe2/nav_disabled` when not
  (WXNET `Team<n>_Ready`, docs/w4m/network.md) [data, flag-to-state rule assumed there].
- Terrain and Règles: the host edits (host edits `net->lobby`, `pushLobby()` when it changed); the others see them read-only with a note, and the help panel stays readable.
- Ready: ZR / R on any screen, or `+` on Règles for a non-host; Règles' button reads PRÊT / PAS PRÊT, and START for the host once `Net::allReady()` and at least 2 teams
  (a host's `+` that cannot start toggles its own ready). The lobby timer (`ReadyTimer`, lobby.h; `main.cpp`) follows W4M: 30 s once 2 players are there, restarted by each join,
  off under 2 players, shown in the heading disc as `%.2d` / `--`; at its end the console sets itself ready [data, docs/w4m/network.md]. START stays the host's press
  (W4M's Steam gate starts by itself) [ours]. `W4NX_LOBBY_TIMER=<s>` shortens it for tests.
- This console's teams (name, voice, hat, pad, how many) are saved on each change as `net` / `netpad` lines of setup.txt (`Frontend::saveNetTeams`) and restored by the next room or
  app start [ours]. A change of map / rules / scheme / wormpot / worms per team leaves the ready flags as they are, as W4M's `LobbyDataChangeMsg` handler does (docs/w4m/network.md).
- After a match the room is shown again (`Frontend::backToRoom`), own ready cleared.
- Tests: `--ui netteams|netmap|netrules[_ro]` (host / guest view of a 3-console room), `--ui salon|salonjoin`; `tests/netbot.sh` (2 consoles: the guest edits its team, the host sees it
  and pushes map and rules, the guest sees them, the host's ready comes from the timer, START, checksums).
