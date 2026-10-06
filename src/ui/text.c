/*
 * PSXS5 - TrueType text (Inter) on top of the platform mesh API.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Glyphs for Latin-1 are packed into one atlas per (weight, size bucket) the
 * first time that size is used; a line of text is then a single mesh draw.
 */
#include "text.h"

#include "../platform/platform.h"
#include "stb_truetype.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_CP 32
#define CP_COUNT 224 /* U+0020..U+00FF */
#define MAX_ATLASES 16

typedef struct
{
    int weight, px;
    int width, height;
    float ascent;
    stbtt_packedchar chars[CP_COUNT];
    PlatTexture *texture;
} Atlas;

static unsigned char *font_data[2];
static stbtt_fontinfo font_info[2];
static bool font_ok[2];
static Atlas atlases[MAX_ATLASES];
static int atlas_count;

static const int buckets[] = {16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128};

static unsigned char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = size > 0 ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

bool text_init(void)
{
    static const char *files[2] = {"fonts/Inter-400.ttf", "fonts/Inter-600.ttf"};
    for (int w = 0; w < 2; ++w)
    {
        char path[PSXS5_PATH_MAX];
        plat_asset_path(path, sizeof(path), files[w]);
        font_data[w] = read_file(path);
        font_ok[w] = font_data[w] &&
                     stbtt_InitFont(&font_info[w], font_data[w],
                                    stbtt_GetFontOffsetForIndex(font_data[w], 0));
        if (!font_ok[w])
            psxs5_log("font missing or invalid: %s", path);
    }
    if (!font_ok[FONT_BOLD] && font_ok[FONT_REGULAR])
    {
        font_info[FONT_BOLD] = font_info[FONT_REGULAR];
        font_ok[FONT_BOLD] = true;
    }
    return font_ok[FONT_REGULAR];
}

void text_shutdown(void)
{
    for (int i = 0; i < atlas_count; ++i)
        plat_texture_free(atlases[i].texture);
    atlas_count = 0;
    for (int w = 0; w < 2; ++w)
        free(font_data[w]);
}

static int bucket_for(float size)
{
    for (size_t i = 0; i < sizeof(buckets) / sizeof(buckets[0]); ++i)
        if (buckets[i] >= size - 0.5f)
            return buckets[i];
    return buckets[sizeof(buckets) / sizeof(buckets[0]) - 1];
}

static Atlas *get_atlas(int weight, float size)
{
    weight = weight ? FONT_BOLD : FONT_REGULAR;
    if (!font_ok[weight])
        return NULL;
    int px = bucket_for(size);
    for (int i = 0; i < atlas_count; ++i)
        if (atlases[i].weight == weight && atlases[i].px == px)
            return &atlases[i];
    if (atlas_count == MAX_ATLASES)
        return &atlases[0];

    Atlas *a = &atlases[atlas_count];
    int over = px <= 32 ? 2 : 1;
    a->width = px <= 32 ? 512 : px <= 64 ? 1024 : 2048;
    a->height = px <= 48 ? 512 : 1024;
    unsigned char *alpha = calloc((size_t)a->width * a->height, 1);
    if (!alpha)
        return NULL;
    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, alpha, a->width, a->height, 0, 1, NULL);
    stbtt_PackSetOversampling(&pc, (unsigned)over, 1);
    stbtt_PackFontRange(&pc, font_data[weight] ? font_data[weight] : font_data[0], 0,
                        (float)px, FIRST_CP, CP_COUNT, a->chars);
    stbtt_PackEnd(&pc);

    uint8_t *rgba = malloc((size_t)a->width * a->height * 4);
    if (!rgba)
    {
        free(alpha);
        return NULL;
    }
    for (int i = 0; i < a->width * a->height; ++i)
    {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    a->texture = plat_texture_create(rgba, a->width, a->height, true);
    free(rgba);
    free(alpha);

    int asc, desc, gap;
    stbtt_GetFontVMetrics(&font_info[weight], &asc, &desc, &gap);
    a->ascent = asc * stbtt_ScaleForPixelHeight(&font_info[weight], (float)px);
    a->weight = weight;
    a->px = px;
    ++atlas_count;
    return a;
}

/* Decodes one UTF-8 sequence; anything outside Latin-1 becomes '?'. */
static int next_cp(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    int cp, len;
    if (s[0] < 0x80)
        cp = s[0], len = 1;
    else if ((s[0] & 0xe0) == 0xc0 && s[1])
        cp = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f), len = 2;
    else if ((s[0] & 0xf0) == 0xe0 && s[1] && s[2])
        cp = '?', len = 3;
    else if ((s[0] & 0xf8) == 0xf0 && s[1] && s[2] && s[3])
        cp = '?', len = 4;
    else
        cp = s[0], len = 1; /* stray Latin-1 byte */
    *p += len;
    return (cp >= FIRST_CP && cp < FIRST_CP + CP_COUNT) ? cp : '?';
}

float text_width(float size, int weight, const char *s)
{
    Atlas *a = get_atlas(weight, size);
    if (!a)
        return 0.0f;
    float scale = size / (float)a->px, w = 0.0f;
    while (*s)
        w += a->chars[next_cp(&s) - FIRST_CP].xadvance;
    return w * scale;
}

void text_draw(float x, float y, float size, int weight, uint32_t argb, int align, const char *s)
{
    Atlas *a = get_atlas(weight, size);
    if (!a || !a->texture || !*s)
        return;
    if (align != ALIGN_LEFT)
    {
        float w = text_width(size, weight, s);
        x -= align == ALIGN_CENTER ? w * 0.5f : w;
    }
    float scale = size / (float)a->px;
    enum { MAX_GLYPHS = 256 };
    static PlatVertex v[MAX_GLYPHS * 6];
    int n = 0;
    float pen_x = 0.0f, pen_y = 0.0f;
    while (*s && n < MAX_GLYPHS * 6)
    {
        int cp = next_cp(&s);
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(a->chars, a->width, a->height, cp - FIRST_CP, &pen_x, &pen_y, &q, 0);
        float x0 = x + q.x0 * scale, x1 = x + q.x1 * scale;
        float y0 = y + (a->ascent + q.y0) * scale, y1 = y + (a->ascent + q.y1) * scale;
        PlatVertex tl = {x0, y0, q.s0, q.t0, argb}, tr = {x1, y0, q.s1, q.t0, argb};
        PlatVertex bl = {x0, y1, q.s0, q.t1, argb}, br = {x1, y1, q.s1, q.t1, argb};
        v[n++] = tl; v[n++] = tr; v[n++] = br;
        v[n++] = tl; v[n++] = br; v[n++] = bl;
    }
    plat_draw_mesh(a->texture, v, n, NULL, 0);
}

void text_draw_fit(float x, float y, float size, int weight, uint32_t argb, int align,
                   float max_width, const char *s)
{
    if (text_width(size, weight, s) <= max_width)
    {
        text_draw(x, y, size, weight, argb, align, s);
        return;
    }
    char buf[256];
    size_t len = strlen(s);
    if (len >= sizeof(buf) - 4)
        len = sizeof(buf) - 4;
    while (len > 0)
    {
        --len;
        while (len > 0 && ((unsigned char)s[len] & 0xc0) == 0x80)
            --len; /* stay on a UTF-8 boundary */
        memcpy(buf, s, len);
        strcpy(buf + len, "...");
        if (text_width(size, weight, buf) <= max_width)
            break;
    }
    text_draw(x, y, size, weight, argb, align, buf);
}

/* ---------------------------------------------------------------- CPU rendering */

static float cpu_line_width(const stbtt_fontinfo *f, float scale, const char *s, size_t n)
{
    float w = 0.0f;
    const char *end = s + n;
    while (s < end && *s)
    {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(f, next_cp(&s), &adv, &lsb);
        w += adv * scale;
    }
    return w;
}

static void cpu_draw_line(uint8_t *rgba, int width, int height, float x, int baseline,
                          const stbtt_fontinfo *f, float scale, uint32_t argb, const char *s,
                          size_t n)
{
    const char *end = s + n;
    uint8_t cr = (argb >> 16) & 0xff, cg = (argb >> 8) & 0xff, cb = argb & 0xff;
    while (s < end && *s)
    {
        int cp = next_cp(&s);
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(f, cp, scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0 && gw < 512 && gh < 512)
        {
            static unsigned char glyph[512 * 512];
            stbtt_MakeCodepointBitmap(f, glyph, gw, gh, gw, scale, scale, cp);
            for (int gy = 0; gy < gh; ++gy)
                for (int gx = 0; gx < gw; ++gx)
                {
                    int px = (int)x + x0 + gx, py = baseline + y0 + gy;
                    if (px < 0 || py < 0 || px >= width || py >= height)
                        continue;
                    unsigned a = glyph[gy * gw + gx] * (argb >> 24) / 255;
                    uint8_t *d = &rgba[(py * width + px) * 4];
                    d[0] = (uint8_t)((cr * a + d[0] * (255 - a)) / 255);
                    d[1] = (uint8_t)((cg * a + d[1] * (255 - a)) / 255);
                    d[2] = (uint8_t)((cb * a + d[2] * (255 - a)) / 255);
                }
        }
        x += adv * scale;
    }
}

int text_render_rgba(uint8_t *rgba, int width, int height, int y, float size, int weight,
                     uint32_t argb, int max_width, const char *s)
{
    weight = weight ? FONT_BOLD : FONT_REGULAR;
    if (!font_ok[weight])
        return 0;
    const stbtt_fontinfo *f = &font_info[weight];
    float scale = stbtt_ScaleForPixelHeight(f, size);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
    int line_h = (int)((asc - desc + gap) * scale);
    int start_y = y;

    while (*s)
    {
        /* longest run of words that fits */
        size_t best = 0, i = 0;
        while (s[i])
        {
            size_t j = i;
            while (s[j] && s[j] != ' ')
                ++j;
            if (best > 0 && cpu_line_width(f, scale, s, j) > max_width)
                break;
            best = j;
            if (!s[j])
                break;
            i = j + 1;
        }
        if (best == 0)
            best = strlen(s);
        float lw = cpu_line_width(f, scale, s, best);
        cpu_draw_line(rgba, width, height, (width - lw) * 0.5f, y + (int)(asc * scale), f, scale,
                      argb, s, best);
        y += line_h;
        s += best;
        while (*s == ' ')
            ++s;
        if (y + line_h > height)
            break;
    }
    return y - start_y;
}
