/*
 * PSXS5 - shape helpers built on plat_draw_mesh.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_DRAW_H
#define PSXS5_DRAW_H

#include "../psxs5.h"

enum PadGlyph
{
    GLYPH_CROSS,
    GLYPH_CIRCLE,
    GLYPH_SQUARE,
    GLYPH_TRIANGLE,
};

void draw_rect(float x, float y, float w, float h, uint32_t argb);
/* Vertical gradient through `count` stops at positions 0..1. */
void draw_vgradient(float x, float y, float w, float h, const uint32_t *colors,
                    const float *stops, int count);
void draw_line(float x0, float y0, float x1, float y1, float thickness, uint32_t argb);
void draw_ring(float cx, float cy, float radius, float thickness, uint32_t argb);
/* Soft halo around a rectangle: `spread` pixels fading from `argb` to transparent. */
void draw_glow(float x, float y, float w, float h, float spread, uint32_t argb);
void draw_pad_glyph(enum PadGlyph glyph, float cx, float cy, float size);
/* "[glyph] label" button hint; returns the width used. */
float draw_hint(float x, float y, enum PadGlyph glyph, const char *label, float size,
                uint32_t argb);

uint32_t argb_alpha(uint32_t argb, float alpha); /* scales the alpha channel */

#endif
