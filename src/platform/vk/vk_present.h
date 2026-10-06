/*
 * PSXS5 v2 - the screen through Vulkan (RADV, VK_KHR_display on VideoOut).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_VK_PRESENT_H
#define PSXS5_VK_PRESENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Takes over the TV through Vulkan. The interface keeps drawing on the CPU
 * into a canvas_w x canvas_h RGBA canvas, which is uploaded every frame. */
bool vkp_open(int canvas_w, int canvas_h, char *error, size_t size);
/* Uploads the canvas (R,G,B,A bytes), draws it over the game picture (none
 * yet), presents, and waits for the TV's refresh. */
void vkp_present(const uint32_t *pixels, size_t pitch_bytes);
void vkp_close(void);
/* "1920x1080 @ 59.94 Hz" once open */
const char *vkp_describe(void);

#endif
