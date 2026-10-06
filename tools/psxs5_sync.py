#!/usr/bin/env python3
# PSXS5 - prepares a PlayStation library on a PC and uploads it to the PS5.
# SPDX-License-Identifier: GPL-3.0-or-later
"""
psxs5_sync.py - get your PS1 games onto PSXS5.

  python tools/psxs5_sync.py plan    --source E:\\ISO\\PSX
  python tools/psxs5_sync.py prepare --source E:\\ISO\\PSX --staging E:\\PSXS5_ready
  python tools/psxs5_sync.py upload  --staging E:\\PSXS5_ready --host 192.168.1.50
  python tools/psxs5_sync.py cheats  --host 192.168.1.50      (libretro .cht library)
  python tools/psxs5_sync.py bios    scph5501.bin --host 192.168.1.50

prepare builds one clean folder per game:
  * already-extracted .cue/.bin, .chd, .pbp are used as-is (hardlinked, no copy)
  * archives (.7z/.rar/.zip) are only extracted when no extracted copy exists
  * missing .cue sheets are generated, multi-disc games get an .m3u
  * duplicates (the same game as a folder and as a loose archive) are skipped
  * the largest image beside the game becomes fallback-cover.png (used only
    when the cover database has nothing for that serial)
upload mirrors the staging folder to /data/PSXS5/games over FTP and skips
files that are already there with the same size, so it can be re-run.
"""
from __future__ import annotations

import argparse
import ftplib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
from psx_disc import disc_serial  # noqa: E402

DISC_EXTS = {".cue", ".bin", ".chd", ".pbp", ".iso", ".img", ".m3u", ".ccd", ".sub", ".mdf", ".mds"}
LOADABLE = {".cue", ".chd", ".pbp", ".iso", ".m3u", ".ccd", ".mds"}
ARCHIVE_EXTS = {".7z", ".rar", ".zip"}
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".webp"}
REMOTE_ROOT = "/data/PSXS5"
SEVEN_ZIP_CANDIDATES = [
    r"C:\Program Files\7-Zip\7z.exe",
    r"C:\Program Files (x86)\7-Zip\7z.exe",
    "7z",
    "7zz",
]
CHEATS_REPO = "https://github.com/libretro/libretro-database.git"
CHEATS_DIR = "cht/Sony - PlayStation"
COVERS_REPO = "https://github.com/xlenore/psx-covers.git"
COVER_URL = "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/{style}/{serial}.{ext}"
COVER_EXT = {"default": "jpg", "3d": "png"}
REGION_PREFIXES = {
    "usa": ["SLUS", "SCUS"],
    "europe": ["SLES", "SCES", "SCED"],
    "japan": ["SLPS", "SCPS", "SLPM", "SIPS", "PAPX", "SCPM"],
}


# --------------------------------------------------------------------- names

def normalize(name: str) -> str:
    """'Final Fantasy Tactics [NTSC-U] [SCUS-94221]' -> 'finalfantasytactics'."""
    name = re.sub(r"\.[A-Za-z0-9]{1,4}$", "", name)
    name = re.sub(r"\[[^\]]*\]|\([^)]*\)", "", name)
    name = re.sub(r"^disney-?pixar'?s\s+", "", name, flags=re.I)
    romans = {"ii": "2", "iii": "3", "iv": "4", "v": "5", "vi": "6", "vii": "7", "viii": "8", "ix": "9", "x": "10"}
    name = re.sub(r"\b(i{2,3}|iv|vi{0,3}|ix|x)\b", lambda m: romans.get(m.group(1).lower(), m.group(1)),
                  name, flags=re.I)
    key = re.sub(r"[^a-z0-9]", "", name.lower())
    if key.startswith("the") and len(key) > 6:
        key = key[3:]
    if key.endswith("the"):
        key = key[:-3]
    return key


def disc_number(name: str) -> int:
    m = re.search(r"\((?:Disc|CD)\s*(\d+)\)", name, re.I)
    return int(m.group(1)) if m else 0


def game_title(name: str) -> str:
    """Folder name for the PS5: 'Chrono Cross (USA) (Disc 1).cue' -> 'Chrono Cross (USA)'."""
    name = re.sub(r"\.[A-Za-z0-9]{1,4}$", "", name)
    # "RE2 (USA) (Disc 1) (Leon)" and "(Disc 2) (Claire)" are one game: cut at the disc tag
    name = re.split(r"\s*\((?:Disc|CD)\s*\d+\)", name, flags=re.I)[0]
    name = re.sub(r"\s*\(Track\s*\d+\)", "", name, flags=re.I)
    name = re.sub(r"\s*\[[^\]]*\]", "", name)
    name = name.replace("_", " ")
    name = re.sub(r"\s+", " ", name).strip(" .-")
    return re.sub(r'[<>:"/\\|?*]', "", name) or "Game"


# --------------------------------------------------------------------- discovery

@dataclass
class Source:
    """One game found in the source library."""
    title: str
    key: str
    kind: str                      # "folder" or "archive"
    path: Path
    loadables: list[Path] = field(default_factory=list)   # cue/chd/pbp...
    loose_bins: list[Path] = field(default_factory=list)  # bins without a cue
    archives: list[Path] = field(default_factory=list)    # still to extract
    cover: Path | None = None
    duplicate_of: str | None = None


def cue_files(cue: Path) -> list[str]:
    try:
        text = cue.read_text(errors="replace")
    except OSError:
        return []
    return re.findall(r'FILE\s+"([^"]+)"', text, re.I)


def scan_folder(folder: Path) -> Source:
    files = [p for p in folder.rglob("*") if p.is_file()]
    loadables = sorted(p for p in files if p.suffix.lower() in LOADABLE)
    covered = set()
    complete = []
    for p in loadables:
        if p.suffix.lower() == ".cue":
            refs = cue_files(p)
            if refs and all((p.parent / r).exists() for r in refs):
                complete.append(p)
                covered.update((p.parent / r).resolve() for r in refs)
        else:
            complete.append(p)
    # A PSP-style EBOOT.PBP next to real disc images is just another copy.
    if any(p.suffix.lower() in {".cue", ".chd", ".m3u"} for p in complete):
        complete = [p for p in complete if p.suffix.lower() not in {".pbp", ".iso"}]
    bins = sorted(p for p in files if p.suffix.lower() == ".bin" and p.resolve() not in covered)
    archives = sorted(p for p in files if p.suffix.lower() in ARCHIVE_EXTS)

    # Archives only matter for discs that are not already extracted.
    have = {normalize(p.name) + str(disc_number(p.name)) for p in complete}
    have |= {normalize(p.name) + str(disc_number(p.name)) for p in bins}
    needed = [a for a in archives if normalize(a.name) + str(disc_number(a.name)) not in have]
    if complete or bins:
        # e.g. "Dino Crisis (v1.0).7z" next to an extracted v1.1: keep the extracted one
        needed = [a for a in needed if disc_number(a.name) > 0]

    covers = sorted((p for p in files if p.suffix.lower() in IMAGE_EXTS), key=lambda p: p.stat().st_size, reverse=True)
    names = [p.name for p in complete] or [p.name for p in bins] or [a.name for a in needed] or [folder.name]
    title = game_title(names[0])
    if title.upper() == "EBOOT":
        title = game_title(folder.name)
    return Source(title, normalize(title), "folder", folder, complete, bins, needed,
                  covers[0] if covers else None)


def discover(source: Path) -> list[Source]:
    games: list[Source] = []
    for entry in sorted(source.iterdir(), key=lambda p: p.name.lower()):
        if entry.is_dir():
            g = scan_folder(entry)
            if g.loadables or g.loose_bins or g.archives:
                games.append(g)
        elif entry.suffix.lower() in ARCHIVE_EXTS:
            title = game_title(entry.name)
            games.append(Source(title, normalize(title), "archive", entry, archives=[entry]))
        elif entry.suffix.lower() in LOADABLE:
            title = game_title(entry.name)
            games.append(Source(title, normalize(title), "folder", entry.parent, loadables=[entry]))

    # Folders win over loose archives of the same game.
    seen: dict[str, Source] = {}
    for g in sorted(games, key=lambda g: g.kind != "folder"):
        if g.key in seen:
            g.duplicate_of = seen[g.key].title
        else:
            seen[g.key] = g
    return games


# --------------------------------------------------------------------- prepare

def seven_zip() -> str:
    for c in SEVEN_ZIP_CANDIDATES:
        if Path(c).exists() or shutil.which(c):
            return c
    sys.exit("7-Zip not found. Install it from https://www.7-zip.org/")


def place(src: Path, dst: Path) -> None:
    """Hardlink when possible (same drive, no extra space), else copy."""
    if dst.exists() and dst.stat().st_size == src.stat().st_size:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def write_cue_for_bins(bins: list[Path], out_dir: Path) -> list[Path]:
    """Creates cue sheets for bins that arrived without one.
    'Game (Track 1).bin' .. 'Game (Track N).bin' become one multi-track cue."""
    groups: dict[str, list[Path]] = {}
    for b in bins:
        base = re.sub(r"\s*\(Track\s*\d+\)", "", b.stem, flags=re.I)
        groups.setdefault(base, []).append(b)
    cues = []
    for base, members in groups.items():
        members.sort(key=lambda p: int((re.search(r"Track\s*(\d+)", p.stem, re.I) or [0, 0])[1]))
        lines = []
        for i, b in enumerate(members, 1):
            lines.append(f'FILE "{b.name}" BINARY')
            if i == 1:
                lines += [f"  TRACK {i:02d} MODE2/2352", "    INDEX 01 00:00:00"]
            else:
                lines += [f"  TRACK {i:02d} AUDIO", "    INDEX 00 00:00:00", "    INDEX 01 00:02:00"]
        cue = out_dir / f"{base}.cue"
        cue.write_text("\n".join(lines) + "\n")
        cues.append(cue)
    return cues


def extract(archive: Path, out_dir: Path) -> None:
    tmp = Path(tempfile.mkdtemp(prefix="psxs5_", dir=out_dir.parent))
    try:
        print(f"    extracting {archive.name} ...", flush=True)
        r = subprocess.run([seven_zip(), "x", "-y", f"-o{tmp}", str(archive)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            print(f"    ! 7-Zip failed on {archive.name}: {r.stderr.strip()[:200]}")
            return
        for f in tmp.rglob("*"):
            if f.is_file() and f.suffix.lower() in DISC_EXTS:
                dst = out_dir / f.name
                if not dst.exists():
                    shutil.move(str(f), dst)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def save_cover(img: Path, out_dir: Path) -> None:
    try:
        from PIL import Image
        with Image.open(img) as im:
            im = im.convert("RGB")
            im.thumbnail((512, 512))
            im.save(out_dir / "fallback-cover.png")
    except Exception:  # Pillow missing or unreadable image: covers are optional
        if img.suffix.lower() in {".png", ".jpg", ".jpeg"}:
            shutil.copy2(img, out_dir / f"fallback-cover{img.suffix.lower()}")


def write_m3u(out_dir: Path, title: str) -> None:
    discs = sorted((p for p in out_dir.iterdir() if p.suffix.lower() in {".cue", ".chd", ".pbp"}
                    and disc_number(p.name) > 0), key=lambda p: disc_number(p.name))
    if len(discs) > 1:
        (out_dir / f"{title}.m3u").write_text("\n".join(p.name for p in discs) + "\n")


def write_serial(out_dir: Path) -> str | None:
    """serial.txt lets PSXS5 match covers/saves even for .chd, which it can't read."""
    for pattern in ("*.m3u", "*.cue", "*.pbp", "*.iso", "*.bin"):
        for p in sorted(out_dir.glob(pattern)):
            serial = disc_serial(p)
            if serial:
                (out_dir / "serial.txt").write_text(serial + "\n")
                return serial
    return None


def prepare(games: list[Source], staging: Path, only: str | None) -> None:
    staging.mkdir(parents=True, exist_ok=True)
    for g in games:
        if g.duplicate_of or (only and only.lower() not in g.title.lower()):
            continue
        out = staging / g.title
        out.mkdir(parents=True, exist_ok=True)
        print(f"  {g.title}")
        for cue in g.loadables:
            place(cue, out / cue.name)
            if cue.suffix.lower() == ".cue":
                for ref in cue_files(cue):
                    place(cue.parent / ref, out / Path(ref).name)
        for b in g.loose_bins:
            place(b, out / b.name)
        if g.loose_bins:
            write_cue_for_bins([out / b.name for b in g.loose_bins], out)
        for a in g.archives:
            extract(a, out)
        # archives may also bring bins without sheets
        sheets = {r.lower() for c in out.glob("*.cue") for r in cue_files(c)}
        orphans = [b for b in out.glob("*.bin") if b.name.lower() not in sheets]
        if orphans:
            write_cue_for_bins(orphans, out)
        write_m3u(out, g.title)
        write_serial(out)
        if g.cover and not any(out.glob("fallback-cover.*")):
            save_cover(g.cover, out)


def print_plan(games: list[Source]) -> None:
    total = dups = 0
    for g in games:
        if g.duplicate_of:
            dups += 1
            print(f"  skip  {g.path.name}  (same game as '{g.duplicate_of}')")
            continue
        total += 1
        what = []
        if g.loadables:
            what.append(f"{len(g.loadables)} ready")
        if g.loose_bins:
            what.append(f"{len(g.loose_bins)} bin(s) need a cue")
        if g.archives:
            what.append("extract " + ", ".join(a.name for a in g.archives))
        print(f"  game  {g.title:<55} {'; '.join(what)}")
    print(f"\n{total} games, {dups} duplicates skipped")


# --------------------------------------------------------------------- FTP

def ftp_connect(host: str, port: int) -> ftplib.FTP:
    ftp = ftplib.FTP()
    ftp.connect(host, port, timeout=30)
    ftp.login()
    ftp.voidcmd("TYPE I")
    return ftp


def ftp_mkdirs(ftp: ftplib.FTP, path: str) -> None:
    cur = ""
    for part in path.strip("/").split("/"):
        cur += "/" + part
        try:
            ftp.mkd(cur)
        except ftplib.error_perm:
            pass


def ftp_size(ftp: ftplib.FTP, path: str) -> int:
    try:
        return int(ftp.size(path) or -1)
    except (ftplib.error_perm, ftplib.error_reply, ValueError):
        return -1


def upload_tree(local: Path, remote: str, host: str, port: int) -> None:
    ftp = ftp_connect(host, port)
    files = sorted(p for p in local.rglob("*") if p.is_file())
    total = sum(p.stat().st_size for p in files)
    done = 0
    made: set[str] = set()
    for p in files:
        rel = p.relative_to(local).as_posix()
        dst = f"{remote}/{rel}"
        rdir = dst.rsplit("/", 1)[0]
        size = p.stat().st_size
        if rdir not in made:
            ftp_mkdirs(ftp, rdir)
            made.add(rdir)
        if ftp_size(ftp, dst) == size:
            done += size
            continue
        print(f"  [{done * 100 // max(total, 1):3d}%] {rel} ({size >> 20} MB)", flush=True)
        with p.open("rb") as fh:
            ftp.storbinary(f"STOR {dst}", fh, blocksize=1 << 20)
        done += size
    ftp.quit()
    print(f"  [100%] {len(files)} files in {remote}")


# --------------------------------------------------------------------- cheats

def library_serials(staging: Path) -> list[str]:
    serials = []
    for game in sorted(p for p in staging.iterdir() if p.is_dir()):
        txt = game / "serial.txt"
        serial = txt.read_text().strip() if txt.exists() else write_serial(game)
        if serial:
            serials.append(serial)
        else:
            print(f"  ? no serial for {game.name} (add cover.png to its folder)")
    return serials


def fetch_my_covers(serials: list[str], dest: Path, styles: list[str]) -> None:
    got = missing = 0
    for style in styles:
        folder = dest / style
        folder.mkdir(parents=True, exist_ok=True)
        for serial in serials:
            target = folder / f"{serial}.{COVER_EXT[style]}"
            if target.exists():
                got += 1
                continue
            url = COVER_URL.format(style=style, serial=serial, ext=COVER_EXT[style])
            try:
                with urllib.request.urlopen(url, timeout=20) as r:
                    target.write_bytes(r.read())
                got += 1
            except urllib.error.HTTPError as e:
                missing += 1
                print(f"  - no {style} cover for {serial} ({e.code})")
    print(f"  {got} covers ready, {missing} not in the database, in {dest}")


def fetch_all_covers(dest: Path, styles: list[str], regions: list[str]) -> Path:
    """Sparse-clones the cover database, limited to the chosen styles and regions."""
    repo = dest / "psx-covers"
    if not repo.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--filter=blob:none", "--no-checkout",
                        COVERS_REPO, str(repo)], check=True)
    prefixes = [p for r in regions for p in REGION_PREFIXES[r]]
    patterns = [f"/covers/{s}/{p}-*" for s in styles for p in prefixes]
    subprocess.run(["git", "-C", str(repo), "sparse-checkout", "init", "--no-cone"], check=True)
    subprocess.run(["git", "-C", str(repo), "sparse-checkout", "set", "--no-cone", *patterns], check=True)
    subprocess.run(["git", "-C", str(repo), "checkout"], check=True)
    for s in styles:
        n = len(list((repo / "covers" / s).glob("*")))
        print(f"  {n} {s} covers in {repo / 'covers' / s}")
    return repo / "covers"


def fetch_cheats(dest: Path) -> Path:
    """Sparse-clones only the PlayStation folder of libretro-database."""
    repo = dest / "libretro-database"
    if not repo.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--filter=blob:none", "--sparse",
                        CHEATS_REPO, str(repo)], check=True)
    subprocess.run(["git", "-C", str(repo), "sparse-checkout", "set", CHEATS_DIR], check=True)
    subprocess.run(["git", "-C", str(repo), "pull", "--ff-only"], check=False)
    folder = repo / CHEATS_DIR
    print(f"  {len(list(folder.glob('*.cht')))} PlayStation cheat files in {folder}")
    return folder


# --------------------------------------------------------------------- main

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["plan", "prepare", "upload", "covers", "cheats", "bios"])
    ap.add_argument("--all", action="store_true", help="covers: the whole database, not just your games")
    ap.add_argument("--style", choices=["default", "3d", "both"], default="default",
                    help="covers: flat front art (default), 3D boxes, or both")
    ap.add_argument("--regions", default="usa,europe,japan",
                    help="covers --all: comma-separated subset of usa,europe,japan")
    ap.add_argument("files", nargs="*", help="BIOS file(s) for the bios command")
    ap.add_argument("--source", type=Path, default=Path(r"E:\ISO\PSX"))
    ap.add_argument("--staging", type=Path, default=Path(r"E:\PSXS5_ready"))
    ap.add_argument("--host", help="PS5 IP address")
    ap.add_argument("--port", type=int, default=2121, help="etaHEN FTP port")
    ap.add_argument("--only", help="limit prepare to titles containing this text")
    args = ap.parse_args()

    if args.command in {"plan", "prepare"}:
        games = discover(args.source)
        if args.command == "plan":
            print_plan(games)
        else:
            prepare(games, args.staging, args.only)
            print(f"\nReady in {args.staging}. Next: upload --host <PS5 IP>")
        return

    if args.command == "covers":
        styles = ["default", "3d"] if args.style == "both" else [args.style]
        dest = args.staging.parent / "PSXS5_covers"
        if args.all:
            regions = [r.strip().lower() for r in args.regions.split(",") if r.strip()]
            bad = [r for r in regions if r not in REGION_PREFIXES]
            if bad:
                sys.exit(f"unknown region(s): {', '.join(bad)}")
            folder = fetch_all_covers(dest, styles, regions)
        else:
            fetch_my_covers(library_serials(args.staging), dest, styles)
            folder = dest
        if args.host:
            for s in styles:
                upload_tree(folder / s, f"{REMOTE_ROOT}/covers/{s}", args.host, args.port)
        return

    if args.command == "cheats":
        folder = fetch_cheats(args.staging.parent / "PSXS5_cheats")
        if args.host:
            upload_tree(folder, f"{REMOTE_ROOT}/cheats", args.host, args.port)
        return

    if not args.host:
        sys.exit("--host is required (your PS5's IP address)")
    if args.command == "upload":
        upload_tree(args.staging, f"{REMOTE_ROOT}/games", args.host, args.port)
    elif args.command == "bios":
        tmp = Path(tempfile.mkdtemp())
        for f in args.files:
            shutil.copy2(f, tmp / Path(f).name.lower())
        upload_tree(tmp, f"{REMOTE_ROOT}/bios", args.host, args.port)
        shutil.rmtree(tmp)


if __name__ == "__main__":
    main()
