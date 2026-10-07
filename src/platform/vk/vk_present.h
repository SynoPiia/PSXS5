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
/* A core renders through Vulkan and has handed over a picture. */
bool vkp_game_image_ready(void);
/* Draw that picture in this rectangle of the canvas, under it, this frame. */
/* crop: share of the picture's height hidden at the top and at the bottom */
/* shader: 0 none, 1 sharp bilinear, 2 CRT; tex_*: the picture's size; lines: the PS1's */
void vkp_show_game(float x, float y, float w, float h, float crop, int shader, int tex_w, int tex_h, int lines);

#endif
