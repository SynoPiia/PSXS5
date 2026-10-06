/*
 * PSXS5 - PS5 screen output without SDL's video driver.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_PS5_VIDEO_H
#define PSXS5_PS5_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PS5_SCREEN_W 1920
#define PS5_SCREEN_H 1080

bool ps5_video_open(char *error, size_t size);
/* Shows one 1920x1080 RGBA (R,G,B,A byte order) frame and waits for vblank. */
void ps5_video_present(const uint32_t *pixels, size_t pitch_bytes);

#endif
