/*
 * PSXS5 - settings persistence.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_CONFIG_H
#define PSXS5_CONFIG_H

#include "psxs5.h"

void config_defaults(Settings *s);
void config_load(Settings *s, const char *path);
/* A game's own settings: the console-wide ones with the game's file over
 * them. Returns false (and *out = *global) when the game has no file. */
bool config_load_game(Settings *out, const Settings *global, const char *path);
bool config_save(const Settings *s, const char *path);
bool config_peek_bool(const char *path, const char *key);
void config_paths(Paths *p, const char *root);

/* A profile's own folders (saves, states, psxs5.ini) under dir. */
void config_user_paths(Paths *p, const char *dir);

#endif
