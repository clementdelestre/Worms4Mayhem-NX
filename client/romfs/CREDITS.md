# Credits

All bundled default audio is CC0 (public domain) or synthesized in-house. None of it comes
from Team17's Worms 4: Mayhem — that's the importer's job, dropped into
`assets/` at runtime, never bundled here.

## Kenney.nl (CC0, https://creativecommons.org/publicdomain/zero/1.0/)

Credit is not mandatory for CC0 but is appreciated — see www.kenney.nl.

- `sfx/explosion.ogg`, `sfx/big_explosion.ogg` — Kenney "Sci-fi Sounds" (explosionCrunch_000, lowFrequency_explosion_000)
- `sfx/fire.ogg` — Kenney "Sci-fi Sounds" (laserSmall_000)
- `sfx/bounce.ogg` — Kenney "Impact Sounds" (impactGeneric_light_000)
- `sfx/turn_start.ogg` — Kenney "Interface Sounds" (bong_001)
- `sfx/tick.ogg` — Kenney "Interface Sounds" (tick_001)
- `voices/male/*`, `voices/female/*` — Kenney "Voiceover Pack #1"
  (war_fire_in_the_hole, war_medic, mission_failed, you_win, go, ready).
  Male voice: Jeffrey M. Smith (fiverr.com/jeffreymsmith). Female voice: Giselle (fiverr.com/easymedia).

Source packs: kenney.nl/assets/impact-sounds, /interface-sounds, /sci-fi-sounds,
/digital-audio, /voiceover-pack.

## Synthesized in-house (ffmpeg, no external source — CC0 by this project)

- `sfx/splash.ogg` — low-passed brown noise burst with fast decay
- `sfx/sheep.ogg` — two-tone sine bleat, pitched up for comic effect
- `sfx/holy.ogg` — ascending 3-note sine chime (C-E-G)

## Lua 5.0.1 (MIT, https://www.lua.org)

`third_party/lua-5.0.1`, the official lua.org source, runs the W4M mission scripts (patched for their bytecode:
`tools/patches/lua-5.0.1-w4m.patch`). Copyright (C) 2003 Tecgraf, PUC-Rio. Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
