/*
 * PSXS5 - RetroAchievements (retroachievements.org) through rcheevos.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_ACHIEVEMENTS_H
#define PSXS5_ACHIEVEMENTS_H

#include "../psxs5.h"

/* Reads <root>/retroachievements.ini (user=, token=, hardcore=) written by
 * `tools/psxs5_sync.py ra-login`, and signs in in the background. */
void ra_init(const Paths *paths);
void ra_shutdown(void);

/* After the core loaded a game: hashes the disc and loads its achievements. */
void ra_game_loaded(void);
void ra_game_unloaded(void);
void ra_frame(void);  /* after every emulated frame */
void ra_idle(void);   /* while paused (menus): keeps the server session alive */
void ra_reset(void);  /* the console was reset */

bool ra_signed_in(void);
const char *ra_user(void);
unsigned ra_user_score(void); /* points, 0 when not signed in */
/* The loaded game's achievements; false when it has none or none loaded. */
bool ra_game_progress(int *unlocked, int *total);
bool ra_hardcore(void);          /* blocks save states, cheats and rewind */
void ra_set_hardcore(bool on);   /* saved to the ini */
/* "12 of 40 achievements, 115 of 400 points", or "" when no set is loaded */
void ra_game_summary(char *out, size_t size);

/* Messages for the UI (unlocks, leaderboards...). Returns false when empty. */
bool ra_next_message(char *title, size_t title_size, char *detail, size_t detail_size);

#endif
