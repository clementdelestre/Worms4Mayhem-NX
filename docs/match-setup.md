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
- `Lobby::cfg` (a `GameConfig`): teams, `teamSetup[]` (`GameConfig::Team`, below), map, rules, scheme, wormpot, wormsPerTeam (a fallback: every team placed on the Teams screen carries its own worm count).
- `owner[team]` (console id), `ready[]` (per team slot: its console is ready), `self`, `host`. `canEdit(team)`: the team's owner is this console (its slot settings, remove). `used`: the playing slots.
  `canEditMatch()`: this console is the host (map, scheme, rules, Wormpot). Local play: one console, `self == host`, every team its own.
- The three screens (`Frontend::setupTeams` / `setupMap` / `setupRules`) draw from the Lobby; START copies it into the match `GameConfig`.
  In a network room the same screens run on a view of `Net::Lobby` (below); there is no other setup screen.
- `GameConfig::Team::pad` (0..3): the pad that put the team in its slot and plays it (a CPU team uses none). Local only: not on the wire, not in the checksum. `main.cpp` `teamPad()`: the pad when
  connected, else 0 (desktop keyboard = pad 0); input, HUD and rumble of a team go through it. One pad may play several teams.

## setup.txt [ours]
`rules`, `reels`, `map`, `scheme` (the Rules screen's masked bits, Wormpot reels, map name, Scheme bytes), then per team in a slot `slot <k> <pad> <worms> <alliance> <handicap> <team name>`: the next
local match starts with those saved teams (a name no longer in teams.txt is skipped). Older `team` / `net` lines (and `teams`, `slots`, `worms`, `pad`, `netpad`) are not read except by `Barracks::load`'s one-time
migration of the teams (below); the next save drops them.

## Saved teams (`Barracks`, client/src/barracks.h) and the match team [W4M: docs/w4m/frontend.md "Saved teams", docs/w4m/turn.md "Alliances and turn order"]
API for the Teams / My Worms / Story select screens and the network:
- `SavedTeam` = W4M StoredTeamData: `name`, `worms[6]` (names), `skill` (0 human, 1..5 CPU, fixed at creation), `grave` (graves.txt row),
  `flag` (`Flag.*`), `speech` (`vocowbo`...), `weapon` (CustomWeapon: the team's Weapon Factory weapon name), `hat` / `gloves` / `glasses` / `tash`
  (`WXFE.A.*` ids, "" none). `slot(count, alliance, handicap, factory)` gives the match's `GameConfig::Team`, its `secret` = the `factory` weapon
  (the player's custom_weapons.json list) named `weapon` [disasm: AddTeam copies SecretWeapon; ours: only the player's own weapons, the stock
  ones (LOCAL DATA.LockedWeapons, `Catalog::stockWeapons`) have no WeaponDef: a team naming one gets none].
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
  `handicap` (-1..1), `secret` (its Weapon Factory weapon, W4M Tn_CustomWeapon; replays W4R6), and the local `pad`. W4M's slot rows: worms 1..6 with the match total <= `MAX_MATCH_WORMS` 16, a new slot min(4, 16 - the
  others), alliance 0..3, handicap -1 / 0 / +1; the Allies and Handicap rows only where W4M allows them (not Destruction, Statue, Fort; Allies not
  Survivor either) [assumed: the row gating, docs/w4m/frontend.md]. The screens agent enforces the cap; the sim takes the counts as given.
- Sim [disasm + data, docs/sim.md "Turn start"]: team-major worms (`Game::teamFirst`, `teamSize`, `wormSlot`), start energy `handicapHealth`,
  alliance queues and the win of the last standing team's alliance; `GameConfig::allyToStart` -1 draws the first alliance from the seed.
- In game [disasm + data; ours: the attachment frame]: the hat, glasses and tash at `HatLocator` (as the hat; W4M's Offsets lists not applied; the glasses sit on the eyes and the tash under
  the nose [observed: `--animshot Base glasses/star` with `W4NX_LOC=HatLocator`]),
  the grave of the team's GraveIndex, the team flag on its energy bar; worm names from `wormNames`, else ours. Gloves as W4M (docs/w4m/frontend.md
  "Gloves on the worm"): `Models::gloves(name)` around the worm's draw (drawWorm, `Frontend::wormPreview`, `--animshot` with `W4NX_GLOVES`) hides
  the worm's two hand meshes and draws the glove model's hand meshes skinned on the worm's bone matrices of that pose, glove bones matched by
  name (`drawGloves`, models.cpp); w4m-models exports gloves skinned (raw vertices, joints, a one-frame `Rest`) and tags hand shapes `H <mesh>`
  in the glb `sel` extras [observed: `--animshot Fire2Firepunch` / `HoldBazooka` / `WindupThrown` with gloves/cowboy / white / spacesuit, on the fists in every pose; Base keeps the hands hidden, as without gloves].
- Weapon Factory weapon per team [disasm: docs/w4m/weapons.md "Whose weapon"]: one inventory slot after the weapons.json table, present when a
  team has a `secret`; `Game::secrets[t]` holds each team's (factoryPayload applied) and `beginTurn` puts the playing team's in the slot
  (`wieldSecret`). Ammo: the team's own weapon's `count` under the scheme set (ours: the editor's Ammo row, W4M SchemeData WeaponFactoryWeapon.Ammo);
  a crate holding the slot credits the collector's slot, which fires the collector's own weapon. A team without one never fires it (`allowed`) [ours].
- Story progress per team [W4M StoredTeamData StoryChapter / StoryMissionsCompleted, docs/w4m/frontend.md; ours: the files]: `Progress::open(profile, teamFile)`: `progress.txt` keeps the profile's `unlock <Lock.*>`, `intro`,
  `welcome` lines (W4M Lock.* and movie flags are the profile's), the mission entries of the picked team are in `progress_<team>.txt` (`Progress::teamFile`: name bytes outside A-Za-z0-9_- as `%XX`); `save()`
  writes both. `Progress::migrate` moves the entries of an older single progress.txt to the first human team's file (done by `Frontend::missionTeamName()` before any open). `Frontend::missionTeam` is the team
  of the last pick (W4M `WXFE.MissionTeamName`); the Challenges SUBHEADER shows it.
- Lists [data docs/w4m/frontend.md "Match setup slot pick", "Single player WXFEP.SelectTeam"]: `Barracks::list(Insert | Human | Edit | Delete, placed, cpuOk)` gives a popup's rows (Insert: not already in a slot, CPU
  teams only where `cpuOk`; Human: Skill 0; Edit: all; Delete: never the last human team), `canCreate()` the Create Team row (< 20 teams). `Catalog::usable(type, unlocks, current)`: the attachments the editor
  offers: DEFSAVE `WXFE_UnlockableItem` State 2 (Unlocked) by default (column 7 of attachments.txt), or its `Lock.*` in the save's `unlock` lines; the team's current one stays [data + ours].
- `Lobby` slots: `place(k, pad, team)` (a free slot of this console), `release(k)`, `wormsIn()`, `newWorms()` (min(4, 16 - the worms in), < 1 = the match is full), `maxWorms(k)` (the row cap), `freeAlliance()`
  (0x737633) [disasm, docs/w4m/frontend.md "Filled slot settings"].

## Screen 1, Teams [user-requested layout (mockup worms4nx-teams-v2.html), W4M skin]
Pixels of the 1280 x 720 screen. 4 player slots side by side (`Frontend::teamSlot`, x 40 + k x 303.5, y 196, 289.5 x 322), W4M's blue text box (blue half of `fe/text_border_list`, 9-slice) as backdrop
[data: sprite, assumed: its use as a slot]; the ribbon "Joueur N" over the top edge (rounded, black outline, tilted) is the colour the team will have in the match (rank among the playing slots), grey on a free slot [ours].
- Tokens (Smash-style coins): one per connected pad (`TeamCursors::slot`, lobby.h), 48 px discs at the slot's top-right corner, several side by side on one slot, gliding to their slot; the fill is the controller body colour
  (`padBody`: `Controls::padColors`, libnx `hidGetNpadControllerColorSingle` / `Split`), two Joy-Cons = `mixColors`, the average of the two colours (blue + red = purple), grey when the system gives none; the number is the pad's. Each pad moves its own
  token at the same time as the others: L / R (PageUp / PageDown on the keyboard = pad 0) always, ◀ ▶ too on a slot the pad does not play [ours].
- A on a free slot opens the saved-team list for that pad (W4M `WXFEP.InsertTeam`: the saved teams not already in a slot, CPU teams too [data: hidden in Destruction / Statue / Fort, modes we do not have], in a room the CPU ones for the host only [ours],
  then "Créer une équipe" while the barracks hold < 20 teams); A on a row puts the team there (`Frontend::placeTeam`: min(4, 16 - the worms in) worms, the lowest free alliance, handicap 0), the pad that picked it plays it (a CPU team needs no pad).
  Create Team runs W4M's create flow (below) and then places the new team. With the match at 16 worms no list opens (FeError). A on a team of this console opens its slot settings (below); B releases it (W4M Remove Team), B with nothing to release
  goes back (Local menu, or leaves the room). A slot with no team does not play: Start (`+`) needs 2 playing slots locally (red tooltip otherwise).
  The match is `Lobby::match()`: the playing slots in order, so the colours are the ranks [ours].
- Slot content: "Manette N - <device>" (`padStyle`, with the `padDot` of the controller colour; "Manette N absente" in `ERROR_RED` when the pad of this console's human slot is gone) or the `FETXT.CPU1..5` text; the saved team's flag (`flags/<x>`)
  and name (`TextBox_Yellow`); its own live worm (`worm.glb`, clip `Base`, untinted, the team's hat / glasses / tash on `HatLocator`, one `RenderTexture` 256 x 346 per frame, turning slowly) 185 x 250 centred, overflowing the slot by 36 px below;
  a black tag at the foot: the worm count, "Groupe N" when another slot shares the alliance, the handicap when set. A free slot: the A glyph (key cap on desktop) with "pour choisir une équipe".
  `Frontend::setupTeams` uploads the pending models each frame (`Models::upload`): a plain launch (Switch) only uploads the menu assets at boot [ours]. The gloves are not drawn on the preview (their skinned model is the models agent's) [open].
- Slot settings [W4M `WXFEP.TeamSettingsList`, docs/w4m/frontend.md]: title = the team's name; rows worms (1..`maxWorms`, wrapping, the match stays <= 16), allies group (1..4, `FETXT.AlliedColour`; the row text "1".."4" stands for the
  `WXFE.TeamInfo01:0..3` icons [assumed]), handicap (-1 / 0 / +1), Remove Team, Accept. Changes apply at once; in a room they go out with the team card. Allies and Handicap are always offered [the modes that hide them do not exist here].
- Button legend under the paper strip (`legend()`): A Choisir / réglages, ◀ ▶ Jeton, B Retirer une équipe, + Suivant; the tooltip is `FETIP.InsertTeam`. A missing pad of a human slot turns the tooltip red; `+` then opens the system
  controller applet (Switch) with the number of distinct pads used, `-` opens it any time; `+` / START open it while a pad is absent and only go on when the applet confirmed with enough pads (`controllerApplet`, `padsReady`). Then `+` goes to Terrain.
- Local slots are sparse (`Lobby::used`, `cfg.teams` = 4 while editing); setup.txt keeps the placed teams. The step indicator ("1 Équipes", "2 Terrain", "3 Règles") sits under the title's underline so a long title never covers it [ours].
- Popups (`Frontend::Pop`, drawn by `popDraw`): modal, one at a time, W4M's paper popup, the title in Button_Yellow over its underline, the rows `feRow`; the Teams screen's belong to the pad that opened them, the others to any pad.
  Create Team [data docs/w4m/frontend.md "Create"]: `FETXT.CPUControl` Yes / No (No default; skipped where CPU teams are off: Story / online lists), the CPU level 1..5, the name (`FETXT.EnterTeamName`: swkbd on Switch, typed inline on
  desktop; an empty or taken name is refused), `Barracks::create`, saved to teams.txt, then the editor.
- Tests: `--ui setup2` scripts two pads (`Frontend::simPads` / `Frontend::sim`); `--ui setup3` places Gunslingers (pad 1), Funky Dudes (pad 2) and CPU Easy, `setup3list` stops with the InsertTeam list open,
  `setup3slot` opens slot 1's settings; `captureState` (`selectteam`, `createask`, `teamedit1..3`) reaches the states the pads cannot click to; `W4NX_INPUTSCRIPT` drives the real menus (key events; the type-3 line `<frame> <char> 2` types a character
  through `Frontend::simText`). `ui_check` covers `Lobby` slots and the 16-worm caps, `Barracks::list`, `Catalog::usable`, `Progress` per team and its migration.

## Mes Worms and the team editor [W4M `WXFE.Customise`, `WXFE.TeamOptions`; docs/w4m/frontend.md "Saved teams"]
- Mes Worms (`MyWorms`): three gold entries Créer une équipe / Éditer une équipe / Supprimer une équipe (`FETXT.CreateTeam / EditTeam / DeleteTeam`). Create runs the create flow above; Edit lists every team (CPU ones too, `FETXT.SelectTeamEdit`); Delete
  lists the teams but the last human one (`FETXT.SelectTeamDelete`) and asks `FETXT.ConfirmDeleteThis`. The CPU level is chosen at creation only; the editor has no controller row [data].
- Editor (`TeamEdit`, `Frontend::editor`, the title `FETXTH.TeamDesign` [assumed], the divide and watermark of the submenus): the team's name (SUBHEADER Teamname) and its live worm on the left, a paper panel on the right with the page's rows [data: items of
  `WXFE.TeamOptions`; the right-hand positions are ours], `n/3` and the page arrows at W4M's (60 / 124 / 190, -116). L / R (PgUp / PgDn) change page, up / down the row, ◀ ▶ change a value, A edits a worm name (page 1) or opens the row's list, `+` (NAV Start) accepts,
  B cancels (asks `Abandonner les modifications ?` when something changed). It edits a copy; Accept writes the saved team and teams.txt.
  Page 1: the 6 worm names. Page 2: Soundbank (`WXFEP.SelectSoundbank`: the banks we have, one row per bank; the rows W4M shares between banks show the plain `FETXT.SB.<bank>` name [assumed]), Test Speech (`FE.PlayRandomSpeech`: a random line of the
  bank on team 0), Head / Eyes / Face / Hands (hat / glasses / tash / gloves, `Catalog::usable`: the Lock.* of attachments.txt apply). Page 3: Team Weapon (the stock Weapon Factory names and ours), Flag (`FETXT.Flag.*`, its picture below), Grave (`FETXT.Grave.*`).
  Lists scroll, the row under the highlight shows live on the worm, B restores the old value. The worm plays `Salute` on the Flag row and `Pray` on the Grave row [data: the rows' Highlighted messages `WXD.WormAnim`]; `InspectHands` (Hands row) is not played.
  The gloves are not drawn on the worm (skinned model: the models agent's) [open]. The grave has no preview [assumed: not needed].

## Team pickers [W4M `WXFEP.SelectTeam`, docs/w4m/frontend.md "Single player WXFEP.SelectTeam", "Online"]
- Story and Challenges (Local menu): the human saved teams then "Créer une équipe" (no CPU question), title `FETXT.SelectStoryTeam` / `SelectChallengeTeam`; the pick sets `missionTeam` and goes to the book / list, whose progress is that team's (above).
  After a created team's editor the list opens again. There is no Tutorial entry in our Local menu.
- Online / LAN (Network menu, before the salon): `FETXT.SelectOnlineTeam`, human teams; the pick (`onlineTeam`) is the team this console brings (W4M `WXNET.NetLocalTeamIndex`); `enterRoom` starts `myTeams` with its full card (`SavedTeam::slot`: outfit, flag,
  grave, worm names, worms 4), sent as TEAMS (`wire::putTeam`), so every console shows the other teams' name, flag and outfit live. More teams can be added with A on a free slot (the same list).

## Screen 2, Terrain [user-requested layout, W4M skin]
Left: `FETXT.Landscape` and the count, the big level picture (430 x 280, cropped to fill) in its ButtonBig frame with the map's name on its foot (white, over a dark gradient). Right: the maps as a grid of tiles (3 columns,
4 rows visible, scrolls with the selection; no theme filter), each with its own frame, the selection in the highlight frame; D-pad moves, Y a random map (W4M: right mouse = random seed), `+` next, B back.
The frame (`pictureFrame`) is the sprite `fe/buttonbig_normal / highlight` (512 x 512, frame 33 / 37 / 41 / 40 px thick, 44 / 46 / 52 / 50 highlighted, picture hole 420 px) cut as a 9-slice and fitted so its
hole is the picture's rect, scale = picture's short side / 420 [data: sprite measurements; assumed: W4M's border placement around the picture]. W4M's LandscapeControl is 95 x 95 FE at (-174, 38) with borders 15 / 17 / 16 (docs/w4m/frontend.md) [data];
how `WXFE_Border` sizes the sprite to a rect is not decoded. The jar (NAV Start) of the strip is the "next" button.
The grid lists the random island, the W4M Prebuilt landscapes and the Worms 3D maps (user-requested to stay selectable) [data, below]. W4M's list is text with the 4 tab headings, ours a picture grid [user-requested].

## Screen 3, Règles [user-requested layout, W4M skin]
Left: list rows (`feRow`, 580 x 34) Schéma (left / right: preset, A: scheme editor), Worm vital (`RULE_KING`), Vampire (`RULE_VAMPIRE`), Gravité basse (`RULE_LOW_GRAVITY`), Mort subite
(`RULE_SUDDEN_DEATH`) as `FETXT.On` / `FETXT.Off`, Wormpot (A: reels, with `fe/icon_wxpot`). Right: the help panel (`fe/com_panel`) with the selected row's title and W4M's text in a FE text box (scrolls when it overflows, like W4M's
`AutoScrollSpeed` boxes): `FETXT.HTPSubtopic24` (scheme, title `FETXT.GameStyle`), `FETXT.WPotHelp.VitalWorm`, `.Vampire`, `.Lowgravity`, `FETXT.HTPSubtopic7`, `FETIP.Wormpot`.
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
  every 0.1 s (`Frontend::toNet`), so remote edits (worms, group, handicap) show live on every console with the team's flag and outfit. A console's pads place / set only its own teams (`canEdit`), the other consoles' teams are read-only; CPU teams are
  offered to the host only. A on a free slot lists this console's saved teams and appends the pick (4 in the room, it shows on the first free slot and the token follows), B removes it (later slots shift up); several local humans keep one pad each (`GameConfig::Team::pad`,
  re-attached to the match config at Start by `Frontend::minePad`).
- A remote player's slot shows the owner console's name in place of the pad, and every slot the W4M ready grenade (top-left): `Nav Start` of `fe2/nav_normal` when the console is ready, `fe2/nav_disabled` when not
  (WXNET `Team<n>_Ready`, docs/w4m/network.md) [data, flag-to-state rule assumed there].
- Terrain and Règles: the host edits (host edits `net->lobby`, `pushLobby()` when it changed); the others see them read-only with a note, and the help panel stays readable.
- Ready: ZR / R on any screen, or `+` on Règles for a non-host; Règles' button reads PRÊT / PAS PRÊT, and START for the host once `Net::allReady()` and at least 2 teams
  (a host's `+` that cannot start toggles its own ready). The lobby timer (`ReadyTimer`, lobby.h; `main.cpp`) follows W4M: 30 s once 2 players are there, restarted by each join,
  off under 2 players, shown in the heading disc as `%.2d` / `--`; at its end the console sets itself ready [data, docs/w4m/network.md]. START stays the host's press
  (W4M's Steam gate starts by itself) [ours]. `W4NX_LOBBY_TIMER=<s>` shortens it for tests.
- A change of map / rules / scheme / wormpot leaves the ready flags as they are, as W4M's `LobbyDataChangeMsg` handler does (docs/w4m/network.md). A console arms its teams with its Weapon Factory weapons (`SavedTeam::slot(..., customs)`) before `sendTeams`.
- After a match the room is shown again (`Frontend::backToRoom`), own ready cleared.
- Tests: `--ui netteams|netmap|netrules[_ro]` (host / guest view of a 3-console room), `--ui salon|salonjoin`; `tests/netbot.sh` (2 consoles: the guest edits its team, the host sees it
  and pushes map and rules, the guest sees them, the host's ready comes from the timer, START, checksums).
