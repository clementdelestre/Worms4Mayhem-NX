# Rounds, statistics and the match-end screens (ours)

W4M side: `docs/w4m/frontend.md` "Match-end screens", `docs/w4m/stats.md` §27, `docs/w4m/turn.md` (win rules). Tags: [data] [disasm] [assumed] [ours].

## Rounds
- `Scheme::wins` (last byte of the scheme, default 2, Family 1) is W4M `SchemeData.Wins` [data]; the scheme page's last row `Number of Rounds` (1..9, wraps) edits it [ours: W4M lists it first].
- `Results` (results.h) lives across the rounds of one match (main.cpp, `results`): `begin()` at a new match, `finish()` on the sim's `GameOver` event: `round` + 1, +1 round win to every team of the winner's alliance, `FE.WinString` (`Ui::resultBanner`), `over` when the winner's round wins reach `wins` [disasm turn.md]. Online matches always end (one win) and a draw is `DrawMatch` [disasm].
- `StartGame$NextRound`: `startMatch` again with the same `GameConfig` (teams, landscape, seed) plus `GameConfig::round`, which mixes into the game RNG and the first alliance draw so the round plays differently on the same landscape [ours: W4M's RNG keeps running; the landscape seed is only changed by PreRound's picker]. `round` is not sent: online has one round.
- Game-over sequence: the sim's `GameOver`, then W4M's GameOverLogicEntity timing (4 s, the orbit 5 s or any input, 1 s) [disasm main.cpp], then `Screen::Results`: WinMatch when the match is over, else WinRound (a draw too); network: WinMatch / DrawMatch.

## Statistics
- `Results::event` (called from `stepOnce` for every sim event) books the W4M rules of docs/w4m/stats.md §27.3 into `current` (RoundStats); `finish()` merges it into `match` (sum, Survival the greater) [disasm]. Source of each: `Fire` (shots, weapon shots by W4M weapon id from `Game::containerOf`; Landmine, Flood, Sentry Gun and Skip Go excluded, Skip Go counts a boring turn), `Hurt` with `GameEvent::count` = the hp lost (`Game::hurt`, poison 6 excluded; the active worm is the attacker; the first hurt after a shot counts it as damaging), `Death` (kills: the active worm; itself, its team or its alliance: team kill), `Collect` / `Mystery` (crates of the active worm), the survival time at a death and, for the living, at the round's end.
- `summarise()` builds the WinRound rows and the match pages (docs/w4m/stats.md §27.5) over the match's worms only (the configured teams' worms) [disasm]; the shuffle uses a time seed [ours: W4M's rand()].
- [assumed] team damage / kills are the sum of its worms' stats (the worm to team fold 0x688f4c is not decoded).

## Screens (results.inc, drawn by ui.cpp's namespace Ui; `Ui::results`)
- WinMatch, WinRound, PreRound, the WXNET.WinMatch / DrawMatch variants (`ResultsScreen::net`) from the item lists of the frontend doc: Paper Fader veil, the `bluedivide` mesh through `feMesh` (In_Split / Out_Split), watermarks, headers, tickers with the highlighted item's tooltip, fireworks (the `WXP_FE_Firework1..6` effects started at the data's FE positions and delays, drawn in the FE camera over the veil: `FrontBg::draw(dt, false)` leaves the particles to this screen), stat pages with Previous / Next Team, the `PreRoundList`, the landscape picture and its `SelectLandscape2` popup (a list of the multiplayer maps; a pick changes the next round's landscape) and `ConfirmMatchQuit`.
- Text outlines follow the data's Colour_Back (docs/w4m/frontend.md, outline colours): kBC_Black on every header, label and list column (HeaderBottom + HeaderTop drawn as one fill on a black outline), kBC_ScrollText on the scrolling `FE.WinString` band text [data + disasm].
- Inputs: D-pad / stick between the items by their `Navigate_*`, A selects, B = Cancel (WinMatch leaves, WinRound / PreRound ask). Leaving goes to the main menu (a room: back to the room) [disasm].
- Captures: `--ui winmatch | winmatchally | winmatchnet | winround | winroundally | preround | preroundally | preroundland` with the flag file `ui <state> <frames>` (made-up stats; frame ~200 is settled) [ours].
