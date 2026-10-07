/*
 * PSXS5 - libretro host for the statically linked PCSX-ReARMed core.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_HOST_H
#define PSXS5_HOST_H

#include "../psxs5.h"

/* serial: the disc's (SLUS-00662), for the emulator choice and the memory
 * card; may be empty. */
bool host_load(const char *game_path, const char *serial, const Paths *paths, const Settings *settings,
               char *error, size_t error_size);
void host_unload(void);
bool host_loaded(void);
/* "PCSX-ReARMed" or "Beetle PSX HW": the emulator of the loaded game */
const char *host_core_name(void);
/* The emulator a game would get with these settings ("Beetle PSX HW" or
 * "PCSX-ReARMed"); *why_not_beetle says why Automatic picked PCSX-ReARMed. */
const char *host_emulator_for(const Settings *settings, const char *serial, const char **why_not_beetle);

void host_set_pads(const PadState pads[PSXS5_MAX_PADS]);
/* Port 1's device for the next host_load: 0 a pad, 1 GunCon, 2 Justifier. */
void host_set_gun(int device);
/* Known fixes for the next host_load (GDB_* from gamedb.h): settings the game breaks with. */
void host_set_fixes(unsigned flags);
/* How hard the game is rumbling this player's controller now, 0..1. */
float host_rumble_level(int port);
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
void *host_memory_data(unsigned id); /* retro_get_memory_data of the running core */
size_t host_memory_size(unsigned id);
bool host_read_sector(uint32_t lba, uint8_t out[2048]); /* user data of a disc sector */
/* host_read_sector from this image (any core), until _end. */
bool host_hash_disc_begin(const char *disc_path);
void host_hash_disc_end(void);

int host_disc_count(void);
int host_disc_index(void);
bool host_disc_select(int index);

/* GameShark codes, to the running core */
void host_cheat_reset(void);
void host_cheat_set(unsigned index, const char *code);
/* Beetle PSX HW: its renderer's widescreen mode (no-op for other cores). */
void host_beetle_widescreen(bool on);

#endif
