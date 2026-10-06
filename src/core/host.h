/*
 * PSXS5 - libretro host for the statically linked PCSX-ReARMed core.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_HOST_H
#define PSXS5_HOST_H

#include "../psxs5.h"

bool host_load(const char *game_path, const Paths *paths, const Settings *settings,
               char *error, size_t error_size);
void host_unload(void);
bool host_loaded(void);

void host_set_pads(const PadState pads[PSXS5_MAX_PADS]);
/* True while the game keeps this DualShock in digital mode (sticks ignored). */
bool host_pad_digital(int port);
void host_run_frame(void);
void host_reset(void);
void host_apply_settings(const Settings *settings); /* takes effect without reloading */

double host_fps(void);
int host_sample_rate(void);
float host_aspect(void);
/* Latest frame; returns NULL when the core has not produced one yet. */
const void *host_frame(int *width, int *height, size_t *pitch, int *pixel_format,
                       bool *fresh);

bool host_save_state(const char *path);
bool host_load_state(const char *path);
/* Save states in memory (rewind, quick resume). */
size_t host_state_size(void);
bool host_serialize(void *buffer, size_t size);
bool host_unserialize(const void *buffer, size_t size);
/* The latest frame scaled to w x h RGBA (save-state thumbnails). */
bool host_capture(uint8_t *rgba, int w, int h);
/* Folder the core reads fan-translation patches from (before host_load). */
void host_set_patches_dir(const char *dir);

/* For RetroAchievements */
struct retro_memory_map;
const struct retro_memory_map *host_memory_map(void);
bool host_read_sector(uint32_t lba, uint8_t out[2048]); /* user data of a disc sector */

int host_disc_count(void);
int host_disc_index(void);
bool host_disc_select(int index);

#endif
