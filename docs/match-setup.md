# Match setup (Teams / Map / Rules), local and network

What is on each screen and roughly where comes from the user's mockup (~/Downloads/worms4nx-local-match.html) [user-requested]; the look is W4M's frontend, not the
mockup's: the blue divide, scrolling watermark, Title Control, list-row borders, FE fonts and gradients, the paper strip with its tooltip, NAV Prev / NAV Start
and the tilts of `WXFE.CreateAGame` (MENUTWKX, docs/w4m/frontend.md "Match setup screens (WXFE.CreateAGame)") [data]. W4M splits the same steps: `WXFE.CreateAGame` team
slots (`WXFEP.InsertTeam`), then `WXFEP.SelectLandscape2` (docs/w4m/frontend.md). The texts are W4M's own (`tr()` keys below), the labels without a key are ours (en / fr).

## Look [data + ours]
- Frame of the 3 screens and the Salon (`setupBack` / `setupFront`, ui.cpp): `divide` + `watermark` + `titleControl` as every submenu (title `FETXTH.LOCALGAME` locally, `FETXTH.GameLobby` in a room,
  `FETXTH.NetworkPlay` in the Salon) [data: CreateAGame's AnimDivide$In_Curve, Scrolling 1-4, Title Control]; the panel slides in once, then the 3 steps of the match setup keep it [ours].
  The strip carries the tooltip of the highlighted row (W4M copies `FETIP.*` into `FE.ToolTip`: `FETIP.InsertTeam`, `FETIP.Landscape2`, `FETIP.GameStyle`, `FETIP.Wormpot`) [data], NAV Prev
  (back arrow) and NAV Start (the jar, Normal) at W4M's (264, -166) [data]. No button bar under the strip: the tooltip and NAV Prev / Start carry it, as W4M's ButtonHelp (Select / Back) [ours]. The 3 steps ("1 Équipes", "2 Terrain", "3 Règles", the current one `Button_Yellow`) sit at the top right [ours: CreateAGame is one screen].
- Rows (`feRow` / `feBorder`): W4M's list rows, `fe/border_normal` (Border_Normal 6) / `border_highlight` (9) / `border_disabled` (7), RowHeight 42 [data]; the halves of the sprite's circle cap a stretched bar [assumed: how W4M cuts the border]. Label `List_Lable_Blue`, value `List_Data_Orange`, team names `TextBox_Yellow`, the
  highlight `Solid_Black` on the orange bar, `ERROR_RED` for a missing pad [data: WXFE.CreateAGameList columns, gradients]; no outline on the black highlight text [assumed: readability].
  Arrows are `fe/textitem_arrows` halves around an editable value [data: Arrow_* of the list control; their colour assumed].
- The label / value split of a row and the alternating tilt of the Rules rows [ours].

## State (`Lobby`, client/src/lobby.h) [ours]
- `Lobby::cfg` (a `GameConfig`): teams, `teamSetup[]` (`GameConfig::Team`, below), map, rules, scheme, wormpot, wormsPerTeam.
- `owner[team]` (console id), `ready[]` (per team slot: its console is ready), `self`, `host`. `canEdit(team)`: the team's owner is this console (name, voice, hat, take / release). `used`: the playing slots.
  `canEditMatch()`: this console is the host (map, scheme, rules, worms per team, Wormpot). Local play: one console, `self == host`, every team its own.
- The three screens (`Frontend::setupTeams` / `setupMap` / `setupRules`) draw from the Lobby; START copies it into the match `GameConfig`.
  In a network room the same screens run on a view of `Net::Lobby` (below); there is no other setup screen.
- `GameConfig::Team::pad` (0..3): the pad that plays the team. Local only: not on the wire, not in the checksum. `main.cpp` `teamPad()`: the pad when
  connected, else 0 (desktop keyboard = pad 0); input, HUD and rumble of a team go through it. One pad may play several teams.

## setup.txt [ours]
`teams`, `slots` (bit mask of the playing slots; a file without it plays slots 0..teams-1), `worms`, `rules`, `reels`, `map`, `scheme`, then per slot `team <i> <cpu> <hat id|-> <speech|-> <name>` and `pad <i> <pad>` (the network teams: `net` / `netpad`). An older line with a hat number and a bank folder is read too (`Barracks::legacyHat`, `vo` + folder). A file without `pad` lines:
team i on pad i when that pad is connected, else pad 0. `rules` is masked to the rows the Rules screen shows (King, Vampire, Low gravity, Sudden death).

## Saved teams (`Barracks`, client/src/barracks.h) and the match team [W4M: docs/w4m/frontend.md "Saved teams", docs/w4m/turn.md "Alliances and turn order"]
API for the Teams / My Worms / Story select screens and the network:
- `SavedTeam` = W4M StoredTeamData: `name`, `worms[6]` (names), `skill` (0 human, 1..5 CPU, fixed at creation), `grave` (graves.txt row),
  `flag` (`Flag.*`), `speech` (`vocowbo`...), `weapon` (CustomWeapon: the team's Weapon Factory weapon name), `hat` / `gloves` / `glasses` / `tash`
  (`WXFE.A.*` ids, "" none). `slot(count, alliance, handicap)` gives the match's `GameConfig::Team`.
- `Barracks` (`Frontend::barracks`, loaded by `loadSetup`): `teams` (W4M order, at most `Barracks::MAX` 20), `load(path, defaults, setup)`: `teams.txt`
  (DATA_DIR, beside setup.txt); missing: the W4M defaults `assets/defteams.txt` (DEFSAVE's 7: Gunslingers, Funky Dudes, Spacemen, Scots, CPU Easy 1,
  CPU Normal 3, CPU Hard 5), then once the old setup.txt `team` / `net` teams not already there (no worm names nor flag: ours as before), and
  `loadSetup` saves it. `save(path)`, `find(name)`, `humans()`, `create(name, skill, randomNames, flag, seed)` (W4M SetUpNew: -1 when full, empty or
  taken; names from `tr("FE.RandomWormNames")`, distinct; flag = the language's, W4M `SAVE.Language`: English Flag.UK, French Flag.France; a CPU
  Flag.Terminator), `canDelete(i)` / `remove(i)` (never the last human team). The caller saves after each change.
- teams.txt [ours]: per team `team <name>`, six `worm <name>`, `skill`, `grave`, `flag`, `speech`, `weapon`, `hat`, `gloves`, `glasses`, `tash`
  (value = the rest of the line, may be empty), `end`.
- `catalog()` (`Catalog`, from `tools/w4m-re/setuplists.py`'s assets/*.txt): `attachments` (type 0 hat, 1 gloves, 2 glasses, 3 tash, 4 the
  Terminator hat; id, model, `MeshTextNameID`), `graves` (by GraveIndex: mesh id, model `graves/<x>`, `FETXT.Grave.*`), `flags` (id, ui png `flags/<x>`,
  `FETXT.Flag.*`), `speech` (W4M's 30 rows), `stockWeapons`; `attachment(id)`, `flag(id)`, `drawName(id)` (the Models name). `Ui::hatLabel(id)`,
  `Ui::hatIds()`, `Ui::speechIds()` (the banks we have), `Ui::voiceBankOf(speech)` (for `Audio::setTeamVoice`).
- `GameConfig::Team` (sim.h; on the wire and in replays as `wire::putTeam`): name, cpu, speech, hat, gloves, glasses, tash, flag, grave, wormNames,
  `worms` (1..6, 0 = `cfg.wormsPerTeam`), `alliance` (0..3, -1 = W4M AddTeam's lowest group no earlier team holds: `GameConfig::alliance(t)`),
  `handicap` (-1..1), and the local `pad`. W4M's slot rows: worms 1..6 with the match total <= `MAX_MATCH_WORMS` 16, a new slot min(4, 16 - the
  others), alliance 0..3, handicap -1 / 0 / +1; the Allies and Handicap rows only where W4M allows them (not Destruction, Statue, Fort; Allies not
  Survivor either) [assumed: the row gating, docs/w4m/frontend.md]. The screens agent enforces the cap; the sim takes the counts as given.
- Sim [disasm + data, docs/sim.md "Turn start"]: team-major worms (`Game::teamFirst`, `teamSize`, `wormSlot`), start energy `handicapHealth`,
  alliance queues and the win of the last standing team's alliance; `GameConfig::allyToStart` -1 draws the first alliance from the seed.
- In game [disasm + data; ours: the attachment frame]: the hat, glasses and tash at `HatLocator` (as the hat; W4M's Offsets lists not applied; the glasses sit on the eyes and the tash under
  the nose [observed: `--animshot Base glasses/star` with `W4NX_LOC=HatLocator`]),
  the grave of the team's GraveIndex, the team flag on its energy bar; worm names from `wormNames`, else ours.
- Story progress per team (W4M StoredTeamData StoryChapter / StoryMissionsCompleted, docs/w4m/frontend.md): progress.txt is still one file; the
  plan for the Story select agent: the picked team's name goes to `Progress::load` / `save` as `progress_<team>.txt` for the mission entries, the
  `unlock` / `intro` / `welcome` lines stay in progress.txt (W4M Lock.* are the profile's), the current progress.txt entries become the first human
  team's on first run.

## Screen 1, Teams [user-requested layout (mockup worms4nx-teams-v2.html), W4M skin]
Pixels of the 1280 x 720 screen. 4 player slots side by side (`Frontend::teamSlot`, x 40 + k x 303.5, y 196, 289.5 x 322), W4M's blue text box (blue half of `fe/text_border_list`, 9-slice) as backdrop
[data: sprite, assumed: its use as a slot]; the ribbon "Joueur N" over the top edge (rounded, black outline, tilted) is the colour the team will have in the match (rank among the playing slots), grey on a free slot [ours].
- Tokens (Smash-style coins): one per connected pad (`TeamCursors::slot`, lobby.h), 48 px discs at the slot's top-right corner, several side by side on one slot, gliding to their slot; the fill is the controller body colour
  (`padBody`: `Controls::padColors`, libnx `hidGetNpadControllerColorSingle` / `Split`), two Joy-Cons = `mixColors`, the average of the two colours (blue + red = purple), grey when the system gives none; the number is the pad's. Each pad moves its own
  token at the same time as the others: L / R (PageUp / PageDown on the keyboard = pad 0) always, ◀ ▶ too on a slot the pad does not play [ours].
- A on a free slot: the pad takes it (human team, `GameConfig::Team::pad`); a pad may take several (`Lobby::take`). On a slot the pad plays: ◀ ▶ change the hat live on the worm (`Lobby::hat`), A opens the name / voice panel (A on Nom edits it: swkbd on Switch,
  inline on desktop; ◀ ▶ or A on Voix change the voice; up / down change row; B closes), B releases the slot (`Lobby::release`; a CPU slot of this console too). B with nothing to release goes back (Local menu, or leaves the room). Y on a free slot makes
  it a CPU level 1, Y again goes up one level, 5 wraps to 1 (`Lobby::cpuStep`). A slot with neither pad nor CPU does not play: Start (`+`) needs 2 playing slots locally (red tooltip otherwise).
  The match is `Lobby::match()`: the playing slots in order, so the colours are the ranks [ours].
- Slot content: "Manette N - <device>" (`padStyle`, with the `padDot` of the controller colour; "Manette N absente" in `ERROR_RED` when the pad of this console's human slot is gone) or the `FETXT.CPU1..5` text; the team name (`TextBox_Yellow`);
  its own live worm (`worm.glb`, clip `Base`, untinted, hat on `HatLocator`, one `RenderTexture` 256 x 346 per slot, turning slowly) 185 x 250 centred, overflowing the slot by 36 px below; the hat name in a black tag at the foot, ◀ ▶
  (`fe/textitem_arrows`) beside it only on the human slots of this console. A free slot: the A and Y button glyphs (key caps on desktop) with "pour jouer" / "pour une IA" (the host only offers Y in a room).
  `Frontend::setupTeams` uploads the pending models each frame (`Models::upload`): a plain launch (Switch) only uploads the menu assets at boot [ours].
- Button legend under the paper strip (`teamsLegend`): A Prendre / modifier, ◀ ▶ Chapeau, Y IA, B Lâcher, + Suivant; the strip's tooltip says how to move the token (L / R). A missing pad of a human slot turns the tooltip red; `+` then opens the system
  controller applet (Switch) with the number of distinct pads used, `-` opens it any time; `+` / START open it while a pad is absent and only go on when the applet confirmed with enough pads (`controllerApplet`, `padsReady`). Then `+` goes to Terrain.
- Local slots are sparse (`Lobby::used`, `cfg.teams` = 4 while editing); setup.txt keeps the mask. The step indicator ("1 Équipes", "2 Terrain", "3 Règles") sits under the title's underline so a long title never covers it [ours].
- Tests: `--ui setup2` scripts two pads (`Frontend::simPads` / `Frontend::sim`: pad 1 frees slot 2, pad 2 takes it and makes slot 3 a CPU level 2, pad 1 goes back to slot 1 and changes its hat); `setup2panel` also opens its panel; `ui_check` covers
  `Lobby` slots, `TeamCursors` and `mixColors`.

## Screen 2, Terrain [user-requested layout, W4M skin]
Left: `FETXT.Landscape` and the count, the big level picture (430 x 280, cropped to fill) in its ButtonBig frame with the map's name on its foot (white, over a dark gradient). Right: the maps as a grid of tiles (3 columns,
4 rows visible, scrolls with the selection; no theme filter), each with its own frame, the selection in the highlight frame; D-pad moves, Y a random map (W4M: right mouse = random seed), `+` next, B back.
The frame (`pictureFrame`) is the sprite `fe/buttonbig_normal / highlight` (512 x 512, frame 33 / 37 / 41 / 40 px thick, 44 / 46 / 52 / 50 highlighted, picture hole 420 px) cut as a 9-slice and fitted so its
hole is the picture's rect, scale = picture's short side / 420 [data: sprite measurements; assumed: W4M's border placement around the picture]. W4M's LandscapeControl is 95 x 95 FE at (-174, 38) with borders 15 / 17 / 16 (docs/w4m/frontend.md) [data];
how `WXFE_Border` sizes the sprite to a rect is not decoded. The jar (NAV Start) of the strip is the "next" button.
The grid lists the random island, the W4M Prebuilt landscapes and the Worms 3D maps (user-requested to stay selectable) [data, below]. W4M's list is text with the 4 tab headings, ours a picture grid [user-requested].

## Screen 3, Règles [user-requested layout, W4M skin]
Left: list rows (`feRow`, 580 x 34) Schéma (left / right: preset, A: scheme editor), Vers par équipe (1..4), Worm vital (`RULE_KING`), Vampire (`RULE_VAMPIRE`), Gravité basse (`RULE_LOW_GRAVITY`), Mort subite
(`RULE_SUDDEN_DEATH`) as `FETXT.On` / `FETXT.Off`, Wormpot (A: reels, with `fe/icon_wxpot`). Right: the help panel (`fe/com_panel`) with the selected row's title and W4M's text in a FE text box (scrolls when it overflows, like W4M's
`AutoScrollSpeed` boxes): `FETXT.HTPSubtopic24` (scheme, title `FETXT.GameStyle`), `FETXT.WPotHelp.VitalWorm`, `.Vampire`, `.Lowgravity`, `FETXT.HTPSubtopic7`, `FETIP.Wormpot`; Vers par équipe has none.
W4M's `/*token*/` markup is dropped and space runs collapse (`plain`). `+` starts the match (network: below). `RULE_NO_DELAYS` (tests) has no row. The house rules Highlander, Karma and Rope race are removed
[user-requested]; their bits (2, 8, 32) stay unused.

## Hat names and map list [data + ours]
- Hat names: `WORMATTACHMENTS.XOM` `WXFE.A.Hat.*` holds `MeshID` (`Hat.Baseball.C`) and `MeshTextNameID` (`FETXT.Hat.Baseball.Camo`); the hat file is the
  lower-cased MeshID without `Hat.`, `.` -> `_` (`baseball_c.glb`) [data]. `tools/w4m-re/setuplists.py` writes `assets/attachments.txt`; a team's hat is its
  `WXFE.A` id (`Ui::hatIds`, W4M order), `Ui::hatLabel` shows the W4M text. The jetpack helmet (`jetpack.glb`) is no `WXFE.A.Hat` entry: it is no team hat [data].
- Map list: `WXFEP.SelectLandscape2` (MENUTWKX) is built by TablePopulationService 0x4cf620 as four tabs: `FETXT.Random2` (random island), `FETXT.Prebuilt`,
  `FETXT.Saved`, `FETXT.Worms3DSection` [disasm 0x4cf620, REA decompile]. The Prebuilt landscapes are the 74 SCRIPTS.XOM `WXFE_LevelDetails` of `Level_Type` 0
  (`Multi.*`; `Level_FileName`, e.g. `Multi_CarpetCapers`, `ChallengeCrate` for Desert Oasis, `Alien-w3d`, `RandomArabian`..., which are procedural) [data]. Our list
  (`Ui::multiplayerMaps`, `assets/multimaps.txt` from `setuplists.py`) keeps those that exist as imported maps, the random island and every `*-w3d` map
  (user-requested, docs/w4m/README.md). How W4M maps its tabs to the 4 `Level_Type`s of Destruction / Statue / Fort / Survivor lists (types 1, 2, 11, 3) was not traced [assumed: not offered here].

## Network room [ours unless tagged]
- Flow: Réseau local / En ligne (Network menu) -> Salon (`Ui::salon`: Créer un salon / Rejoindre un salon as the Network menu's gold entries with a black brush on the selection, the games as list rows; Rejoindre lists the LAN games heard or the server's rooms,
  R refreshes; the panel, the entries and the list rows glide in with `rowAppear` and `FeNextIn` as the other menus, `Salon::t0` / `t1`; the status line goes through `tr()`) -> the 3 screens above. W4M had the same order: GameList / HostGame / GameLobby (docs/w4m/network.md "Lobby flow") [data]. B on Teams leaves the room.
- The Lobby is a view of `Net::Lobby` (`Frontend::fromNet`): `owner[k]` = the slot's console, `self` / `host` = this console / the room's host, `ready[k]` = its console is
  ready. Slot order = colour, join order; free slots follow. This console's teams (`myTeams`, with their local pads) are the source of its own rows; a change goes out as TEAMS at most
  every 0.1 s (`Frontend::toNet`), so remote edits (name, hat, voice) show live on every console. A console's pads take / edit only its own players (`canEdit`), the other consoles' players are read-only; CPU is
  offered to the host only. A on a free slot appends a team to this console (4 in the room, it shows on the first free slot and the token follows), B removes it (later slots shift up); several local humans keep one pad each (`GameConfig::Team::pad`,
  re-attached to the match config at Start by `Frontend::minePad`).
- A remote player's slot shows the owner console's name in place of the pad, and every slot the W4M ready grenade (top-left): `Nav Start` of `fe2/nav_normal` when the console is ready, `fe2/nav_disabled` when not
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
