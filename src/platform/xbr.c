/*
 * PSXS5 - xBR 2x (level 2), the edge-directed pixel-art scaler by Hyllian.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * For each source pixel E the 5x5 neighbourhood decides, per output corner,
 * whether an edge crosses that corner and how steep it is, then blends the
 * corner towards the colour across the edge. Stair-steps become diagonals
 * and curves round off, while flat areas and straight edges stay sharp.
 *
 *          A1 B1 C1
 *       A0  A  B  C C4
 *       D0  D  E  F F4      E -> E0 E1
 *       G0  G  H  I I4           E2 E3
 *          G5 H5 I5
 *
 * Colours are XRGB8888. Rows are processed in parallel by blit_parallel().
 */
#include "xbr.h"

#include "blit.h"

#include <stdlib.h>

/* Perceptual difference: weighted YUV distance. */
static inline int dist(uint32_t a, uint32_t b)
{
    int r = (int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff);
    int g = (int)((a >> 8) & 0xff) - (int)((b >> 8) & 0xff);
    int bl = (int)(a & 0xff) - (int)(b & 0xff);
    int y = abs(r * 299 + g * 587 + bl * 114) / 1000;
    int u = abs(r * -169 + g * -331 + bl * 500) / 1000;
    int v = abs(r * 500 + g * -419 + bl * -81) / 1000;
    return 48 * y + 7 * u + 6 * v;
}

static inline bool same(uint32_t a, uint32_t b)
{
    return dist(a, b) < 155; /* "equal enough": tolerates PS1 dithering noise */
}

/* a + (b - a) * w / 4, per channel */
static inline uint32_t mix4(uint32_t a, uint32_t b, int w)
{
    uint32_t rb = ((a & 0xff00ffu) * (4 - w) + (b & 0xff00ffu) * w) >> 2;
    uint32_t g = ((a & 0x00ff00u) * (4 - w) + (b & 0x00ff00u) * w) >> 2;
    return (rb & 0xff00ffu) | (g & 0x00ff00u);
}

/* One corner. Names follow the bottom-right case (corner E3 between F, H, I);
 * the other three corners call it with the neighbourhood rotated.
 * n1/n2 are the two "side" neighbours of the corner, d the diagonal, and the
 * rest the outer ring used to measure the competing edge directions. */
static inline void corner(uint32_t e, uint32_t f, uint32_t h, uint32_t i, uint32_t c, uint32_t g,
                          uint32_t f4, uint32_t h5, uint32_t i4, uint32_t i5, uint32_t b,
                          uint32_t d, uint32_t *n3, uint32_t *n1, uint32_t *n2)
{
    /* edge running along F-H (across the corner) vs. along E-I */
    int w1 = dist(e, c) + dist(e, g) + dist(i, f4) + dist(i, h5) + 4 * dist(h, f);
    int w2 = dist(h, d) + dist(h, i5) + dist(f, i4) + dist(f, b) + 4 * dist(e, i);
    if (w1 >= w2 || same(e, f) || same(e, h))
        return;
    uint32_t px = dist(e, f) <= dist(e, h) ? f : h;
    int ke = dist(f, g), ki = dist(h, c);
    bool shallow = 2 * ke <= ki && !same(e, g) && !same(d, g);
    bool steep = 2 * ki <= ke && !same(e, c) && !same(b, c);
    if (shallow && steep)
    {
        *n3 = mix4(*n3, px, 3);
        *n1 = mix4(*n1, px, 1);
        *n2 = mix4(*n2, px, 1);
    }
    else if (shallow)
    {
        *n3 = mix4(*n3, px, 3);
        *n2 = mix4(*n2, px, 1);
    }
    else if (steep)
    {
        *n3 = mix4(*n3, px, 3);
        *n1 = mix4(*n1, px, 1);
    }
    else
        *n3 = mix4(*n3, px, 2);
}

typedef struct
{
    const uint32_t *src;
    int w, h;
    size_t pitch;
    uint32_t *dst;
} Ctx;

static void rows(void *opaque, int y_begin, int y_end)
{
    const Ctx *c = opaque;
    const int w = c->w, h = c->h;
    const size_t dpitch = (size_t)w * 2;
#define P(xx, yy)                                                                                 \
    c->src[(size_t)((yy) < 0 ? 0 : (yy) >= h ? h - 1 : (yy)) * c->pitch +                         \
           (size_t)((xx) < 0 ? 0 : (xx) >= w ? w - 1 : (xx))]
    for (int y = y_begin; y < y_end; ++y)
    {
        uint32_t *o0 = c->dst + (size_t)y * 2 * dpitch, *o1 = o0 + dpitch;
        for (int x = 0; x < w; ++x)
        {
            uint32_t A1 = P(x - 1, y - 2), B1 = P(x, y - 2), C1 = P(x + 1, y - 2);
            uint32_t A0 = P(x - 2, y - 1), A = P(x - 1, y - 1), B = P(x, y - 1), C = P(x + 1, y - 1), C4 = P(x + 2, y - 1);
            uint32_t D0 = P(x - 2, y), D = P(x - 1, y), E = P(x, y), F = P(x + 1, y), F4 = P(x + 2, y);
            uint32_t G0 = P(x - 2, y + 1), G = P(x - 1, y + 1), H = P(x, y + 1), I = P(x + 1, y + 1), I4 = P(x + 2, y + 1);
            uint32_t G5 = P(x - 1, y + 2), H5 = P(x, y + 2), I5 = P(x + 1, y + 2);
            uint32_t e0 = E, e1 = E, e2 = E, e3 = E;
            /* bottom-right (E3), bottom-left (E2), top-left (E0), top-right (E1):
             * the same rule with the neighbourhood rotated by 90 degrees each time */
            corner(E, F, H, I, C, G, F4, H5, I4, I5, B, D, &e3, &e1, &e2);
            corner(E, H, D, G, I, A, H5, D0, G5, G0, F, B, &e2, &e3, &e0);
            corner(E, D, B, A, G, C, D0, B1, A0, A1, H, F, &e0, &e2, &e1);
            corner(E, B, F, C, A, I, B1, F4, C1, C4, D, H, &e1, &e0, &e3);
            o0[x * 2] = e0;
            o0[x * 2 + 1] = e1;
            o1[x * 2] = e2;
            o1[x * 2 + 1] = e3;
        }
    }
#undef P
}

void xbr2x(const uint32_t *src, int w, int h, size_t pitch_px, uint32_t *dst)
{
    Ctx c = {src, w, h, pitch_px, dst};
    blit_parallel(rows, &c, h);
}
