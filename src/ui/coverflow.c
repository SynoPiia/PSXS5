/*
 * PSXS5 - the game shelf (home screen).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Covers stand on a shelf: the selected one faces you, its neighbours turn
 * away towards the edges. Categories (L1/R1) filter it, OPTIONS sorts it, and
 * the background takes the colour of the selected cover. Everything here is
 * drawn with blits and fills, the software renderer's fast paths.
 */
#include "coverflow.h"

#include "../app.h"
#include "../config.h"
#include "../covers.h"
#include "../play.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../ra/achievements.h"
#include "../stats.h"
#include "../update.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CENTER_X 960.0f
#define CENTER_Y 430.0f
#define COVER_H 500.0f
#define SIDE_GAP 400.0f  /* centre to the first neighbour */
#define STACK_GAP 150.0f /* between further neighbours */
#define SIDE_SCALE 0.74f
#define SIDE_SQUEEZE 0.58f /* turned-away covers look narrower */
#define LAUNCH_TIME 0.3f

static struct
{
    int *view; /* library indices on the shelf, in order */
    int view_count, view_capacity;
    int cursor; /* position in view */
    float pos;  /* animated position */
    bool details;
    float details_t, launch_t;
    float chip_x, chip_w; /* animated category highlight */
    float tint[3];        /* animated background colour */
    float title_fade;     /* text fades in after a move */
    int last_cursor_game;
    /* "Continue or start over" when a game has a quick-resume save */
    bool dialog, resume;
    int dialog_choice;
    float dialog_t;
    long resume_age;
    PlatTexture *resume_thumb;
} S;

static const char *const CATEGORY_NAMES[CAT_COUNT] = {
    "All games", "Recently played", "Favorites", "Multi-disc", "USA", "Europe", "Japan"};
static const char *const SORT_NAMES[SORT_COUNT] = {"Title", "Recently played", "Most played",
                                                   "Region"};

const char *shelf_sort_name(int sort)
{
    return tr(SORT_NAMES[sort >= 0 && sort < SORT_COUNT ? sort : 0]);
}

/* 0 unknown, 1 USA, 2 Europe, 3 Japan */
static int region_of(const char *serial)
{
    if (!serial[0])
        return 0;
    if (!strncmp(serial, "SLUS", 4) || !strncmp(serial, "SCUS", 4) || !strncmp(serial, "PAPX", 4))
        return 1;
    if (!strncmp(serial, "SLES", 4) || !strncmp(serial, "SCES", 4) || !strncmp(serial, "SCED", 4))
        return 2;
    return 3;
}

const char *shelf_region_name(const char *serial)
{
    static const char *const names[] = {"Unknown region", "USA", "Europe", "Japan"};
    return tr(names[region_of(serial)]);
}

/* ---------------------------------------------------------------- the view */

static bool in_category(const Game *g, int category)
{
    GameStats *st = stats_get(g->id);
    switch (category)
    {
    case CAT_RECENT: return st && st->last_played > 0;
    case CAT_FAVORITES: return st && st->favorite;
    case CAT_MULTI_DISC: return g->discs > 1;
    case CAT_USA: return region_of(g->serial) == 1;
    case CAT_EUROPE: return region_of(g->serial) == 2;
    case CAT_JAPAN: return region_of(g->serial) == 3;
    default: return true;
    }
}

static int category_size(int category)
{
    int n = 0;
    for (int i = 0; i < app.library.count; ++i)
        n += in_category(&app.library.games[i], category);
    return n;
}

static int sort_mode;

static int compare(const void *pa, const void *pb)
{
    const Game *a = &app.library.games[*(const int *)pa], *b = &app.library.games[*(const int *)pb];
    GameStats *sa = stats_get(a->id), *sb = stats_get(b->id);
    switch (sort_mode)
    {
    case SORT_RECENT:
    {
        int64_t la = sa ? sa->last_played : 0, lb = sb ? sb->last_played : 0;
        if (la != lb)
            return la < lb ? 1 : -1;
        break;
    }
    case SORT_MOST_PLAYED:
    {
        uint32_t ta = sa ? sa->seconds : 0, tb = sb ? sb->seconds : 0;
        if (ta != tb)
            return ta < tb ? 1 : -1;
        break;
    }
    case SORT_REGION:
    {
        int ra = region_of(a->serial), rb = region_of(b->serial);
        if (ra != rb)
            return ra - rb;
        break;
    }
    default:
        break;
    }
    int t = str_icmp(a->title, b->title);
    return t ? t : (*(const int *)pa - *(const int *)pb);
}

static void build_view(int keep_game)
{
    Settings *g = &app.global;
    if (g->shelf_category != CAT_ALL && category_size(g->shelf_category) == 0)
        g->shelf_category = CAT_ALL;
    if (S.view_capacity < app.library.count)
    {
        int *grown = realloc(S.view, sizeof(int) * (size_t)(app.library.count + 16));
        if (!grown)
            return;
        S.view = grown;
        S.view_capacity = app.library.count + 16;
    }
    S.view_count = 0;
    for (int i = 0; i < app.library.count; ++i)
        if (in_category(&app.library.games[i], g->shelf_category))
            S.view[S.view_count++] = i;
    sort_mode = g->shelf_category == CAT_RECENT ? SORT_RECENT : g->sort_mode;
    qsort(S.view, (size_t)S.view_count, sizeof(int), compare);
    S.cursor = 0;
    for (int k = 0; k < S.view_count; ++k)
        if (S.view[k] == keep_game)
            S.cursor = k;
    S.pos = (float)S.cursor;
}

static int selected_game(void)
{
    return S.view_count ? S.view[S.cursor] : -1;
}

void shelf_init(int last_game)
{
    S.last_cursor_game = last_game;
    S.tint[0] = 0x3a / 255.0f;
    S.tint[1] = 0x50 / 255.0f;
    S.tint[2] = 0xc8 / 255.0f;
}

void shelf_library_changed(void)
{
    build_view(S.last_cursor_game);
}

void shelf_select_game(int library_index)
{
    S.last_cursor_game = library_index;
    build_view(library_index);
}

/* ---------------------------------------------------------------- backdrop */

/* A neutral vertical gradient, built once and blitted tinted each frame:
 * a full-screen gradient mesh per frame would be costly here. */
void shelf_backdrop(void)
{
    static PlatTexture *backdrop;
    static bool tried;
    if (!backdrop && !tried)
    {
        tried = true;
        enum { W = 1920, H = 1080 };
        uint8_t *px = malloc((size_t)W * H * 4);
        if (px)
        {
            for (int y = 0; y < H; ++y)
            {
                float t = (float)y / (H - 1);
                /* dark top, brighter band behind the covers, dark floor */
                float l = t < 0.42f ? 0.30f + t / 0.42f * 0.42f
                                    : t < 0.64f ? 0.72f - (t - 0.42f) / 0.22f * 0.30f
                                                : 0.42f - (t - 0.64f) / 0.36f * 0.30f;
                for (int x = 0; x < W; ++x)
                {
                    float dx = (x - W * 0.5f) / (W * 0.5f);
                    float v = l * (1.0f - dx * dx * 0.35f); /* a soft vignette */
                    uint8_t c = (uint8_t)(v * 255.0f);
                    uint8_t *p = &px[((size_t)y * W + x) * 4];
                    p[0] = p[1] = p[2] = c;
                    p[3] = 255;
                }
            }
            backdrop = plat_texture_create(px, W, H, true);
            free(px);
        }
    }
    if (!backdrop)
    {
        draw_rect(0, 0, plat_width(), plat_height(), TH_BG);
        return;
    }
    uint32_t tint = 0xff000000u | (uint32_t)(S.tint[0] * 255) << 16 |
                    (uint32_t)(S.tint[1] * 255) << 8 | (uint32_t)(S.tint[2] * 255);
    plat_draw_texture(backdrop, 0, 0, plat_width(), plat_height(), tint, false);
}

static void update_tint(int game)
{
    /* Dark: deep navy with a hint of the cover. Cover colour: the theme's blue
     * pulled most of the way towards the cover's own colour. */
    bool dark = app.global.background == 0;
    float target[3] = {0x3a / 255.0f, 0x50 / 255.0f, 0xc8 / 255.0f};
    if (dark)
    {
        target[0] = 0x1c / 255.0f;
        target[1] = 0x24 / 255.0f;
        target[2] = 0x5e / 255.0f;
    }
    uint32_t c = game >= 0 ? covers_color(game) : 0;
    if (c)
    {
        float cover[3] = {((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f};
        float m = fmaxf(cover[0], fmaxf(cover[1], cover[2]));
        float mix = dark ? 0.22f : 0.55f, level = dark ? 0.32f : 0.8f;
        for (int i = 0; i < 3; ++i)
        {
            float v = m > 0.05f ? cover[i] / m * level : level * 0.6f; /* keep it bright enough */
            target[i] = target[i] * (1.0f - mix) + v * mix;
        }
    }
    for (int i = 0; i < 3; ++i)
        anim_approach(&S.tint[i], target[i], app.dt, 4.0f);
}

/* ---------------------------------------------------------------- pieces */

static void draw_cover(PlatTexture *tex, const Game *g, float cx, float cy, float h, float squeeze,
                       uint32_t tint, bool selected)
{
    int tw = 1, th = 1;
    plat_texture_size(tex, &tw, &th);
    float aspect = tex ? (float)tw / th : 0.88f;
    float w = h * aspect * squeeze, x = cx - w * 0.5f, y = cy - h * 0.5f;
    if (selected)
    {
        draw_rrect(x - 10, y + 14, w + 20, h + 6, 18, 0x60000000u); /* shadow */
        draw_rrect_outline(x - 7, y - 7, w + 14, h + 14, 14, 4, 0xffe8ebffu);
    }
    if (tex)
        plat_draw_texture(tex, x, y, w, h, tint, true);
    else
    {
        draw_rrect(x, y, w, h, 10, TH_CARD);
        text_draw_fit(cx, cy - 16, 24, FONT_BOLD, TH_TEXT_DIM, ALIGN_CENTER, w - 24, g->title);
    }
}

static void draw_details(const Game *g, float t)
{
    if (t <= 0.0f)
        return;
    float ease = 1.0f - (1.0f - t) * (1.0f - t);
    float w = 620, x = plat_width() - (w + 48) * ease, y = 140, h = 800;
    draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(0xf2151a3du, t));
    text_draw_fit(x + 40, y + 34, 32, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_LEFT, w - 80, g->title);
    GameStats *st = stats_get(g->id);
    char played[48] = "", when[48] = "", ach[48] = "", discs[16];
    if (st)
    {
        stats_format_time(st->seconds, played, sizeof(played));
        stats_format_when(st->last_played, when, sizeof(when));
        if (st->ach_unlocked >= 0 && st->ach_total > 0)
            snprintf(ach, sizeof(ach), "%d / %d", st->ach_unlocked, st->ach_total);
    }
    snprintf(discs, sizeof(discs), "%d", g->discs);
    const char *folder = strrchr(g->folder, '/');
    struct { const char *label; const char *value; int icon; } rows[] = {
        {"Serial", g->serial[0] ? g->serial : tr("Unknown"), ICON_CARDS},
        {"Region", shelf_region_name(g->serial), ICON_WORLD},
        {"Discs", discs, ICON_DISC},
        {"Format", path_ext(g->path), ICON_FOLDER},
        {"Played", played[0] ? played : tr("Not yet"), ICON_CLOCK},
        {"Last played", when[0] ? when : tr("Never"), ICON_HISTORY},
        {"Achievements", ach[0] ? ach : tr("Unknown"), ICON_TROPHY},
        {"Folder", folder ? folder + 1 : g->folder, ICON_FOLDER},
    };
    for (int i = 0; i < 8; ++i)
    {
        float ry = y + 110 + i * 70;
        icon_draw(rows[i].icon, x + 40, ry + 2, 30, argb_alpha(TH_FOCUS, t));
        text_draw(x + 88, ry, 24, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t), ALIGN_LEFT, tr(rows[i].label));
        text_draw_fit(x + 310, ry, 24, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_LEFT, w - 350,
                      rows[i].value);
    }
    text_draw(x + 40, y + h - 60, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t), ALIGN_LEFT,
              tr("Saves and cheats follow the serial."));
}

/* Category chips along the top; the highlight slides between them. */
static void draw_header(void)
{
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, PSXS5_NAME);
    float x = TH_MARGIN + text_width(44, FONT_BOLD, PSXS5_NAME) + 40, y = 46, h = 46;
    for (int c = 0; c < CAT_COUNT; ++c)
    {
        int n = category_size(c);
        if (c != CAT_ALL && n == 0)
            continue;
        char label[64];
        if (c == CAT_ALL)
            snprintf(label, sizeof(label), "%s  %d", tr(CATEGORY_NAMES[c]), n);
        else
            str_copy(label, sizeof(label), tr(CATEGORY_NAMES[c]));
        float w = text_width(22, FONT_REGULAR, label) + 40;
        bool on = c == app.global.shelf_category;
        if (on)
        {
            if (S.chip_w == 0)
                S.chip_x = x, S.chip_w = w;
            anim_approach(&S.chip_x, x, app.dt, TH_SNAP);
            anim_approach(&S.chip_w, w, app.dt, TH_SNAP);
            draw_rrect(S.chip_x, y, S.chip_w, h, h * 0.5f, TH_TEXT);
        }
        text_draw(x + 20, y + 11, 22, FONT_REGULAR, on ? TH_BG : TH_HINT, ALIGN_LEFT, label);
        x += w + 6;
    }

    /* account and clock, top right */
    char clock_text[16] = "";
    time_t now = time(NULL);
    struct tm tm;
    if (local_time(now, &tm))
        snprintf(clock_text, sizeof(clock_text), "%02d:%02d", tm.tm_hour, tm.tm_min);
    float rx = plat_width() - TH_MARGIN;
    text_draw(rx, 53, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, clock_text);
    rx -= text_width(24, FONT_REGULAR, clock_text) + 32;
    if (update_state() == UPDATE_AVAILABLE || update_state() == UPDATE_INSTALLED)
    {
        char up[64];
        snprintf(up, sizeof(up), tr(update_state() == UPDATE_INSTALLED ? "Restart for %s" : "Update %s"),
                 update_version());
        float uw = text_width(20, FONT_BOLD, up) + 64;
        draw_rrect(rx - uw, 44, uw, 46, 23, 0xff2a2410u);
        icon_draw(ICON_DOWNLOAD, rx - uw + 14, 52, 30, TH_GOLD);
        text_draw(rx - uw + 50, 56, 20, FONT_BOLD, TH_GOLD, ALIGN_LEFT, up);
        rx -= uw + 24;
    }
    if (ra_user()[0])
    {
        char who[96];
        snprintf(who, sizeof(who), "%s  \xc2\xb7  %u", ra_user(), ra_user_score());
        text_draw(rx, 53, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, who);
        rx -= text_width(24, FONT_REGULAR, who) + 38;
        icon_draw(ICON_TROPHY, rx, 52, 30, TH_GOLD);
    }
}

static void draw_info(const Game *g, float alpha)
{
    GameStats *st = stats_get(g->id);
    float y = 730;
    float tw = text_width(52, FONT_BOLD, g->title);
    if (st && st->favorite)
        icon_draw(ICON_STAR, CENTER_X - fminf(tw, 1500) * 0.5f - 52, y + 10, 40,
                  argb_alpha(TH_GOLD, alpha));
    text_draw_fit(CENTER_X, y, 52, FONT_BOLD, argb_alpha(TH_TEXT, alpha), ALIGN_CENTER, 1500, g->title);

    /* tags: region, serial, discs, play time, last played */
    char discs[32], played[48] = "", played_tag[64] = "", when[48] = "";
    snprintf(discs, sizeof(discs), tr(g->discs == 1 ? "%d disc" : "%d discs"), g->discs);
    if (st)
    {
        stats_format_time(st->seconds, played, sizeof(played));
        if (played[0])
            snprintf(played_tag, sizeof(played_tag), tr("Played %s"), played);
        stats_format_when(st->last_played, when, sizeof(when));
    }
    const char *tags[5] = {shelf_region_name(g->serial), g->serial[0] ? g->serial : tr("No serial"),
                           discs, played_tag, when};
    float widths[5], total = 0;
    for (int i = 0; i < 5; ++i)
        if (tags[i][0])
        {
            widths[i] = text_width(22, FONT_REGULAR, tags[i]) + 36;
            total += widths[i] + 10;
        }
        else
            widths[i] = 0;
    float x = CENTER_X - (total - 10) * 0.5f, ty = y + 84;
    for (int i = 0; i < 5; ++i)
        if (widths[i] > 0)
            x += draw_pill(x, ty, 40, 22, argb_alpha(0xc01c2250u, alpha),
                           argb_alpha(TH_TEXT_SOFT, alpha), tags[i]) + 10;

    /* achievement progress, when known */
    if (st && st->ach_unlocked >= 0 && st->ach_total > 0)
    {
        float bw = 340, bx = CENTER_X - bw * 0.5f + 20, by = ty + 70;
        icon_draw(ICON_TROPHY, bx - 46, by - 12, 30, argb_alpha(TH_GOLD, alpha));
        draw_rrect(bx, by, bw, 8, 4, argb_alpha(0xff1c2250u, alpha));
        float f = (float)st->ach_unlocked / st->ach_total;
        draw_rrect(bx, by, bw * (f > 1 ? 1 : f), 8, 4, argb_alpha(TH_GOLD, alpha));
        char a[32];
        snprintf(a, sizeof(a), "%d / %d", st->ach_unlocked, st->ach_total);
        text_draw(bx + bw + 16, by - 13, 22, FONT_REGULAR, argb_alpha(TH_TEXT_SOFT, alpha), ALIGN_LEFT, a);
    }
}

/* ---------------------------------------------------------------- screen */

static void storage_screen(uint32_t pressed)
{
    shelf_backdrop();
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, PSXS5_NAME);
    float w = 1200, h = 300, x = CENTER_X - w * 0.5f, y = 330;
    draw_rrect(x, y, w, h, TH_RADIUS, TH_CARD_SOFT);
    icon_draw(ICON_ALERT_TRIANGLE, CENTER_X - 32, y + 40, 64, TH_GOLD);
    text_draw(CENTER_X, y + 124, 36, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("PSXS5 can't open /data/PSXS5"));
    text_draw_fit(CENTER_X, y + 190, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, w - 80,
                  app.storage_error);
    static const int glyphs[] = {GLYPH_SQUARE};
    static const char *const labels[] = {"Settings"};
    app_draw_hints(glyphs, labels, 1, NULL);
    if (pressed & BIT(BTN_SQUARE))
        app_open_settings(SCREEN_LIBRARY);
    app_draw_toast();
}

static void change_category(int step)
{
    int c = app.global.shelf_category;
    for (int i = 0; i < CAT_COUNT; ++i)
    {
        c = (c + step + CAT_COUNT) % CAT_COUNT;
        if (c == CAT_ALL || category_size(c) > 0)
            break;
    }
    if (c == app.global.shelf_category)
        return;
    int keep = selected_game();
    app.global.shelf_category = c;
    build_view(keep);
    S.title_fade = 0;
    sfx_play(SFX_CLICK);
}

void shelf_screen(uint32_t pressed)
{
    if (app.storage_error[0])
    {
        storage_screen(pressed);
        return;
    }

    /* ------------------------------------------------ input */
    if (S.dialog)
    {
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_UP) | BIT(BTN_DOWN)))
        {
            S.dialog_choice ^= 1;
            sfx_play(SFX_CLICK);
        }
        if (pressed & BIT(BTN_CROSS))
        {
            S.dialog = false;
            S.resume = S.dialog_choice == 0;
            S.launch_t = 0.0001f;
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_CIRCLE))
        {
            S.dialog = false;
            sfx_play(SFX_BACK);
        }
        pressed = 0;
    }
    if (S.launch_t <= 0.0f)
    {
        int before = S.cursor;
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_UP)))
            --S.cursor;
        if (pressed & (BIT(BTN_RIGHT) | BIT(BTN_DOWN)))
            ++S.cursor;
        if (pressed & BIT(BTN_L2))
            S.cursor -= 8;
        if (pressed & BIT(BTN_R2))
            S.cursor += 8;
        S.cursor = S.cursor < 0 ? 0 : S.cursor >= S.view_count ? (S.view_count ? S.view_count - 1 : 0)
                                                                : S.cursor;
        if (S.cursor != before)
        {
            sfx_play(SFX_CLICK);
            S.title_fade = 0.35f;
        }
        if (pressed & BIT(BTN_L1))
            change_category(-1);
        if (pressed & BIT(BTN_R1))
            change_category(1);
        if (pressed & BIT(BTN_START))
        {
            int keep = selected_game();
            app.global.sort_mode = (app.global.sort_mode + 1) % SORT_COUNT;
            build_view(keep);
            char msg[96];
            snprintf(msg, sizeof(msg), tr("Sorted by %s"), shelf_sort_name(app.global.sort_mode));
            app_toast(msg);
            sfx_play(SFX_CLICK);
        }
        int game = selected_game();
        if ((pressed & BIT(BTN_R3)) && game >= 0)
        {
            GameStats *st = stats_get(app.library.games[game].id);
            if (st)
            {
                st->favorite = !st->favorite;
                stats_save();
                app_toast(st->favorite ? "Added to favorites" : "Removed from favorites");
                sfx_play(SFX_SELECT);
                if (app.global.shelf_category == CAT_FAVORITES)
                    build_view(game);
            }
        }
        if (pressed & BIT(BTN_TRIANGLE) && game >= 0)
        {
            S.details = !S.details;
            sfx_play(S.details ? SFX_SELECT : SFX_BACK);
        }
        if ((pressed & BIT(BTN_CIRCLE)) && S.details)
        {
            S.details = false;
            sfx_play(SFX_BACK);
        }
        if ((pressed & BIT(BTN_CROSS)) && game >= 0)
        {
            const Game *g = &app.library.games[game];
            S.details = false;
            if (app.global.quick_resume && !ra_hardcore() && play_has_resume(g, &S.resume_age))
            {
                /* offer to continue */
                char path[PSXS5_PATH_MAX];
                static uint8_t rgba[THUMB_W * THUMB_H * 4];
                play_resume_path(g, path, sizeof(path));
                plat_texture_free(S.resume_thumb);
                S.resume_thumb = play_load_thumb(path, rgba)
                                     ? plat_texture_create(rgba, THUMB_W, THUMB_H, true)
                                     : NULL;
                S.dialog = true;
                S.dialog_choice = 0;
            }
            else
            {
                S.resume = false;
                S.launch_t = 0.0001f;
            }
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_SQUARE))
        {
            sfx_play(SFX_SELECT);
            config_save(&app.global, app.paths.config);
            app_open_settings(SCREEN_LIBRARY);
        }
    }

    /* ------------------------------------------------ animation */
    anim_approach(&S.pos, (float)S.cursor, app.dt, 16.0f);
    S.details_t = fminf(fmaxf(S.details_t + (S.details ? app.dt : -app.dt) * 8.0f, 0.0f), 1.0f);
    if (S.title_fade > 0)
        S.title_fade = fmaxf(S.title_fade - app.dt, 0.0f);
    int game = selected_game();
    if (game >= 0)
        S.last_cursor_game = game;
    if (S.launch_t > 0.0f)
    {
        S.launch_t += app.dt;
        if (S.launch_t >= LAUNCH_TIME)
        {
            S.launch_t = 0.0f;
            config_save(&app.global, app.paths.config);
            app_start_game(game, S.resume);
        }
    }
    covers_update_view(S.view, S.view_count, S.cursor);
    update_tint(game);

    /* ------------------------------------------------ shelf */
    shelf_backdrop();
    float launch = S.launch_t > 0.0f ? S.launch_t / LAUNCH_TIME : 0.0f;
    if (S.view_count > 0)
    {
        int first = (int)floorf(S.pos) - 6, last = (int)ceilf(S.pos) + 6;
        /* far to near, so the selected cover is drawn last */
        for (int ring = 6; ring >= 0; --ring)
            for (int side = -1; side <= 1; side += 2)
            {
                int k = (int)floorf(S.pos + 0.5f) + ring * side;
                if (ring == 0 && side == 1)
                    continue;
                if (k < first || k > last || k < 0 || k >= S.view_count)
                    continue;
                float d = k - S.pos, ad = fabsf(d), near = ad < 1.0f ? ad : 1.0f;
                float ox = ad < 1.0f ? d * SIDE_GAP
                                     : (d > 0 ? 1.0f : -1.0f) * (SIDE_GAP + (ad - 1.0f) * STACK_GAP);
                float scale = 1.0f - near * (1.0f - SIDE_SCALE);
                float squeeze = 1.0f - near * (1.0f - SIDE_SQUEEZE);
                bool selected = k == S.cursor;
                if (selected)
                    scale *= 1.0f + launch * 0.12f;
                float fade = ad > 4.0f ? fmaxf(0.0f, 1.0f - (ad - 4.0f) / 2.0f) : 1.0f;
                float shade = (1.0f - near * 0.4f) * fade * (selected ? 1.0f : 1.0f - launch);
                uint8_t s8 = (uint8_t)(255 * shade);
                uint32_t tint = 0xff000000u | (uint32_t)s8 << 16 | (uint32_t)s8 << 8 | s8;
                int index = S.view[k];
                draw_cover(covers_get(index), &app.library.games[index], CENTER_X + ox, CENTER_Y,
                           COVER_H * scale, squeeze, tint, selected && ad < 0.25f && launch < 0.5f);
            }
        float text_a = (1.0f - launch) * (1.0f - S.title_fade / 0.35f * 0.8f);
        draw_info(&app.library.games[game], text_a);
        draw_details(&app.library.games[game], S.details_t);
    }
    else
    {
        float w = 1200, h = 330, x = CENTER_X - w * 0.5f, y = 300;
        draw_rrect(x, y, w, h, TH_RADIUS, TH_CARD_SOFT);
        icon_draw(ICON_DISC, CENTER_X - 32, y + 30, 64, TH_FOCUS);
        text_draw(CENTER_X, y + 108, 36, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("Your shelf is empty"));
        char line[PSXS5_PATH_MAX + 64];
        snprintf(line, sizeof(line), tr("Games: %s/<Game name>/"), app.paths.games);
        text_draw(CENTER_X, y + 166, 24, FONT_REGULAR, TH_TEXT, ALIGN_CENTER, line);
        snprintf(line, sizeof(line), tr("BIOS (optional): %s/"), app.paths.bios);
        text_draw(CENTER_X, y + 206, 24, FONT_REGULAR, TH_TEXT, ALIGN_CENTER, line);
        text_draw(CENTER_X, y + 262, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  tr("On your PC:  python tools/psxs5_sync.py upload --host <PS5 IP>"));
    }

    draw_header();
    int pending = covers_downloading();
    if (pending > 0)
    {
        char dl[64];
        snprintf(dl, sizeof(dl), tr("Getting covers (%d)"), pending);
        text_draw(plat_width() - TH_MARGIN, 104, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_RIGHT, dl);
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_R3};
    static const char *const labels[] = {"Play", "Details", "Settings", "Favorite"};
    char right[128];
    snprintf(right, sizeof(right), "%s   \xc2\xb7   %s: %s", tr("L1 / R1  Category"), tr("OPTIONS  Sort"),
             shelf_sort_name(app.global.sort_mode));
    app_draw_hints(glyphs, labels, S.view_count ? 4 : 1, S.view_count ? right : NULL);

    /* the continue dialog */
    S.dialog_t = fminf(fmaxf(S.dialog_t + (S.dialog ? app.dt : -app.dt) * 8.0f, 0.0f), 1.0f);
    if (S.dialog_t > 0.0f && game >= 0)
    {
        float t = S.dialog_t, e = 1.0f - (1.0f - t) * (1.0f - t);
        draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(0xc0000000u, t));
        const float w = 1000, h = 420, x = CENTER_X - w * 0.5f, y = 330 + (1.0f - e) * 40;
        draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(0xff151a3du, t));
        text_draw_fit(x + 40, y + 30, 32, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_LEFT, w - 80,
                      app.library.games[game].title);
        const char *labels[2] = {tr("Continue"), tr("Start over")};
        char age[64], ago[48];
        long a = S.resume_age;
        if (a < 120)
            str_copy(ago, sizeof(ago), "");
        else if (a < 3600)
            snprintf(ago, sizeof(ago), tr("%ld min ago"), a / 60);
        else if (a < 86400)
            snprintf(ago, sizeof(ago), tr("%ld h ago"), a / 3600);
        else
            snprintf(ago, sizeof(ago), tr("%ld days ago"), a / 86400);
        if (ago[0])
            snprintf(age, sizeof(age), tr("Saved %s"), ago);
        else
            str_copy(age, sizeof(age), tr("Saved just now"));
        for (int i = 0; i < 2; ++i)
        {
            float bx = x + 40 + i * (w - 80) * 0.5f, bw = (w - 80) * 0.5f - 12, by = y + 100, bh = 280;
            bool on = i == S.dialog_choice;
            draw_rrect(bx, by, bw, bh, TH_RADIUS_SMALL, argb_alpha(on ? TH_ROW_SELECTED : 0xff1c2250u, t));
            if (on)
                draw_rrect_outline(bx, by, bw, bh, TH_RADIUS_SMALL, 3, argb_alpha(TH_FOCUS, t));
            if (i == 0 && S.resume_thumb)
                plat_draw_texture(S.resume_thumb, bx + (bw - 256) * 0.5f, by + 24, 256, 192,
                                  argb_alpha(0xffffffffu, t), false);
            else
                icon_draw(i == 0 ? ICON_PLAYER_PLAY : ICON_REFRESH, bx + bw * 0.5f - 48, by + 70, 96,
                          argb_alpha(TH_FOCUS, t));
            text_draw(bx + bw * 0.5f, by + 226, 28, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_CENTER,
                      labels[i]);
            if (i == 0)
                text_draw(bx + bw * 0.5f, by + bh + 14, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t),
                          ALIGN_CENTER, age);
        }
    }

    if (S.launch_t > 0.0f)
        draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(0xff000000u, launch));
    app_draw_toast();
}
