<p align="center">
  <img src="docs/media/banner.png" alt="PSXS5 - PlayStation X Super 5" width="100%">
</p>

<h3 align="center">PlayStation 1 emulation, running natively on a jailbroken PS5.</h3>

<p align="center">
  <img alt="Platform: jailbroken PS5" src="https://img.shields.io/badge/platform-jailbroken%20PS5-3d55c8">
  <img alt="Based on PCSX-ReARMed" src="https://img.shields.io/badge/based%20on-PCSX--ReARMed-5a6fe0">
  <img alt="RetroAchievements" src="https://img.shields.io/badge/RetroAchievements-supported-f0b429">
  <img alt="Licence: GPL-3.0-or-later" src="https://img.shields.io/badge/licence-GPL--3.0--or--later-2b2f7a">
</p>

---

## What is PSXS5?

PSXS5 (PlayStation X Super 5) is a PlayStation 1 emulator that installs as an app on the PS5 home screen.

- The emulation is [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed), built into the app and running on the PS5's own CPU.
- You pick a game from a cover-flow shelf with your covers.
- Games are upscaled with xBR for a 4K TV.

## A passion project

PSXS5 is a hobby project, made for the love of the PS1 library. No company or schedule is behind it, and nothing is for sale. Expect rough edges. Bug reports that come with `/data/PSXS5/logs/psxs5.log` are the most helpful.

## Highlights

- **Native app.** A real home-screen title (`PPSA97510`) with its own icon and art. Nothing is streamed and no PC is needed while you play.
- **A shelf for your games.** A 3D cover flow with reflections and a soft click as you browse.
  - Covers are matched by the serial read from each disc, so every region gets its own art.
  - Missing covers download on the console.
- **Plays every common format.** `.cue`/`.bin`, `.chd`, `.pbp` (including multi-disc), `.iso`, `.img`, `.mdf`, `.ccd` and `.m3u` playlists for multi-disc games.
- **Sharp on a 4K TV.**
  - Internal resolution: native or 2x (sharper 3D).
  - Upscale: 1x to 4x, with three filters:
    - Sharp pixels
    - Smooth pixels (Scale2x/3x)
    - xBR, the smoothest edges for 2D art
  - Aspect ratio: auto, 4:3, 16:9, 16:10, 1:1 pixels or stretch.
  - Integer scaling, bilinear smoothing and dithering on or off.
- **Built for full speed.** Aims at 60 fps (50 for PAL games). Scaling is spread over the PS5's CPU cores, and the video goes straight to the PS5's display output.
- **RetroAchievements.** Earn [RetroAchievements](https://retroachievements.org) as you play.
  - Unlocks and leaderboard results pop up on screen.
  - Optional hardcore mode.
  - See [RetroAchievements](#retroachievements).
- **Cheats for every game.** PSXS5 reads RetroArch `.cht` files (GameShark / Action Replay codes).
  - The PC tool installs about 2,000 of them from libretro-database and matches them to your games.
  - Toggle them per game from the in-game menu.
- **Save states.** 10 slots per game, plus memory cards saved per game.
- **DualSense ready.**
  - DualShock analog with rumble.
  - On digital-only games, the left stick drives the D-pad.
  - Cross confirms and Circle goes back in the menus.
- **Real BIOS or none.** Games boot with PCSX-ReARMed's built-in HLE BIOS. If you put your own BIOS dump in `bios/`, PSXS5 uses it.
- **Quiet interface sounds.** Five styles (Soft, Wood, Pop, Chime, Classic), a volume setting, or off.
- **A PC tool for your library.** `tools/psxs5_sync.py` prepares your games on Windows and uploads them over FTP:
  - unpacks archives
  - writes missing `.cue` sheets and `.m3u` playlists
  - fetches covers and cheats
- **Logs that help.** A session log and a crash report (with addresses for the symbol map) stay on the console.

## What you need

- **A jailbroken PS5** with [etaHEN](https://github.com/etaHEN/etaHEN) and kstuff loaded. Development happens on firmware 13.60.
- **[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)**, loaded at every boot. It puts PSXS5 on the home screen from `/data/homebrew/PPSA97510/`.
- **An FTP client**, such as etaHEN's FTP server on port 2121 with FileZilla, or the included PC tool.
- **Your own games**, dumped from discs you own.
- **Optional: your own PS1 BIOS**, dumped from your own console. Without one, the built-in HLE BIOS is used and most games run fine.

> **No BIOS files, games, keys or cheats with copyrighted content are included with PSXS5, and none will be provided.** You have to supply your own legally made backups.

## Getting started

1. **Download** `PSXS5-v1.0.0.zip` from the [latest release](../../releases/latest) and extract it. Inside is a `PPSA97510` folder and the PC tools.
2. **Install the app.** Copy the `PPSA97510` folder to `/data/homebrew/` on the PS5 over FTP. Then set its permissions to `777`: in FileZilla, right-click the folder → *File permissions* → `777`, recurse into subdirectories. Without this, the PS5 says *"Can't start the game or app"* (CE-107750-0). The PC tool does all of this for you:
   ```bash
   python tools/psxs5_sync.py app --app-dir PPSA97510 --host <PS5 IP>
   ```
3. **Restart ShadowMountPlus** (or the console). PSXS5 appears on the home screen.
4. **Add your games** to `/data/PSXS5/games/`, one folder per game:
   ```text
   /data/PSXS5/games/Final Fantasy VII/   FF7 Disc 1.cue, FF7 Disc 1.bin, ... (any format above)
   ```
   Or let the PC tool prepare and upload a whole folder of games (see [PC tool](#pc-tool)).
5. **Optional:** put your BIOS dump (for example `scph5501.bin`) in `/data/PSXS5/bios/`.
6. **Start PSXS5.** Covers download the first time, then the shelf opens.

### Folders on the console

```text
/data/homebrew/PPSA97510/     the app
/data/PSXS5/
├── games/<Game name>/        your games, one folder per game (a cheats.cht here overrides the library)
├── bios/                     optional: your BIOS dump (scph*.bin)
├── covers/                   downloaded covers, by serial
├── cheats/                   the .cht library
├── saves/                    memory cards, one per game
├── states/                   save states, 10 slots per game
├── logs/psxs5.log            the session log (send this with bug reports)
├── retroachievements.ini     RetroAchievements sign-in (token only)
└── psxs5.ini                 settings
```

## Controls

**On the shelf**

| Button | Does |
|---|---|
| D-pad or left stick | Browse the games |
| L1 / R1 | Jump a page |
| Cross | Play |
| Triangle | Game details |
| Square | Settings |
| Circle | Back |

**In a game**

| Button | Does |
|---|---|
| OPTIONS | PS1 START |
| Touchpad tap | PS1 SELECT |
| Hold the touchpad (0.7 s) or L3 + R3 | The PSXS5 menu |
| Left stick | Analog stick; on digital-only games it also drives the D-pad (*Left stick as D-pad* in Settings) |

The **PSXS5 menu** has:
- resume
- save and load state (slots 0–9)
- disc change, for multi-disc games
- cheats
- reset
- settings
- quit to the shelf

## Settings

| Section | Settings |
|---|---|
| Video | Internal resolution (native, 2x), upscale (off, 2x, 3x, 4x), upscale filter (sharp, Scale2x, xBR), aspect ratio, integer scaling, smooth final scaling, dithering, FPS counter |
| System | Region (auto, NTSC, PAL), BIOS (real if present, or built-in HLE), controller (digital or DualShock), left stick as D-pad, fast CD loading, unlocking `/data` with etaHEN |
| RetroAchievements | Account, hardcore mode |
| Library | Cover style (flat or 3D box), download missing covers, interface sound and volume, rescan library |

Video settings apply while you play. Region, BIOS and controller apply from the next game.

## RetroAchievements

PSXS5 never asks for your password on the console. You sign in once from your PC: the tool swaps your password for a login token, and only the token is copied to the PS5.

```bash
python tools/psxs5_sync.py ra-login --host <PS5 IP>
```

Restart PSXS5 and it signs in by itself. Start a game, and if it has an achievement set, a banner shows how many you have unlocked.

- **Hardcore mode** (Settings → RetroAchievements) turns off loading states and cheats, as RetroAchievements requires. Turning it on restarts the running game.
- Without a connection, unlocks are kept and sent when the console is back online.
- Games are recognised by the same disc hash RetroArch uses, so the usual RetroAchievements-compatible dumps (Redump) work.

## Cheats

The PC tool downloads libretro-database's PlayStation cheats and installs the right file for each of your games:

```bash
python tools/psxs5_sync.py cheats --host <PS5 IP>
```

In a game, open the PSXS5 menu → **Cheats** and switch codes on and off. Your choice is remembered per game. To use your own codes, put a `cheats.cht` in the game's folder.

## PC tool

`tools/psxs5_sync.py` runs on Windows with Python 3 (and [7-Zip](https://www.7-zip.org/) for archives).

```bash
python tools/psxs5_sync.py plan    --source E:\ISO\PSX                 # what it would do, read-only
python tools/psxs5_sync.py sync    --source E:\ISO\PSX --host <PS5 IP>  # prepare + upload, one game at a time
python tools/psxs5_sync.py covers  --host <PS5 IP>                     # covers for your games
python tools/psxs5_sync.py cheats  --host <PS5 IP>                     # the cheat library
python tools/psxs5_sync.py bios    scph5501.bin --host <PS5 IP>        # your own BIOS dump
python tools/psxs5_sync.py ra-login --host <PS5 IP>                    # RetroAchievements
python tools/psxs5_sync.py app     --app-dir PPSA97510 --host <PS5 IP> # install or update the app
```

`sync` handles the whole library, one game at a time:
- unpacks `.7z` / `.rar` / `.zip` archives, including archives inside archives and split archives
- writes missing `.cue` sheets and `.m3u` playlists for multi-disc games
- skips duplicates
- uploads to `/data/PSXS5/games/`
- sets the permissions

It remembers what is already on the console, so you can run it again whenever you add games.

## Known limitations

- **Rendering is on the CPU.** It is fast enough for full speed, but internal resolution stops at 2x and HD texture packs aren't supported.
- **Sandboxed mode.** If etaHEN won't unlock `/data`, PSXS5 still runs: it reads the game list the PC tool uploads (`library.txt`) instead of listing the folder.
- **Closing from the PS button** crashed the console once during testing while `/data` was unlocked through etaHEN. If it happens to you, set *Settings → System → Unlock /data with etaHEN* to Off and let us know.
- The PS5's own keyboard and on-screen keyboard aren't used, so text entry (like the RetroAchievements sign-in) happens on the PC.

## Building from source

The PS5 app is built by GitHub Actions on every push (*Actions* → latest run → `PSXS5-PPSA97510` artifact). To build it yourself on Linux or WSL (Ubuntu 24.04):

```bash
sudo apt install clang-18 lld-18 llvm-18 make ninja-build ccache pkg-config python3 python3-venv tar unzip wget
git clone --recursive https://github.com/SnivyX/PSXS5.git
cd PSXS5
make            # -> dist/PPSA97510/
make desktop    # optional PC test build (needs libsdl2-dev libcurl4-openssl-dev zlib1g-dev)
```

`make` builds PCSX-ReARMed (`tools/build-core.sh`) and rcheevos (`tools/build-rcheevos.sh`) as static libraries, compiles `src/` and signs `eboot.bin`.

| Path | What |
|---|---|
| `src/main.c` | screens and main loop |
| `src/core/host.c` | libretro host for the built-in core |
| `src/ra/` | RetroAchievements |
| `src/ui/` | cover flow, text, sounds |
| `src/platform/` | PS5 video output, input, heap, scaling filters |
| `tools/psxs5_sync.py` | the PC tool |
| `tools/make_art.py` | generates the icon, home-screen art and this page's banner |
| `docs/boilerplate/` | documentation of the PS5 app boilerplate PSXS5 is built on |

## Credits

PSXS5 stands on the work of many people. Thank you all.

**Emulation and libraries**
- [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed): notaz, the PCSX / PCSX-Reloaded teams and the libretro contributors. The emulator core.
- [rcheevos](https://github.com/RetroAchievements/rcheevos) and [RetroAchievements.org](https://retroachievements.org): the RetroAchievements team, and the community that builds the achievement sets.
- [libretro](https://www.libretro.com/) / [libretro-common](https://github.com/libretro/libretro-common): the core API and helpers.
- [libretro-database](https://github.com/libretro/libretro-database): the PlayStation cheat library, gathered by its contributors.
- [SDL2](https://www.libsdl.org/): Sam Lantinga and the SDL contributors.
- [curl / libcurl](https://curl.se/): Daniel Stenberg and contributors.
- [stb](https://github.com/nothings/stb): Sean Barrett (image decoding and font rendering).
- [Inter](https://github.com/rsms/inter): Rasmus Andersson (the interface font).
- [dlmalloc](https://gee.cs.oswego.edu/dl/html/malloc.html): Doug Lea.
- **xBR**: Hyllian, for the upscaling algorithm. **Scale2x / Scale3x**: Andrea Mazzoleni (AdvanceMAME).
- [xlenore/psx-covers](https://github.com/xlenore/psx-covers): xlenore, for the cover collection.

**The PS5 scene**
- [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate): BlackBearReloaded. The FSELF build pipeline, runtime and deploy tooling PSXS5 is built on.
- [ps5-payload-dev SDK and PacBrew](https://github.com/ps5-payload-dev): John Törnblom and contributors. The SDK, toolchain support and the SDL2 / libcurl ports.
- [SharpProspero](https://github.com/SvenGDK/SharpProspero): SvenGDK. The ELF converter and FSELF writer.
- [etaHEN](https://github.com/etaHEN/etaHEN): the etaHEN team. The HEN, FTP server, ELF loader and on-demand jailbreak PSXS5 uses.
- **kstuff**: its authors and maintainers.
- [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus): drakmor. It puts homebrew titles on the home screen.
- [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon): ArkSama / Team PHU, with mpereiraesaa's fork. The fallback sandbox elevation.
- [PS5SX2](https://github.com/Swordpdf/PS5SX2): Swordpdf. The PS2 emulator whose shelf and project page inspired PSXS5's own. Studying it and the other PS5 emulator ports also helped work out how a native emulator title runs on the PS5.

**Tools**
- [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex) (`texconv`) for the home-screen art, [Pillow](https://python-pillow.org/) for generating it, and [7-Zip](https://www.7-zip.org/) for unpacking libraries.
- Developed with the help of [Claude Code](https://claude.com/claude-code).

Full licence details are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

PSXS5 is free software under the GPL-3.0-or-later. PCSX-ReARMed is GPL-2.0-or-later, with parts under LGPL-2.1-or-later.

PSXS5 is not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation" is a registered trademark of Sony Interactive Entertainment Inc. It is used here only to describe what the emulator does.

PSXS5 doesn't include and won't provide BIOS files, games or keys. Only use dumps of discs and consoles you own.
