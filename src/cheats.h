/*
 * PSXS5 - RetroArch .cht cheat files (GameShark / Action Replay codes).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_CHEATS_H
#define PSXS5_CHEATS_H

#include "library.h"

#define CHEATS_MAX 512

typedef struct
{
    char desc[80];
    char code[256]; /* "80012345 0063+D0012345 0001" */
    bool enabled;
} Cheat;

typedef struct
{
    Cheat items[CHEATS_MAX];
    int count;
    char source[PSXS5_PATH_MAX]; /* the .cht that was loaded, "" if none */
    char state_path[PSXS5_PATH_MAX];
} CheatList;

/* Finds the best .cht for `game`: one beside the game files first, then the
 * library in `cheats_dir` (libretro-database "Sony - PlayStation" layout).
 * Restores which codes were switched on last time. */
bool cheats_load(CheatList *list, const Game *game, const char *cheats_dir);
void cheats_clear(CheatList *list);

/* Pushes enabled codes into the running core and remembers the selection. */
void cheats_apply(const CheatList *list);
void cheats_save_selection(const CheatList *list);

#endif
