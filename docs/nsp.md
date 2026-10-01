# Installable NSP (application mode)

Packs Worms4NX as an application NSP (its own icon/name on the HOME menu, full
application memory budget) instead of the homebrew `.nro`.

## Prerequisites
- Atmosphère CFW with sigpatches (the NSP's ACID signature is self-signed by
  hacBrewPack, not Nintendo — sigpatches make the loader accept that).
- `prod.keys` dumped from your own console (never committed, never printed).
- DBI or Goldleaf on the console (or Ryubing/Ryujinx, once firmware + keys are
  set up there) to install the `.nsp`.

## Build
```sh
./tools/nsp/build-nsp.sh [path-to-prod.keys]   # default: Ryubing's prod.keys under ~/.var/app/io.github.ryubing.Ryujinx/...
```
Runs entirely in the `devkitpro/devkita64` docker image (no host sudo/packages):
builds `client/worms4nx_nsp.elf` (separate `BUILD=build_nsp`/`TARGET=worms4nx_nsp`,
the normal `.nro` build is untouched), converts it to NSO, builds `main.npdm` from
`tools/nsp/worms4nx.json` (kept outside `client/` so `Makefile.switch`'s
autodetect doesn't switch the normal build to sysmodule mode), builds
`control.nacp` + icon from `client/icon.jpg` (already 256x256 JPEG), then clones
and compiles [hacBrewPack](https://github.com/The-4n/hacBrewPack) from source
(cached under `tools/nsp/out/`, gitignored) to pack `client/romfs` + exefs +
control into a real NCA-based NSP.

> The upstream `The-4n/hacBrewPack` repo is gone from GitHub; the script builds
> the `rlaphoenix/hacBrewPack` mirror (identical source/README).

Output: `tools/nsp/out/nsp/0100576f524d3000.nsp`.

- **Title ID**: `0100576F524D3000` (homebrew range, spells "WoRM" in hex).
- **NPDM**: application-type, `pool_partition=0` (Application memory pool, not
  Applet), full filesystem/service permissions — same shape as devkitPro's own
  homebrew/sysmodule template, not a cut-down applet profile.
- **Verify**: the script runs `hactool --intype=pfs0/nca -y` on the packed NSP
  and each NCA (PFS0 header, 3 files: Program/Control/Meta NCA; all section
  hashes report `GOOD`); output is filtered so no key material is ever printed.

## Install
Copy the `.nsp` to the SD card (e.g. `switch/`) and install with DBI or
Goldleaf, as any other NSP. It then appears on the HOME menu as "Worms4NX"
with its own icon, running as a full application (not an applet overlay).

User data/assets are unchanged: still read from/written to
`sdmc:/switch/worms4nx/` regardless of `.nro` vs installed NSP — no code
changes were needed for application mode (checked: no `appletGetAppletType`/
applet-only calls in `client/src`; `romfsInit`, the SD asset paths, and
`socketInitializeDefault` all behave the same under Application or Applet).

## Uninstall
Delete the title from the HOME menu (hold it, "Manage Software" > "Delete
Software") like any installed game. `sdmc:/switch/worms4nx/` (maps, SFX,
voices, `server.txt`) is left on the SD card untouched.

## Forwarder
Not built (out of scope for this pass): a disc-stub-NSP pointing at the
`.nro` only makes sense if you don't want to rebuild/reinstall the NSP on
every change. Since `build-nsp.sh` already rebuilds the whole NSP in one
command, a separate forwarder wouldn't save a step here — skip unless you
specifically want the title entry to stay fixed while swapping `.nro`s on SD.

## Limits
- Not tested on a real console or in Ryubing/Ryujinx: no Switch firmware is
  installed in the local Ryujinx profile (`EnsureFirmwareIsValid` fails before
  the title even loads), and firmware isn't something to fetch here — this
  needs real hardware or a firmware dump the user supplies.
- `--nologo` is used (no `NintendoLogo.png`/`StartupMovie.gif`): boot shows a
  black screen briefly before the game instead of the Nintendo logo/animation.
