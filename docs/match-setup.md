# Match setup (Teams / Map / Rules), local and network

What is on each screen and roughly where comes from the user's mockup (~/Downloads/worms4nx-local-match.html) [user-requested]; the look is W4M's frontend, not the
mockup's: the blue divide, scrolling watermark, Title Control, list-row borders, FE fonts and gradients, the paper strip with its tooltip, NAV Prev / NAV Start
and the tilts of `WXFE.CreateAGame` (MENUTWKX, docs/w4m/frontend.md "Match setup screens (WXFE.CreateAGame)") [data]. W4M splits the same steps: `WXFE.CreateAGame` team
slots (`WXFEP.InsertTeam`), then `WXFEP.SelectLandscape2` (docs/w4m/frontend.md). The texts are W4M's own (`tr()` keys below), the labels without a key are ours (en / fr).

## Look [data + ours]
- Frame of the 3 screens and the Salon (`setupBack` / `setupFront`, ui.cpp): `divide` + `watermark` + `titleControl` as every submenu (title `FETXTH.LOCALGAME` locally, `FETXTH.GameLobby` in a room,
  `FETXTH.NetworkPlay` in the Salon) [data: CreateAGame's AnimDivide$In_Curve, Scrolling 1-4, Title Control]; the panel slides in once, then the 3 steps of the match setup keep it [ours].
  The strip carries the tooltip of the highlighted row (W4M copies `FETIP.*` into `FE.ToolTip`: `FETIP.InsertTeam`, `FETIP.Landscape2`, `FETIP.GameStyle`, `FETIP.Wormpot`) [data], NAV Prev
  (back arrow) and NAV Start (the jar, Normal) at W4M's (264, -166) [data]. The pad's buttons (Switch glyphs / PC keys) are drawn under the strip, white FE text [ours: W4M has
  Select / Back only]. The 3 steps ("1 Équipes", "2 Terrain", "3 Règles", the current one `Button_Yellow`) sit at the top right [ours: CreateAGame is one screen].
- Rows (`feRow` / `feBorder`): W4M's list rows, `fe/border_normal` (Border_Normal 6) / `border_highlight` (9) / `border_disabled` (7), RowHeight 42, tilts 0.06 / 0.04 / 0.02 / 0.01 for the 4
  team rows [data]; the halves of the sprite's circle cap a stretched bar [assumed: how W4M cuts the border]. Label `List_Lable_Blue`, value `List_Data_Orange`, team names `TextBox_Yellow`, the
  highlight `Solid_Black` on the orange bar, `ERROR_RED` for a missing pad [data: WXFE.CreateAGameList columns, gradients]; no outline on the black highlight text [assumed: readability].
  Arrows are `fe/textitem_arrows` halves around an editable value [data: Arrow_* of the list control; their colour assumed].
- Team colour dot at the row's left cap, the label / value split of a row, the alternating tilt of the Rules rows and the pad capsules (disabled border when no pad) [ours].

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
- Left panel: 4 capsules, the 4 Switch player slots: LEDs (pad i lights i + 1 of 4), "Manette i", `padStyle()` name when present, else "L + R pour rejoindre" (disabled border), and one dot per
  team the pad plays (team colour) [ours]. Under them the focused team's worm (below).
- Right: W4M's team list (head `FETXT.Teams`): 4 slots, each a row with the team's colour dot, its name (`TextBox_Yellow`) and its control (local) or its console and ready grenade (room);
  an empty slot reads `FETXT.InsertTeam` (A or X adds a team). The focused team's rows Contrôle (Manette 1..4, then `FETXT.CPU1..5`), Voix (`FETXTSH.Voice`) and Chapeau open right under
  it (list of the visible rows: D-pad up / down walks it; A on the team row edits its name; left / right change a value, or A for Contrôle) [ours].
- X adds a team (empty slot) or removes the focused one (2 teams least). Y on any pad: that pad plays the focused team.
- A human team whose pad is absent: red control value and a red tooltip on the strip. `+` then opens the system controller applet (Switch) with the number of distinct pads used, `-` opens it any
  time; `+` / START open it while a pad is absent and only go on when the applet confirmed with enough pads (`controllerApplet` returns false on a cancel or too few pads, `padsReady`). Then `+` goes to Terrain.
- The focused team's worm model (`worm.glb`, clip `Base`, team tint, hat on `HatLocator`) is drawn live in one `RenderTexture` (256x346), turning slowly, on the left panel [user-requested].
  `Frontend::setupTeams` uploads the pending models each frame (`Models::upload`): a plain launch (Switch) only uploads the menu assets at boot, so the worm and the hats (ranked right after them in
  `Models::start`) were never in `Models` before the first match; `--ui` captures load everything and hid it.

## Screen 2, Terrain [user-requested]
Left: W4M's `LandscapeControl` (head `FETXT.Landscape` and the count): the level picture in its ButtonBig border (`fe/buttonbig_normal`, Border_Normal 15) and the map's name. Right: the maps as a grid of tiles (3 columns,
4 rows visible, scrolls with the selection; no theme filter), each in the same border, the selection in `fe/buttonbig_highlight` (17); D-pad moves, Y a random map (W4M: right mouse = random seed), `+` next, B back.
The grid lists the random island, the W4M Prebuilt landscapes and the Worms 3D maps (user-requested to stay selectable) [data, below]. W4M's list is text with the 4 tab headings, ours a picture grid [user-requested].

## Screen 3, Règles [user-requested]
Right: list rows Schéma (left / right: preset, A: scheme editor), Vers par équipe (1..4), Worm vital (`RULE_KING`), Vampire (`RULE_VAMPIRE`), Gravité basse (`RULE_LOW_GRAVITY`), Mort subite
(`RULE_SUDDEN_DEATH`) as `FETXT.On` / `FETXT.Off`, Wormpot (A: reels, with `fe/icon_wxpot`). The left panel explains the selected row with W4M's text in a FE text box (scrolls when it overflows, like W4M's
`AutoScrollSpeed` boxes): `FETXT.HTPSubtopic24` (scheme, title `FETXT.GameStyle`), `FETXT.WPotHelp.VitalWorm`, `.Vampire`, `.Lowgravity`, `FETXT.HTPSubtopic7`, `FETIP.Wormpot`; Vers par équipe has none.
W4M's `/*token*/` markup is dropped and space runs collapse (`plain`). `+` starts the match (network: below). `RULE_NO_DELAYS` (tests) has no row. The house rules Highlander, Karma and Rope race are removed
[user-requested]; their bits (2, 8, 32) stay unused.

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
- Flow: Réseau local / En ligne (Network menu) -> Salon (`Ui::salon`: Créer un salon / Rejoindre un salon as the Network menu's gold entries with a black brush on the selection, the games as list rows; Rejoindre lists the LAN games heard or the server's rooms,
  R refreshes) -> the 3 screens above. W4M had the same order: GameList / HostGame / GameLobby (docs/w4m/network.md "Lobby flow") [data]. B on Teams leaves the room.
- The Lobby is a view of `Net::Lobby` (`Frontend::fromNet`): `owner[k]` = the slot's console, `self` / `host` = this console / the room's host, `ready[k]` = its console is
  ready. Slot order = colour, join order. This console's teams (`myTeams`, with their local pads) are the source of its own rows; a change goes out as TEAMS at most
  every 0.1 s (`Frontend::toNet`), so remote edits (name, hat, voice) show live on every console. A console edits only its own teams (`canEdit`); CPU control is
  offered to the host only. X adds a team to this console (4 in the room) or removes the focused own one (1 least); several local humans keep one pad each (`GameConfig::Team::pad`,
  re-attached to the match config at Start by `Frontend::minePad`).
- Team rows show the owner console's name ("(vous)" for this one) and the W4M ready grenade: `Nav Start` of `fe2/nav_normal` when the console is ready, `fe2/nav_disabled` when not
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
