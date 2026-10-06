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

int host_disc_count(void);
int host_disc_index(void);
bool host_disc_select(int index);

#endif
