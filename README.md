# PSXS5 — PlayStation X Super 5

A PlayStation 1 emulator for jailbroken PS5 consoles, as a home-screen app.

- **Core:** [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) (GPLv2+), linked statically through libretro. Its built-in HLE BIOS means games boot without a BIOS dump; a real one is used when present.
- **Frontend:** PSXS5's own C11 code on SDL2 — game library, in-game menu, save states, disc swapping, cheats and settings.
- **PS5 foundation:** [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) (GPL-3.0+) for the FSELF build, `libc.prx` runtime, Lapy sandbox elevation and FTP deploy.

Status: **pre-alpha, not yet built or tested on hardware.**

## Requirements on the PS5

- etaHEN with the ELF loader on port 9021, ShadowMountPlus, and FTP on port 2121.
- The boilerplate's runtime is hardware-verified on firmware 6.02 and 12.70; others are untested.

## Data layout on the console

```text
/data/homebrew/PPSA05001/     the app (eboot.bin, sce_sys, sce_module, lapy.elf)
/data/PSXS5/
├── games/<Game Name>/        .cue+.bin, .chd, .pbp, .iso or .m3u — one folder per game
├── bios/                     optional scph*.bin
├── cheats/                   .cht files (libretro format)
├── saves/                    memory cards, one per game serial
├── states/                   save states, 10 slots per game
├── logs/psxs5.log
└── psxs5.ini                 settings
```

USB drives are also scanned: put games in a `PSXS5` folder at the drive root.

## Getting your games over

`tools/psxs5_sync.py` runs on Windows with Python 3 and 7-Zip:

```bash
python tools/psxs5_sync.py plan                      # see what it will do (read-only)
python tools/psxs5_sync.py prepare                   # E:\ISO\PSX -> E:\PSXS5_ready
python tools/psxs5_sync.py upload --host 192.168.1.50
python tools/psxs5_sync.py cheats --host 192.168.1.50
python tools/psxs5_sync.py bios scph5501.bin --host 192.168.1.50
```

`prepare` uses already-extracted discs where they exist and extracts archives only when needed. It generates missing `.cue` sheets and writes `.m3u` playlists for multi-disc games. Duplicates are skipped, and files are hardlinked on the same drive so nothing is copied twice. `upload` skips files already on the console, so it can be re-run.

## Library and covers

The library is a coverflow shelf: the selected game faces you with a glow, its neighbours turn away in perspective over a reflective floor, and moving along it plays a soft jewel-case click (synthesised, toggle in Settings).

Covers are matched by the **serial read from each disc** (`SYSTEM.CNF`), so every region gets its own art. Lookup order:

1. `cover.png` / `cover.jpg` you put in a game's folder
2. `/data/PSXS5/covers/default/<serial>.jpg` (or `covers/3d/<serial>.png` for the 3D-box style)
3. downloaded on the console from [xlenore/psx-covers](https://github.com/xlenore/psx-covers) when missing (Settings → Download missing covers)
4. `fallback-cover.png` collected by the sync tool from your folders
5. a generated title card

From the PC: `python tools/psxs5_sync.py covers --host <ip>` fetches covers for your games; add `--all` (optionally `--regions usa,europe`) for the whole database, and `--style 3d` or `both` for the 3D boxes (those are 226 px, so flat art looks sharper on a TV).

## Video

| Setting | Options |
| --- | --- |
| Internal resolution | Native, 2x (sharper 3D from the enhanced renderer) |
| Upscale | Off, 2x, 3x, 4x |
| Upscale filter | Sharp pixels, Smooth pixels (Scale2x/3x edge smoothing) |
| Aspect ratio | Auto (game), 4:3, 16:9, 16:10, 1:1 pixels, Stretch to screen |
| Integer scaling | Off / On |
| Smooth final scaling | Off / On (bilinear) |

For real widescreen, enable a game's widescreen code in Cheats and set 16:9.

## Controls

| DualSense | Library | In game |
| --- | --- | --- |
| Left / Right | Browse the shelf | D-pad |
| Cross | Play | Cross |
| Circle | Close details | Circle |
| Triangle | Game details | Triangle |
| Square | Settings (incl. Rescan library) | Square |
| L1 / R1 | Jump 8 games | L1 / R1 |
| Touchpad click (or L3+R3) | — | PSXS5 menu |

The in-game menu has resume, save and load state (10 slots), disc swapping, cheats, reset, settings, and quit to library.

## Cheats

PSXS5 reads RetroArch `.cht` files (GameShark / Action Replay codes). The `cheats` command of the sync tool installs libretro-database's PlayStation set (about 2,000 files). PSXS5 picks the best match per game by name and region. A `.cht` placed in a game's folder overrides it. Your choices are remembered per game.

## Building

Builds run on Linux or WSL (Ubuntu).

```bash
sudo apt install curl git make ninja-build ccache pkg-config python3 python3-venv \
  tar wget unzip clang-18 lld-18 llvm-18 clang-format-18 clang-tidy-18 \
  libsdl2-dev libcurl4-openssl-dev zlib1g-dev
git submodule update --init --recursive
make                                  # -> dist/PPSA05001/ and dist/PPSA05001.zip
make deploy PS5_HOST=192.168.1.50     # upload the app over FTP
make run-desktop                      # PC test build; PSXS5_ROOT=/mnt/e/PSXS5_ready/.. etc.
```

`make` first builds the core (`tools/build-core.sh ps5`), then the boilerplate pipeline compiles `src/`, links the core and the PacBrew SDL2 port, and signs `eboot.bin`.

## Layout

```text
src/main.c               screens and main loop
src/core/host.c          libretro host for the static core
src/library.c            game scanning, multi-disc grouping
src/cheats.c             .cht parsing and matching
src/platform/plat_sdl.c  video, input, audio, notifications
src/platform/elevation/  boilerplate Lapy client (PS5 only)
tools/psxs5_sync.py      PC-side library preparation and upload
tools/build-core.sh      builds PCSX-ReARMed as libpcsx_rearmed.a
```

## License

GPL-3.0-or-later. PCSX-ReARMed is GPL-2.0-or-later and LGPL-2.1-or-later. See `THIRD_PARTY_NOTICES.md`. No BIOS, games or keys are included; use dumps of discs you own.
