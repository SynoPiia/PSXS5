/*
 * PSXS5 - shape helpers built on plat_draw_mesh.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "draw.h"

#include "../platform/platform.h"
#include "text.h"

#include <math.h>

uint32_t argb_alpha(uint32_t argb, float alpha)
{
    if (alpha <= 0.0f)
        return argb & 0x00ffffffu;
    if (alpha > 1.0f)
        alpha = 1.0f;
    uint32_t a = (uint32_t)((argb >> 24) * alpha + 0.5f);
    return (argb & 0x00ffffffu) | (a << 24);
}

static void quad(PlatVertex *v, float x0, float y0, float x1, float y1, float x2, float y2,
                 float x3, float y3, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
    /* corners in order: 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left */
    PlatVertex a = {x0, y0, 0, 0, c0}, b = {x1, y1, 1, 0, c1}, c = {x2, y2, 1, 1, c2},
               d = {x3, y3, 0, 1, c3};
    v[0] = a; v[1] = b; v[2] = c;
    v[3] = a; v[4] = c; v[5] = d;
}

void draw_rect(float x, float y, float w, float h, uint32_t argb)
{
    PlatVertex v[6];
    quad(v, x, y, x + w, y, x + w, y + h, x, y + h, argb, argb, argb, argb);
    plat_draw_mesh(NULL, v, 6, NULL, 0);
}

void draw_vgradient(float x, float y, float w, float h, const uint32_t *colors,
                    const float *stops, int count)
{
    PlatVertex v[6 * 8];
    int n = 0;
    for (int i = 0; i + 1 < count && i < 8; ++i)
    {
        float y0 = y + h * stops[i], y1 = y + h * stops[i + 1];
        quad(&v[n], x, y0, x + w, y0, x + w, y1, x, y1, colors[i], colors[i], colors[i + 1],
             colors[i + 1]);
        n += 6;
    }
    plat_draw_mesh(NULL, v, n, NULL, 0);
}

void draw_line(float x0, float y0, float x1, float y1, float thickness, uint32_t argb)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;
    float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
    PlatVertex v[6];
    quad(v, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, argb, argb,
         argb, argb);
    plat_draw_mesh(NULL, v, 6, NULL, 0);
}

void draw_ring(float cx, float cy, float radius, float thickness, uint32_t argb)
{
    enum { SEGMENTS = 32 };
    PlatVertex v[SEGMENTS * 6];
    float r0 = radius - thickness * 0.5f, r1 = radius + thickness * 0.5f;
    for (int i = 0; i < SEGMENTS; ++i)
    {
        float a0 = 6.2831853f * i / SEGMENTS, a1 = 6.2831853f * (i + 1) / SEGMENTS;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        quad(&v[i * 6], cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1, cx + c1 * r0,
             cy + s1 * r0, cx + c0 * r0, cy + s0 * r0, argb, argb, argb, argb);
    }
    plat_draw_mesh(NULL, v, SEGMENTS * 6, NULL, 0);
}

void draw_glow(float x, float y, float w, float h, float spread, uint32_t argb)
{
    /* Four trapezoids around the rectangle, opaque on the inside edge. */
    uint32_t in = argb, out = argb & 0x00ffffffu;
    float X0 = x - spread, Y0 = y - spread, X1 = x + w + spread, Y1 = y + h + spread;
    PlatVertex v[24];
    quad(&v[0], X0, Y0, X1, Y0, x + w, y, x, y, out, out, in, in);             /* top */
    quad(&v[6], x + w, y, X1, Y0, X1, Y1, x + w, y + h, in, out, out, in);     /* right */
    quad(&v[12], x, y + h, x + w, y + h, X1, Y1, X0, Y1, in, in, out, out);    /* bottom */
    quad(&v[18], X0, Y0, x, y, x, y + h, X0, Y1, out, in, in, out);           /* left */
    plat_draw_mesh(NULL, v, 24, NULL, 0);
}

void draw_pad_glyph(enum PadGlyph glyph, float cx, float cy, float size)
{
    float r = size * 0.36f, t = size * 0.11f;
    switch (glyph)
    {
    case GLYPH_CROSS:
        draw_line(cx - r, cy - r, cx + r, cy + r, t, 0xff9fc0ffu);
        draw_line(cx + r, cy - r, cx - r, cy + r, t, 0xff9fc0ffu);
        break;
    case GLYPH_CIRCLE:
        draw_ring(cx, cy, r, t, 0xffff8f8fu);
        break;
    case GLYPH_SQUARE:
        draw_line(cx - r, cy - r, cx + r, cy - r, t, 0xfff3a0d0u);
        draw_line(cx + r, cy - r, cx + r, cy + r, t, 0xfff3a0d0u);
        draw_line(cx + r, cy + r, cx - r, cy + r, t, 0xfff3a0d0u);
        draw_line(cx - r, cy + r, cx - r, cy - r, t, 0xfff3a0d0u);
        break;
    case GLYPH_TRIANGLE:
    {
        float top = cy - r * 1.05f, bottom = cy + r * 0.75f;
        draw_line(cx, top, cx + r, bottom, t, 0xff5fd3a8u);
        draw_line(cx + r, bottom, cx - r, bottom, t, 0xff5fd3a8u);
        draw_line(cx - r, bottom, cx, top, t, 0xff5fd3a8u);
        break;
    }
    }
}

float draw_hint(float x, float y, enum PadGlyph glyph, const char *label, float size,
                uint32_t argb)
{
    draw_pad_glyph(glyph, x + size * 0.5f, y + size * 0.55f, size);
    text_draw(x + size * 1.3f, y, size, FONT_REGULAR, argb, ALIGN_LEFT, label);
    return size * 1.3f + text_width(size, FONT_REGULAR, label) + size * 1.4f;
}
