# Worms4NX

Fan remake of Worms 4: Mayhem for Nintendo Switch homebrew (Atmosphère), with a Linux desktop build for development.
No Team17 asset is shipped: bundled sounds are CC0 (see `client/romfs/CREDITS.md`), original assets will come from an importer reading your own install.

## Layout
- `client/` C++17 + [raylib-nx](https://github.com/luizpestana/raylib-nx) game: voxel destructible terrain, deterministic lockstep sim, local and online play.
- `server/` Rust lobby + input relay server (see `server/README.md`, protocol in `PROTOCOL.md`).
- `third_party/raylib-nx/` (cloned, not committed): `git clone --depth 1 https://github.com/luizpestana/raylib-nx third_party/raylib-nx`.

## Build
raylib libs (once):
```sh
cd third_party/raylib-nx/src
make PLATFORM=PLATFORM_DESKTOP RAYLIB_RELEASE_PATH=../out/desktop && rm -f *.o
docker run --rm -u $(id -u):$(id -g) -v "$PWD/../..":/w -w /w/raylib-nx/src devkitpro/devkita64 make PLATFORM=PLATFORM_NX RAYLIB_RELEASE_PATH=../out/nx CUSTOM_CFLAGS=-DNX_DISABLE_GAMEPAD_EMULATION && rm -f *.o
```
Game:
```sh
cd client && make && ./worms4nx            # desktop; make check = determinism test
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/w -w /w/client devkitpro/devkita64 make -f Makefile.switch   # -> client/worms4nx.nro
```
Server: `cd server && cargo run --release -- 0.0.0.0:7777` (or `docker build -t worms4nx-server server`).

## Run on Switch / emulator
Copy `worms4nx.nro` to `sdmc:/switch/worms4nx/`. Online: put `<server-ip> 7777 <name>` in `sdmc:/switch/worms4nx/server.txt` (desktop: `client/server.txt`).
Ryubing: open the `.nro`, enable *Guest Internet Access* for online play.

### Installable NSP
`./tools/nsp/build-nsp.sh` packs an application NSP (HOME menu entry, own icon,
full app memory) instead of the `.nro` — see `docs/nsp.md` for prerequisites
(Atmosphère + sigpatches), install (DBI/Goldleaf) and limits.

## Controls
| | Controller | Keyboard |
|---|---|---|
| Turn / walk | left stick | arrows |
| Aim | right stick | W / S |
| Fire (hold = power) | A | Space |
| Jump | B | Enter |
| Next weapon | R / Y | Tab |

## Modding
- Weapons: `client/romfs/weapons.json`.
- Maps: `client/romfs/maps/*.json` (format in `docs/maps.md`), user maps in `sdmc:/switch/worms4nx/assets/maps/`.
- Sounds/voices: drop replacements in `sdmc:/switch/worms4nx/assets/{sfx,voices/<bank>}/`.
