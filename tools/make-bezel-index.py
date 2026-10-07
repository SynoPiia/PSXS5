#!/usr/bin/env python3
"""PSXS5 - writes assets/bezels-index.txt: the names of The Bezel Project's
PlayStation bezels (retroarch/overlay/GameBezels/PSX). PSXS5 matches a game
against this list, as it does for cheats, and downloads just that picture.

    python tools/make-bezel-index.py        (rerun now and then for new bezels)

SPDX-License-Identifier: GPL-3.0-or-later
"""
import json
import pathlib
import urllib.parse
import urllib.request

REPO = "thebezelproject/bezelproject-PSX"
FOLDER = "retroarch/overlay/GameBezels/PSX/"


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "PSXS5", "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def main():
    branch = get(f"https://api.github.com/repos/{REPO}")["default_branch"]
    # the folder alone (the whole repository's listing is too long to come back whole)
    folder = urllib.parse.quote(f"{branch}:{FOLDER.rstrip('/')}", safe="")
    tree = get(f"https://api.github.com/repos/{REPO}/git/trees/{folder}")
    if tree.get("truncated"):
        raise SystemExit("the folder listing was truncated")
    names = sorted(e["path"] for e in tree["tree"] if e["type"] == "blob" and e["path"].endswith(".png"))
    out = pathlib.Path(__file__).resolve().parent.parent / "assets" / "bezels-index.txt"
    out.write_text(f"# {REPO} {branch}: {FOLDER} ({len(names)} files)\n" + "\n".join(names) + "\n",
                   encoding="utf-8", newline="\n")
    print(f"{len(names)} bezels -> {out}")


if __name__ == "__main__":
    main()
