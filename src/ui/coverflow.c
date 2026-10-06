/*
 * PSXS5 - the coverflow game library.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Covers sit on a shelf: the selected one faces you, its neighbours turn away
 * in perspective and stack towards the edges. Each cover is drawn as vertical
 * strips so the perspective stays correct with affine-only triangle mapping,
 * and is mirrored below with a fading reflection.
 */
#include "coverflow.h"

#include "../covers.h"
#include "../platform/platform.h"
#include "draw.h"
#include "sfx.h"
#include "text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRIPS 12
#define CENTER_X 960.0f
#define CENTER_Y 450.0f
#define COVER_H 560.0f
#define FOCAL 1500.0f
#define SIDE_GAP 540.0f   /* centre to first neighbour */
#define STACK_GAP 132.0f  /* between further neighbours */
#define MAX_YAW 1.01f     /* ~58 degrees */
#define LAUNCH_TIME 0.45f

static const uint32_t BG_COLORS[] = {0xff1a2470u, 0xff3049c4u, 0xff2a3faeu, 0xff0d1440u};
static const float BG_STOPS[] = {0.0f, 0.46f, 0.62f, 1.0f};

static uint32_t lerp_color(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0xff000000u;
    for (int s = 0; s < 24; s += 8)
    {
        float ca = (float)((a >> s) & 0xff), cb = (float)((b >> s) & 0xff);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

/* The gradient is built once on the CPU and blitted each frame: redrawing a
 * full-screen gradient mesh every frame is costly in the software renderer. */
void coverflow_backdrop(void)
{
    static PlatTexture *backdrop;
    static bool tried;
    if (!backdrop && !tried)
    {
        tried = true;
        enum { W = 1920, H = 1080 }; /* screen size: blitted 1:1, no per-frame scaling */
        uint8_t *px = malloc((size_t)W * H * 4); /* 8 MB, once */
        if (!px)
            return;
        for (int y = 0; y < H; ++y)
        {
            float t = (float)y / (H - 1);
            int i = 0;
            while (i < 2 && t > BG_STOPS[i + 1])
                ++i;
            float k = (t - BG_STOPS[i]) / (BG_STOPS[i + 1] - BG_STOPS[i]);
            uint32_t c = lerp_color(BG_COLORS[i], BG_COLORS[i + 1], k);
            for (int x = 0; x < W; ++x)
            {
                uint8_t *p = &px[(y * W + x) * 4];
                p[0] = (c >> 16) & 0xff;
                p[1] = (c >> 8) & 0xff;
                p[2] = c & 0xff;
                p[3] = 255;
            }
        }
        backdrop = plat_texture_create(px, W, H, true);
        free(px);
    }
    if (backdrop)
        plat_draw_texture(backdrop, 0, 0, (float)plat_width(), (float)plat_height(), 0xffffffffu,
                          false);
    else
        draw_vgradient(0, 0, (float)plat_width(), (float)plat_height(), BG_COLORS, BG_STOPS, 4);
}

void coverflow_init(Coverflow *cf, int cursor)
{
    memset(cf, 0, sizeof(*cf));
    cf->cursor = cursor;
    cf->pos = (float)cursor;
}

static const char *region_name(const char *serial)
{
    if (!serial[0])
        return "Unknown region";
    if (!strncmp(serial, "SLUS", 4) || !strncmp(serial, "SCUS", 4))
        return "USA";
    if (!strncmp(serial, "SLES", 4) || !strncmp(serial, "SCES", 4) || !strncmp(serial, "SCED", 4))
        return "Europe";
    return "Japan";
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

typedef struct
{
    float x[STRIPS + 1], top[STRIPS + 1], bottom[STRIPS + 1];
} Shape;

/* Projects a cover of half-size (hw, hh) at horizontal offset `ox`, turned by `yaw`,
 * pushed towards the viewer by `lift`. */
static void project(Shape *s, float ox, float hw, float hh, float yaw, float lift)
{
    float c = cosf(yaw), sn = sinf(yaw);
    for (int k = 0; k <= STRIPS; ++k)
    {
        float x3 = ((float)k / STRIPS - 0.5f) * 2.0f * hw;
        float z = -x3 * sn - lift;
        float f = FOCAL / (FOCAL + z);
        s->x[k] = CENTER_X + ox + x3 * c * f;
        s->top[k] = CENTER_Y - hh * f;
        s->bottom[k] = CENTER_Y + hh * f;
    }
}

static void draw_shape(PlatTexture *tex, const Shape *s, uint32_t tint, float reflect_alpha,
                       bool flat)
{
    PlatVertex v[STRIPS * 12];
    int n = 0;
    if (flat && tex)
    {
        /* Facing the viewer: one seamless blit instead of strips. */
        plat_draw_texture(tex, s->x[0], s->top[0], s->x[STRIPS] - s->x[0],
                          s->bottom[0] - s->top[0], tint, true);
    }
    else
    {
        for (int k = 0; k < STRIPS; ++k)
        {
            float u0 = (float)k / STRIPS, u1 = (float)(k + 1) / STRIPS;
            /* overlap the next strip by a pixel: the software rasteriser
             * otherwise leaves a hairline of background between strips */
            float xr = s->x[k + 1] + (k + 1 < STRIPS ? 1.0f : 0.0f);
            PlatVertex a = {s->x[k], s->top[k], u0, 0, tint}, b = {xr, s->top[k + 1], u1, 0, tint};
            PlatVertex c = {xr, s->bottom[k + 1], u1, 1, tint}, d = {s->x[k], s->bottom[k], u0, 1, tint};
            v[n++] = a; v[n++] = b; v[n++] = c;
            v[n++] = a; v[n++] = c; v[n++] = d;
        }
        plat_draw_mesh(tex, v, n, NULL, 0);
    }

    if (reflect_alpha <= 0.0f)
        return;
    /* mirror: the bottom 45% of the cover, upside down, fading to nothing */
    n = 0;
    uint32_t top_c = argb_alpha(tint, reflect_alpha), end_c = tint & 0x00ffffffu;
    for (int k = 0; k < STRIPS; ++k)
    {
        float u0 = (float)k / STRIPS, u1 = (float)(k + 1) / STRIPS;
        float h0 = (s->bottom[k] - s->top[k]) * 0.45f, h1 = (s->bottom[k + 1] - s->top[k + 1]) * 0.45f;
        float y0 = s->bottom[k] + 6, y1 = s->bottom[k + 1] + 6;
        float xr = s->x[k + 1] + (k + 1 < STRIPS ? 1.0f : 0.0f);
        PlatVertex a = {s->x[k], y0, u0, 1, top_c}, b = {xr, y1, u1, 1, top_c};
        PlatVertex c = {xr, y1 + h1, u1, 0.55f, end_c}, d = {s->x[k], y0 + h0, u0, 0.55f, end_c};
        v[n++] = a; v[n++] = b; v[n++] = c;
        v[n++] = a; v[n++] = c; v[n++] = d;
    }
    plat_draw_mesh(tex, v, n, NULL, 0);
}

static void draw_details(const Game *g, float t)
{
    if (t <= 0.0f)
        return;
    float w = 560, x = plat_width() - w * t, y = 150, h = 640;
    draw_rect(x, y, w, h, argb_alpha(0xe00d1440u, t));
    draw_rect(x, y, 6, h, argb_alpha(0xff8fb0ffu, t));
    uint32_t dim = argb_alpha(0xffc9d2ffu, t), white = argb_alpha(0xffffffffu, t);
    text_draw_fit(x + 40, y + 36, 32, FONT_BOLD, white, ALIGN_LEFT, w - 80, g->title);
    char line[160];
    const char *labels[] = {"Serial", "Region", "Discs", "Format", "Folder"};
    char values[5][128];
    snprintf(values[0], 128, "%s", g->serial[0] ? g->serial : "Unknown");
    snprintf(values[1], 128, "%s", region_name(g->serial));
    snprintf(values[2], 128, "%d", g->discs);
    snprintf(values[3], 128, "%s", path_ext(g->path));
    const char *folder = strrchr(g->folder, '/');
    snprintf(values[4], 128, "%s", folder ? folder + 1 : g->folder);
    for (int i = 0; i < 5; ++i)
    {
        text_draw(x + 40, y + 110 + i * 60, 24, FONT_REGULAR, dim, ALIGN_LEFT, labels[i]);
        text_draw_fit(x + 200, y + 110 + i * 60, 24, FONT_BOLD, white, ALIGN_LEFT, w - 240,
                      values[i]);
    }
    snprintf(line, sizeof(line), "Saves and cheats follow the serial.");
    text_draw(x + 40, y + h - 60, 20, FONT_REGULAR, dim, ALIGN_LEFT, line);
}

CoverflowAction coverflow_frame(Coverflow *cf, const Library *lib, uint32_t pressed, float dt,
                                const char *notice)
{
    const int count = lib->count;
    CoverflowAction action = CF_NONE;

    /* ------------------------------------------------ input */
    if (cf->launch_t <= 0.0f && count > 0)
    {
        int before = cf->cursor;
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_UP)))
            --cf->cursor;
        if (pressed & (BIT(BTN_RIGHT) | BIT(BTN_DOWN)))
            ++cf->cursor;
        if (pressed & BIT(BTN_L1))
            cf->cursor -= 8;
        if (pressed & BIT(BTN_R1))
            cf->cursor += 8;
        cf->cursor = cf->cursor < 0 ? 0 : cf->cursor >= count ? count - 1 : cf->cursor;
        if (cf->cursor != before)
            sfx_play(SFX_CLICK);
        if (pressed & BIT(BTN_TRIANGLE))
        {
            cf->details = !cf->details;
            sfx_play(cf->details ? SFX_SELECT : SFX_BACK);
        }
        if ((pressed & BIT(BTN_CIRCLE)) && cf->details)
        {
            cf->details = false;
            sfx_play(SFX_BACK);
        }
        if (pressed & BIT(BTN_CROSS))
        {
            cf->launch_t = 0.0001f;
            cf->details = false;
            sfx_play(SFX_SELECT);
        }
    }
    if (pressed & BIT(BTN_SQUARE) && cf->launch_t <= 0.0f)
        action = CF_SETTINGS;

    /* ------------------------------------------------ animation */
    float ease = 1.0f - expf(-dt * 11.0f);
    cf->pos += (cf->cursor - cf->pos) * ease;
    if (fabsf(cf->cursor - cf->pos) < 0.001f)
        cf->pos = (float)cf->cursor;
    cf->details_t = clampf(cf->details_t + (cf->details ? dt : -dt) * 5.0f, 0.0f, 1.0f);
    if (cf->launch_t > 0.0f)
    {
        cf->launch_t += dt;
        if (cf->launch_t >= LAUNCH_TIME)
        {
            cf->launch_t = 0.0f;
            action = CF_PLAY;
        }
    }
    covers_update(cf->cursor);

    /* ------------------------------------------------ shelf */
    coverflow_backdrop();
    if (count > 0)
    {
        /* far to near so the selected cover is drawn last */
        int order[64], n = 0;
        int first = (int)floorf(cf->pos) - 7, last = (int)ceilf(cf->pos) + 7;
        for (int i = first; i <= last; ++i)
            if (i >= 0 && i < count && n < 64)
                order[n++] = i;
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                if (fabsf(order[b] - cf->pos) > fabsf(order[a] - cf->pos))
                {
                    int t = order[a];
                    order[a] = order[b];
                    order[b] = t;
                }

        float launch = cf->launch_t > 0.0f ? cf->launch_t / LAUNCH_TIME : 0.0f;
        for (int k = 0; k < n; ++k)
        {
            int i = order[k];
            float d = i - cf->pos, ad = fabsf(d), near = ad < 1.0f ? ad : 1.0f;
            float ox = ad < 1.0f ? d * SIDE_GAP
                                 : (d > 0 ? 1.0f : -1.0f) * (SIDE_GAP + (ad - 1.0f) * STACK_GAP);
            float yaw = clampf(d, -1.0f, 1.0f) * MAX_YAW;
            float scale = 1.0f - near * 0.18f;
            float lift = (1.0f - near) * 90.0f;
            if (i == cf->cursor)
                scale *= 1.0f + launch * 0.25f;

            PlatTexture *tex = covers_get(i);
            int tw = 1, th = 1;
            plat_texture_size(tex, &tw, &th);
            float aspect = tex ? (float)tw / th : 0.9f;
            float hh = COVER_H * 0.5f * scale, hw = hh * aspect;

            Shape s;
            project(&s, ox, hw, hh, yaw, lift);
            uint8_t shade = (uint8_t)(255 * (1.0f - near * 0.28f) * (1.0f - launch * (i != cf->cursor)));
            uint32_t tint = 0xff000000u | (uint32_t)shade << 16 | (uint32_t)shade << 8 | shade;

            if (i == cf->cursor && ad < 0.2f && launch < 0.5f)
            {
                float g = (1.0f - ad / 0.2f) * (1.0f - launch * 2.0f);
                float x0 = s.x[0], x1 = s.x[STRIPS], y0 = s.top[0], y1 = s.bottom[0];
                draw_glow(x0, y0, x1 - x0, y1 - y0, 36.0f, argb_alpha(0xa08fb0ffu, g));
                draw_glow(x0, y0, x1 - x0, y1 - y0, 4.0f, argb_alpha(0xffffffffu, g));
            }
            if (tex)
                draw_shape(tex, &s, tint, ad < 1.5f ? 0.30f * (1.0f - launch) : 0.0f, fabsf(yaw) < 0.02f);
            else
                draw_shape(NULL, &s, 0xff1d2348u, 0.0f, false); /* still loading */
        }

        const Game *g = &lib->games[cf->cursor];
        float text_a = 1.0f - launch;
        text_draw_fit(CENTER_X, 870, 56, FONT_BOLD, argb_alpha(0xffffffffu, text_a), ALIGN_CENTER,
                      1600, g->title);
        char meta[128];
        snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s  \xc2\xb7  %d disc%s",
                 g->serial[0] ? g->serial : "No serial", region_name(g->serial), g->discs,
                 g->discs == 1 ? "" : "s");
        text_draw(CENTER_X, 944, 26, FONT_REGULAR, argb_alpha(0xffc9d2ffu, text_a), ALIGN_CENTER,
                  meta);
        draw_details(g, cf->details_t);
    }
    else
    {
        text_draw(CENTER_X, 400, 48, FONT_BOLD, 0xffffffffu, ALIGN_CENTER, "Your shelf is empty");
        text_draw(CENTER_X, 480, 26, FONT_REGULAR, 0xffc9d2ffu, ALIGN_CENTER,
                  "On your PC:  python tools/psxs5_sync.py upload --host <PS5 IP>");
        text_draw(CENTER_X, 524, 26, FONT_REGULAR, 0xffc9d2ffu, ALIGN_CENTER,
                  "Then choose Settings > Rescan library.");
    }

    /* ------------------------------------------------ chrome */
    text_draw(64, 44, 44, FONT_BOLD, 0xffffffffu, ALIGN_LEFT, PSXS5_NAME);
    char sub[96];
    snprintf(sub, sizeof(sub), "%d game%s  \xc2\xb7  v" PSXS5_VERSION, count, count == 1 ? "" : "s");
    text_draw(66, 102, 22, FONT_REGULAR, 0xffc9d2ffu, ALIGN_LEFT, sub);
    int pending = covers_downloading();
    if (pending > 0)
    {
        char dl[64];
        snprintf(dl, sizeof(dl), "Getting covers (%d)", pending);
        text_draw(plat_width() - 64, 56, 22, FONT_REGULAR, 0xffc9d2ffu, ALIGN_RIGHT, dl);
    }
    if (notice && *notice)
        text_draw(plat_width() - 64, 96, 22, FONT_REGULAR, 0xffffd28au, ALIGN_RIGHT, notice);

    float hx = 64, hy = 1008;
    hx += draw_hint(hx, hy, GLYPH_CROSS, "Play", 26, 0xffdfe5ffu);
    hx += draw_hint(hx, hy, GLYPH_TRIANGLE, cf->details ? "Hide details" : "Details", 26, 0xffdfe5ffu);
    hx += draw_hint(hx, hy, GLYPH_SQUARE, "Settings", 26, 0xffdfe5ffu);
    text_draw(hx, hy, 26, FONT_REGULAR, 0xffdfe5ffu, ALIGN_LEFT, "L1 / R1  Jump");

    if (cf->launch_t > 0.0f)
        draw_rect(0, 0, (float)plat_width(), (float)plat_height(),
                  argb_alpha(0xff000000u, cf->launch_t / LAUNCH_TIME));
    return action;
}
