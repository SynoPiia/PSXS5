/*
 * PSXS5 - the in-game menu (a side panel over the paused game) and cheats.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../core/host.h"
#include "../covers.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../ra/achievements.h"
#include "coverflow.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

enum Item
{
    MI_RESUME,
    MI_SAVE,
    MI_LOAD,
    MI_DISC,
    MI_CHEATS,
    MI_SETTINGS,
    MI_RESET,
    MI_QUIT,
    MI_COUNT
};

static struct
{
    int cursor;
    float sel_y, open_t, slot_x;
    int disc_choice;
    int cheat_cursor;
    float cheat_y, cheat_scroll;
} M;

void menu_open(void)
{
    M.cursor = MI_RESUME;
    M.open_t = 0;
    M.sel_y = 0;
    M.disc_choice = host_disc_index();
    app.screen = SCREEN_MENU;
    sfx_play(SFX_SELECT);
}

static bool item_shown(int i)
{
    return i != MI_DISC || host_disc_count() > 1;
}

/* "2 min ago", "Yesterday"...; "" when the slot is empty */
static void slot_age(int slot, char *out, size_t size)
{
    char path[PSXS5_PATH_MAX];
    app_state_path(path, sizeof(path), slot);
    struct stat st;
    out[0] = '\0';
    if (stat(path, &st) != 0)
        return;
    long ago = (long)(time(NULL) - st.st_mtime);
    if (ago < 60)
        str_copy(out, size, tr("Just now"));
    else if (ago < 3600)
        snprintf(out, size, tr("%ld min ago"), ago / 60);
    else if (ago < 86400)
        snprintf(out, size, tr("%ld h ago"), ago / 3600);
    else if (ago < 172800)
        str_copy(out, size, tr("Yesterday"));
    else
        snprintf(out, size, tr("%ld days ago"), ago / 86400);
}

static void draw_slots(float x, float y, float w, bool active)
{
    const int visible = 5;
    const float gap = 14, cw = (w - gap * (visible - 1)) / visible, ch = cw * 0.75f;
    int slot = app.settings.state_slot;
    float target = (float)slot;
    anim_approach(&M.slot_x, target, app.dt, TH_SNAP);
    text_draw(x, y - 40, 22, FONT_BOLD, TH_TEXT_DIM, ALIGN_LEFT, tr("State slots"));
    if (active)
        text_draw(x + w, y - 40, 22, FONT_REGULAR, TH_HINT, ALIGN_RIGHT, tr("Left / Right  Slot"));
    plat_set_clip((int)x - 6, (int)y - 6, (int)w + 12, (int)ch + 12);
    float start = x + w * 0.5f - cw * 0.5f - M.slot_x * (cw + gap);
    for (int s = 0; s < 10; ++s)
    {
        float cx = start + s * (cw + gap);
        if (cx + cw < x - 10 || cx > x + w + 10)
            continue;
        char age[48];
        slot_age(s, age, sizeof(age));
        bool on = s == slot;
        draw_rrect(cx, y, cw, ch, TH_RADIUS_SMALL, on ? 0xff24305cu : age[0] ? 0xff1a2147u : 0xff141938u);
        if (on)
            draw_rrect_outline(cx, y, cw, ch, TH_RADIUS_SMALL, 3, active ? TH_FOCUS : 0xff4a5590u);
        char name[32];
        snprintf(name, sizeof(name), tr("Slot %d"), s);
        text_draw(cx + 16, y + 14, 22, FONT_BOLD, TH_TEXT, ALIGN_LEFT, name);
        text_draw_fit(cx + 16, y + ch - 40, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, cw - 24,
                      age[0] ? age : tr("Empty"));
        icon_draw(age[0] ? ICON_DEVICE_FLOPPY : ICON_X, cx + cw - 44, y + 12, 28,
                  age[0] ? TH_FOCUS : 0xff3a4280u);
    }
    plat_set_clip(0, 0, 0, 0);
}

void menu_screen(uint32_t pressed)
{
    /* ------------------------------------------------ input */
    int before = M.cursor;
    int step = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
    if (step)
    {
        do
            M.cursor = (M.cursor + step + MI_COUNT) % MI_COUNT;
        while (!item_shown(M.cursor));
    }
    if (M.cursor != before)
        sfx_play(SFX_CLICK);
    bool on_slots = M.cursor == MI_SAVE || M.cursor == MI_LOAD;
    bool left = pressed & BIT(BTN_LEFT), right = pressed & BIT(BTN_RIGHT);
    if (on_slots && (left || right))
    {
        app.settings.state_slot = (app.settings.state_slot + (right ? 1 : 9)) % 10;
        app.global.state_slot = app.settings.state_slot;
        sfx_play(SFX_CLICK);
    }
    int discs = host_disc_count();
    if (M.cursor == MI_DISC && discs > 1 && (left || right))
    {
        M.disc_choice = (M.disc_choice + (right ? 1 : discs - 1)) % discs;
        sfx_play(SFX_CLICK);
    }
    if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_MENU)))
    {
        sfx_play(SFX_BACK);
        app.screen = SCREEN_GAME;
        return;
    }
    if (pressed & BIT(BTN_CROSS))
    {
        char msg[96], st[PSXS5_PATH_MAX];
        app_state_path(st, sizeof(st), app.settings.state_slot);
        switch (M.cursor)
        {
        case MI_RESUME:
            app.screen = SCREEN_GAME;
            return;
        case MI_SAVE:
            make_dirs(app.paths.states);
            if (host_save_state(st))
            {
                snprintf(msg, sizeof(msg), tr("Saved to slot %d"), app.settings.state_slot);
                app.screen = SCREEN_GAME;
            }
            else
                snprintf(msg, sizeof(msg), tr("Couldn't save to slot %d"), app.settings.state_slot);
            app_toast(msg);
            sfx_play(SFX_SELECT);
            return;
        case MI_LOAD:
            if (ra_hardcore())
                str_copy(msg, sizeof(msg), tr("Not allowed in hardcore mode"));
            else if (host_load_state(st))
            {
                snprintf(msg, sizeof(msg), tr("Loaded slot %d"), app.settings.state_slot);
                app.screen = SCREEN_GAME;
            }
            else
                snprintf(msg, sizeof(msg), tr("Slot %d is empty"), app.settings.state_slot);
            app_toast(msg);
            sfx_play(SFX_SELECT);
            return;
        case MI_DISC:
            if (host_disc_select(M.disc_choice))
            {
                snprintf(msg, sizeof(msg), tr("Disc %d inserted"), M.disc_choice + 1);
                app.screen = SCREEN_GAME;
            }
            else
                str_copy(msg, sizeof(msg), tr("Disc change failed"));
            app_toast(msg);
            return;
        case MI_CHEATS:
            if (ra_hardcore())
            {
                app_toast("Cheats are off in hardcore mode");
                return;
            }
            M.cheat_cursor = 0;
            M.cheat_scroll = 0;
            M.cheat_y = 0;
            app.screen = SCREEN_CHEATS;
            sfx_play(SFX_SELECT);
            return;
        case MI_SETTINGS:
            sfx_play(SFX_SELECT);
            app_open_settings(SCREEN_MENU);
            return;
        case MI_RESET:
            host_reset();
            ra_reset();
            app_toast("Console reset");
            app.screen = SCREEN_GAME;
            return;
        case MI_QUIT:
            app_save_settings();
            sfx_play(SFX_BACK);
            app_stop_game();
            shelf_select_game(app.global.last_game);
            return;
        default:
            break;
        }
    }

    /* ------------------------------------------------ draw */
    M.open_t = fminf(M.open_t + app.dt * 7.0f, 1.0f);
    float ease = 1.0f - (1.0f - M.open_t) * (1.0f - M.open_t);
    app_draw_game((uint8_t)(255 - 150 * ease));

    const float pw = 620;
    float px = -pw * (1.0f - ease);
    draw_rect(px, 0, pw, plat_height(), 0xf00f1330u);
    draw_rect(px + pw, 0, 3, plat_height(), argb_alpha(TH_DIVIDER, ease));

    /* the game: cover, title, serial, achievements */
    const Game *g = app.game;
    PlatTexture *cover = g ? covers_get(app.game_index) : NULL;
    float cx = px + 48, cy = 56;
    if (cover)
    {
        int tw = 1, th = 1;
        plat_texture_size(cover, &tw, &th);
        float h = 150, w = h * tw / th;
        plat_draw_texture(cover, cx, cy, w, h, 0xffffffffu, true);
        cx += w + 28;
    }
    if (g)
    {
        text_draw_fit(cx, cy + 20, 32, FONT_BOLD, TH_TEXT, ALIGN_LEFT, px + pw - cx - 40, g->title);
        text_draw(cx, cy + 70, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT,
                  g->serial[0] ? g->serial : tr("No serial"));
        int unlocked, total;
        if (ra_game_progress(&unlocked, &total))
        {
            char a[64];
            snprintf(a, sizeof(a), tr("%d of %d achievements"), unlocked, total);
            icon_draw(ICON_TROPHY, cx, cy + 108, 26, TH_GOLD);
            text_draw(cx + 36, cy + 108, 22, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT, a);
        }
    }

    static const struct
    {
        const char *name;
        int icon;
    } ITEMS[MI_COUNT] = {
        {"Resume", ICON_PLAYER_PLAY},    {"Save state", ICON_DEVICE_FLOPPY},
        {"Load state", ICON_HISTORY},    {"Disc", ICON_DISC},
        {"Cheats", ICON_CODE},           {"Settings", ICON_ADJUSTMENTS},
        {"Reset", ICON_REFRESH},         {"Quit to shelf", ICON_DOOR_EXIT},
    };
    const float row_h = 72, top = 270, x = px + 32, w = pw - 64;
    float y = top, sel_target = top;
    for (int i = 0; i < MI_COUNT; ++i)
    {
        if (!item_shown(i))
            continue;
        if (i == M.cursor)
            sel_target = y;
        y += row_h;
    }
    if (M.sel_y == 0)
        M.sel_y = sel_target;
    anim_approach(&M.sel_y, sel_target, app.dt, TH_SNAP);
    draw_rrect(x, M.sel_y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
    draw_rrect_outline(x, M.sel_y, w, row_h - 8, TH_RADIUS_SMALL, 3, TH_FOCUS);
    y = top;
    for (int i = 0; i < MI_COUNT; ++i)
    {
        if (!item_shown(i))
            continue;
        uint32_t c = i == MI_QUIT ? TH_DANGER : TH_TEXT;
        icon_draw(ITEMS[i].icon, x + 22, y + 16, 32, i == MI_QUIT ? TH_DANGER : TH_FOCUS);
        text_draw(x + 74, y + 17, 28, FONT_REGULAR, c, ALIGN_LEFT, tr(ITEMS[i].name));
        char value[64] = "";
        if (i == MI_DISC)
        {
            snprintf(value, sizeof(value), tr("%d of %d"), M.disc_choice + 1, discs);
            draw_choice(x + w - 16, y + 12, 42, 22, TH_PILL, TH_TEXT_SOFT, value);
            value[0] = '\0';
        }
        else if (i == MI_CHEATS)
        {
            int on = 0;
            for (int k = 0; k < app.cheats.count; ++k)
                on += app.cheats.items[k].enabled;
            if (ra_hardcore())
                str_copy(value, sizeof(value), tr("hardcore"));
            else if (app.cheats.count)
                snprintf(value, sizeof(value), tr("%d on"), on);
        }
        else if (i == MI_LOAD && ra_hardcore())
            str_copy(value, sizeof(value), tr("hardcore"));
        if (value[0])
            text_draw(x + w - 24, y + 19, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_RIGHT, value);
        y += row_h;
    }

    /* save slots, along the bottom right */
    float sx = px + pw + 80, sw = plat_width() - sx - TH_MARGIN;
    if (sw > 400)
        draw_slots(sx, 780, sw, on_slots);

    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
    static const char *const labels[] = {"Select", "Resume"};
    app_draw_hints(glyphs, labels, 2, NULL);
    app_draw_toast();
}

/* ---------------------------------------------------------------- cheats */

void cheats_screen(uint32_t pressed)
{
    CheatList *cl = &app.cheats;
    const int rows = 10;
    const float row_h = 70, top = 180, x = 300, w = plat_width() - 600.0f;
    if (cl->count > 0)
    {
        int before = M.cheat_cursor;
        if (pressed & BIT(BTN_UP))
            M.cheat_cursor = (M.cheat_cursor + cl->count - 1) % cl->count;
        if (pressed & BIT(BTN_DOWN))
            M.cheat_cursor = (M.cheat_cursor + 1) % cl->count;
        if (pressed & BIT(BTN_L1))
            M.cheat_cursor = M.cheat_cursor > rows ? M.cheat_cursor - rows : 0;
        if (pressed & BIT(BTN_R1))
            M.cheat_cursor = M.cheat_cursor + rows < cl->count ? M.cheat_cursor + rows : cl->count - 1;
        if (M.cheat_cursor != before)
            sfx_play(SFX_CLICK);
        if (pressed & BIT(BTN_CROSS))
        {
            cl->items[M.cheat_cursor].enabled = !cl->items[M.cheat_cursor].enabled;
            cheats_apply(cl);
            cheats_save_selection(cl);
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_SQUARE))
        {
            for (int i = 0; i < cl->count; ++i)
                cl->items[i].enabled = false;
            cheats_apply(cl);
            cheats_save_selection(cl);
            app_toast("All cheats off");
        }
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        sfx_play(SFX_BACK);
        app.screen = SCREEN_MENU;
        return;
    }

    app_draw_game(40);
    draw_rect(0, 0, plat_width(), plat_height(), 0x900a0d24u);
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Cheats"));
    const char *src = strrchr(cl->source, '/');
    if (cl->count)
        text_draw_fit(TH_MARGIN + 2, 100, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, 1400,
                      src ? src + 1 : cl->source);
    if (cl->count == 0)
    {
        float bw = 1300, bx = (plat_width() - bw) * 0.5f, by = 360;
        draw_rrect(bx, by, bw, 260, TH_RADIUS, TH_CARD);
        icon_draw(ICON_CODE, plat_width() * 0.5f - 32, by + 32, 64, TH_FOCUS);
        text_draw(plat_width() * 0.5f, by + 112, 34, FONT_BOLD, TH_TEXT, ALIGN_CENTER,
                  tr("No cheats for this game"));
        char where[PSXS5_PATH_MAX + 64];
        snprintf(where, sizeof(where), tr("Put .cht files in %s, or one next to the game."), app.paths.cheats);
        text_draw_fit(plat_width() * 0.5f, by + 168, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, bw - 80, where);
        text_draw(plat_width() * 0.5f, by + 206, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  tr("tools/psxs5_sync.py cheats installs the libretro cheat library."));
    }
    else
    {
        float want = M.cheat_scroll;
        if (M.cheat_cursor < want)
            want = (float)M.cheat_cursor;
        if (M.cheat_cursor >= want + rows)
            want = (float)(M.cheat_cursor - rows + 1);
        anim_approach(&M.cheat_scroll, want, app.dt, TH_SNAP);
        draw_rrect(x - 16, top - 16, w + 32, rows * row_h + 24, TH_RADIUS, TH_CARD);
        plat_set_clip((int)x - 8, (int)top - 8, (int)w + 16, (int)(rows * row_h) + 8);
        float target = top + (M.cheat_cursor - M.cheat_scroll) * row_h;
        if (M.cheat_y == 0)
            M.cheat_y = target;
        anim_approach(&M.cheat_y, target, app.dt, TH_SNAP * 1.5f);
        draw_rrect(x, M.cheat_y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rrect_outline(x, M.cheat_y, w, row_h - 8, TH_RADIUS_SMALL, 3, TH_FOCUS);
        int first = (int)floorf(M.cheat_scroll);
        for (int i = first; i < cl->count && i <= first + rows; ++i)
        {
            const Cheat *c = &cl->items[i];
            float y = top + (i - M.cheat_scroll) * row_h;
            draw_switch(x + 24, y + 15, 34, c->enabled ? 1.0f : 0.0f);
            text_draw_fit(x + 130, y + 17, 26, FONT_REGULAR, TH_TEXT, ALIGN_LEFT, w - 160, c->desc);
        }
        plat_set_clip(0, 0, 0, 0);
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Toggle", "All off", "Back"};
    app_draw_hints(glyphs, labels, 3, cl->count ? "L1 / R1  Page" : NULL);
    app_draw_toast();
}
