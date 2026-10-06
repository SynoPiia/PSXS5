/*
 * PSXS5 - PlayStation X Super 5
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Entry point and screens: coverflow library, in-game menu, settings, cheats.
 * Emulation is PCSX-ReARMed, linked statically and driven through libretro.
 */
#include "cheats.h"
#include "config.h"
#include "core/host.h"
#include "covers.h"
#include "library.h"
#include "platform/platform.h"
#include "psxs5.h"
#include "ui/coverflow.h"
#include "ui/draw.h"
#include "ui/sfx.h"
#include "ui/text.h"
#include "platform/ps5_crash.h"
#include "ra/achievements.h"
#include "i18n.h"
#include "platform/vk/vk_probe.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- look */

#define COL_TEXT 0xffffffffu
#define COL_DIM 0xffc9d2ffu
#define COL_PANEL 0xd00d1440u
#define COL_ROW 0x40ffffffu
#define COL_SELECT 0xff3d6cffu
#define COL_GOOD 0xff5fd38au
#define UI_RATE 44100

enum Screen
{
    SCREEN_LIBRARY,
    SCREEN_GAME,
    SCREEN_MENU,
    SCREEN_SETTINGS,
    SCREEN_CHEATS,
};

static Paths paths;
static Settings settings;
static Library library;
static CheatList cheats;
static Coverflow shelf;
static enum Screen screen = SCREEN_LIBRARY;
static enum Screen settings_return = SCREEN_LIBRARY;
static int menu_cursor, settings_cursor, settings_scroll, cheat_cursor, cheat_scroll;
static const Game *current_game;
static char storage_error[256];
static bool sandboxed;          /* /data folders can't be listed: use library.txt */
static bool unlock_setting = true; /* Settings > System > Unlock /data with etaHEN */
static char sandbox_reason[200];
static char toast[128];
static uint64_t toast_until;
static float fps_measured;
static float frame_dt = 1.0f / 60.0f;
static int covers_style_loaded = -1;
static bool covers_download_loaded;

static void show_toast(const char *msg)
{
    str_copy(toast, sizeof(toast), tr(msg));
    toast_until = plat_ticks_us() + 2500000;
    psxs5_log("%s", msg);
}

/* ---------------------------------------------------------------- input edges */

/* Button presses for menus, with auto-repeat on directions. */
static uint32_t nav_pressed(const PadState *pads)
{
    static uint32_t previous;
    static uint64_t repeat_at;
    uint32_t held = 0;
    for (int i = 0; i < PSXS5_MAX_PADS; ++i)
    {
        held |= pads[i].buttons;
        if (pads[i].ly < -20000)
            held |= BIT(BTN_UP);
        if (pads[i].ly > 20000)
            held |= BIT(BTN_DOWN);
        if (pads[i].lx < -20000)
            held |= BIT(BTN_LEFT);
        if (pads[i].lx > 20000)
            held |= BIT(BTN_RIGHT);
    }
    uint32_t pressed = held & ~previous;
    const uint32_t dirs = BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT) |
                          BIT(BTN_L1) | BIT(BTN_R1);
    uint64_t now = plat_ticks_us();
    if (pressed & dirs)
        repeat_at = now + 380000;
    else if ((held & dirs) && now >= repeat_at)
    {
        pressed |= held & dirs;
        repeat_at = now + 85000;
    }
    previous = held;
    return pressed;
}

/* ---------------------------------------------------------------- helpers */

static void state_path(char *out, size_t size, const Game *g, int slot)
{
    char file[96];
    snprintf(file, sizeof(file), "%.80s.state%d", g ? g->id : "game", slot);
    path_join(out, size, paths.states, file);
}

static void describe_bios(char *out, size_t size)
{
    if (settings.force_hle)
    {
        str_copy(out, size, tr("Using the built-in HLE BIOS"));
        return;
    }
    DIR *d = opendir(paths.bios);
    out[0] = '\0';
    if (d)
    {
        struct dirent *e;
        while ((e = readdir(d)))
            if (str_icmp(path_ext(e->d_name), "bin") == 0)
            {
                snprintf(out, size, tr("Real BIOS found: %.60s"), e->d_name);
                break;
            }
        closedir(d);
    }
    if (!out[0])
        str_copy(out, size, tr("No BIOS file in bios/, using the built-in HLE BIOS"));
}

static void restart_covers(void)
{
    covers_start(&library, &paths, &settings);
    covers_style_loaded = settings.cover_style;
    covers_download_loaded = settings.cover_download;
}

static void rescan(void)
{
    const char *roots[] = {paths.games, "/mnt/usb0/PSXS5", "/mnt/usb1/PSXS5",
                           "/mnt/ext0/PSXS5", "/mnt/ext1/PSXS5"};
    char index[PSXS5_PATH_MAX];
    path_join(index, sizeof(index), paths.root, "library.txt");
    if (sandboxed)
    {
        /* Can't list folders: the sync tool's index is the library. */
        if (!library_load_index(&library, index))
            library.count = 0;
    }
    else
    {
        library_scan(&library, roots, sizeof(roots) / sizeof(roots[0]));
        if (library.count == 0)
            library_load_index(&library, index);
    }
    if (shelf.cursor >= library.count)
        coverflow_init(&shelf, library.count ? library.count - 1 : 0);
    restart_covers();
}

static void draw_toast(void)
{
    if (!toast[0] || plat_ticks_us() > toast_until)
        return;
    float w = text_width(26, FONT_REGULAR, toast) + 64;
    float x = (plat_width() - w) * 0.5f, y = plat_height() - 200;
    draw_rect(x, y, w, 64, 0xe00d1440u);
    draw_rect(x, y, 6, 64, 0xff8fb0ffu);
    text_draw(x + 34, y + 17, 26, FONT_REGULAR, COL_TEXT, ALIGN_LEFT, toast);
}

static void draw_title(const char *title, const char *subtitle)
{
    text_draw(64, 44, 44, FONT_BOLD, COL_TEXT, ALIGN_LEFT, tr(title));
    if (subtitle)
        text_draw_fit(66, 102, 22, FONT_REGULAR, COL_DIM, ALIGN_LEFT, 1200, subtitle);
}

/* RetroAchievements banner: top right, one message at a time */
static void draw_achievement(void)
{
    static char title[96], detail[192];
    static uint64_t until;
    uint64_t now = plat_ticks_us();
    if (now > until)
    {
        if (!ra_next_message(title, sizeof(title), detail, sizeof(detail)))
            return;
        until = now + 4500000;
        sfx_play(SFX_SELECT);
    }
    const float w = 640, h = 112, x = plat_width() - w - 40, y = 40;
    draw_rect(x, y, w, h, 0xe0181c28u);
    draw_rect(x, y, 8, h, 0xffffc94au);
    text_draw_fit(x + 32, y + 18, 28, FONT_BOLD, COL_TEXT, ALIGN_LEFT, w - 56, title);
    text_draw_fit(x + 32, y + 62, 22, FONT_REGULAR, COL_DIM, ALIGN_LEFT, w - 56, detail);
}

static void draw_hints3(enum PadGlyph g1, const char *l1, enum PadGlyph g2, const char *l2,
                        const char *extra)
{
    float x = 64, y = 1008;
    x += draw_hint(x, y, g1, l1, 26, 0xffdfe5ffu);
    if (l2)
        x += draw_hint(x, y, g2, l2, 26, 0xffdfe5ffu);
    if (extra)
        text_draw(x, y, 26, FONT_REGULAR, 0xffdfe5ffu, ALIGN_LEFT, tr(extra));
}

/* ---------------------------------------------------------------- game start/stop */

static void draw_game_frame(uint8_t dim)
{
    int w, h, fmt;
    size_t pitch;
    bool fresh;
    const void *pixels = host_frame(&w, &h, &pitch, &fmt, &fresh);
    if (pixels && fresh)
        plat_upload_game(pixels, w, h, pitch, fmt, settings.upscale, settings.upscale_filter);
    plat_draw_game(&settings, host_aspect(), dim);
}

static void start_game(int index)
{
    if (index < 0 || index >= library.count)
        return;
    const Game *g = &library.games[index];
    char error[160];
    psxs5_log("start: %s (%s) from %s", g->title, g->serial, g->path);
    if (!host_load(g->path, &paths, &settings, error, sizeof(error)))
    {
        show_toast(error);
        return;
    }
    current_game = g;
    settings.last_game = index;
    config_save(&settings, paths.config);
    plat_audio_open(host_sample_rate());
    plat_audio_clear();
    ra_game_loaded();
    if (ra_hardcore())
        cheats_clear(&cheats); /* hardcore: no cheats */
    else if (cheats_load(&cheats, g, paths.cheats))
    {
        cheats_apply(&cheats);
        int on = 0;
        for (int i = 0; i < cheats.count; ++i)
            on += cheats.items[i].enabled;
        if (on)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), tr(on == 1 ? "%d cheat active" : "%d cheats active"), on);
            show_toast(msg);
        }
    }
    screen = SCREEN_GAME;
}

static void stop_game(void)
{
    ra_game_unloaded();
    host_unload();
    plat_audio_open(UI_RATE);
    plat_audio_clear();
    cheats_clear(&cheats);
    current_game = NULL;
    screen = SCREEN_LIBRARY;
}

/* ---------------------------------------------------------------- library */

static void library_screen(uint32_t pressed)
{
    if (storage_error[0])
    {
        coverflow_backdrop();
        draw_title(PSXS5_NAME, tr("Storage unavailable"));
        text_draw(960, 440, 40, FONT_BOLD, COL_TEXT, ALIGN_CENTER, tr("PSXS5 can't open /data/PSXS5"));
        text_draw_fit(960, 510, 26, FONT_REGULAR, COL_DIM, ALIGN_CENTER, 1600, storage_error);
        draw_hints3(GLYPH_SQUARE, "Settings", GLYPH_CROSS, NULL, NULL);
        if (pressed & BIT(BTN_SQUARE))
        {
            settings_return = SCREEN_LIBRARY;
            screen = SCREEN_SETTINGS;
        }
        draw_toast();
        return;
    }
    const char *notice = toast[0] && plat_ticks_us() < toast_until ? toast : NULL;
    switch (coverflow_frame(&shelf, &library, pressed, frame_dt, notice))
    {
    case CF_PLAY:
        start_game(shelf.cursor);
        break;
    case CF_SETTINGS:
        sfx_play(SFX_SELECT);
        settings_return = SCREEN_LIBRARY;
        settings_cursor = settings_scroll = 0;
        screen = SCREEN_SETTINGS;
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- in-game menu */

enum MenuItem
{
    MI_RESUME,
    MI_SAVE,
    MI_LOAD,
    MI_SLOT,
    MI_DISC,
    MI_CHEATS,
    MI_RESET,
    MI_SETTINGS,
    MI_QUIT,
    MI_COUNT
};

static void menu_screen(uint32_t pressed)
{
    int discs = host_disc_count();
    if (pressed & BIT(BTN_UP))
        menu_cursor = (menu_cursor + MI_COUNT - 1) % MI_COUNT;
    if (pressed & BIT(BTN_DOWN))
        menu_cursor = (menu_cursor + 1) % MI_COUNT;
    if (menu_cursor == MI_DISC && discs < 2)
        menu_cursor = (pressed & BIT(BTN_UP)) ? MI_SLOT : MI_CHEATS;
    if (pressed & (BIT(BTN_UP) | BIT(BTN_DOWN)))
        sfx_play(SFX_CLICK);

    char st[PSXS5_PATH_MAX];
    state_path(st, sizeof(st), current_game, settings.state_slot);
    bool left = pressed & BIT(BTN_LEFT), right = pressed & BIT(BTN_RIGHT);

    if (menu_cursor == MI_SLOT && (left || right))
        settings.state_slot = (settings.state_slot + (right ? 1 : 9)) % 10;
    static int disc_choice = -1;
    if (disc_choice < 0 || disc_choice >= discs)
        disc_choice = host_disc_index();
    if (menu_cursor == MI_DISC && (left || right))
        disc_choice = (disc_choice + (right ? 1 : discs - 1)) % discs;

    if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_MENU)))
    {
        screen = SCREEN_GAME;
        return;
    }
    if (pressed & BIT(BTN_CROSS))
    {
        char msg[64];
        switch (menu_cursor)
        {
        case MI_RESUME:
            screen = SCREEN_GAME;
            return;
        case MI_SAVE:
            make_dirs(paths.states);
            if (host_save_state(st))
            {
                snprintf(msg, sizeof(msg), tr("Saved to slot %d"), settings.state_slot);
                screen = SCREEN_GAME;
            }
            else
                snprintf(msg, sizeof(msg), tr("Couldn't save to slot %d"), settings.state_slot);
            show_toast(msg);
            break;
        case MI_LOAD:
            if (ra_hardcore())
                snprintf(msg, sizeof(msg), "%s", tr("Not allowed in hardcore mode"));
            else if (host_load_state(st))
            {
                snprintf(msg, sizeof(msg), tr("Loaded slot %d"), settings.state_slot);
                screen = SCREEN_GAME;
            }
            else
                snprintf(msg, sizeof(msg), tr("Slot %d is empty"), settings.state_slot);
            show_toast(msg);
            break;
        case MI_DISC:
            if (host_disc_select(disc_choice))
            {
                snprintf(msg, sizeof(msg), tr("Disc %d inserted"), disc_choice + 1);
                screen = SCREEN_GAME;
            }
            else
                snprintf(msg, sizeof(msg), "%s", tr("Disc change failed"));
            show_toast(msg);
            break;
        case MI_CHEATS:
            if (ra_hardcore())
            {
                show_toast("Cheats are off in hardcore mode");
                break;
            }
            cheat_cursor = cheat_scroll = 0;
            screen = SCREEN_CHEATS;
            return;
        case MI_RESET:
            host_reset();
            ra_reset();
            show_toast("Console reset");
            screen = SCREEN_GAME;
            break;
        case MI_SETTINGS:
            settings_return = SCREEN_MENU;
            settings_cursor = settings_scroll = 0;
            screen = SCREEN_SETTINGS;
            return;
        case MI_QUIT:
            config_save(&settings, paths.config);
            stop_game();
            sfx_play(SFX_BACK);
            return;
        default:
            break;
        }
    }

    draw_game_frame(70);
    float x = 660, w = 600, row_h = 66, y0 = 230;
    int rows = discs < 2 ? MI_COUNT - 1 : MI_COUNT;
    draw_rect(x, y0 - 110, w, 110 + rows * row_h + 30, COL_PANEL);
    draw_rect(x, y0 - 110, w, 6, 0xff8fb0ffu);
    text_draw_fit(x + 36, y0 - 80, 28, FONT_BOLD, COL_TEXT, ALIGN_LEFT, w - 72,
                  current_game ? current_game->title : "");

    float y = y0;
    for (int i = 0; i < MI_COUNT; ++i)
    {
        if (i == MI_DISC && discs < 2)
            continue;
        char label[64], value[48] = "";
        switch (i)
        {
        case MI_RESUME: str_copy(label, sizeof(label), tr("Resume")); break;
        case MI_SAVE: str_copy(label, sizeof(label), tr("Save state")); break;
        case MI_LOAD:
            str_copy(label, sizeof(label), tr("Load state"));
            if (ra_hardcore())
                str_copy(value, sizeof(value), "hardcore");
            else if (!path_exists(st))
                str_copy(value, sizeof(value), tr("empty"));
            break;
        case MI_SLOT:
            str_copy(label, sizeof(label), tr("Slot"));
            snprintf(value, sizeof(value), "<  %d  >", settings.state_slot);
            break;
        case MI_DISC:
            str_copy(label, sizeof(label), tr("Disc"));
            snprintf(value, sizeof(value), tr("<  %d of %d  >"), disc_choice + 1, discs);
            break;
        case MI_CHEATS:
            str_copy(label, sizeof(label), tr("Cheats"));
            if (ra_hardcore())
                str_copy(value, sizeof(value), "hardcore");
            else
                snprintf(value, sizeof(value), "%d", cheats.count);
            break;
        case MI_RESET: str_copy(label, sizeof(label), tr("Reset")); break;
        case MI_SETTINGS: str_copy(label, sizeof(label), tr("Settings")); break;
        default: str_copy(label, sizeof(label), tr("Quit to library")); break;
        }
        if (i == menu_cursor)
            draw_rect(x + 16, y - 10, w - 32, row_h - 8, COL_SELECT);
        text_draw(x + 44, y + 4, 28, FONT_REGULAR, COL_TEXT, ALIGN_LEFT, label);
        if (value[0])
            text_draw(x + w - 44, y + 4, 28, FONT_REGULAR, COL_DIM, ALIGN_RIGHT, value);
        y += row_h;
    }
    draw_hints3(GLYPH_CROSS, "Select", GLYPH_CIRCLE, "Resume", NULL);
    draw_toast();
}

/* ---------------------------------------------------------------- settings */

typedef enum
{
    ROW_SECTION,
    ROW_CHOICE,
    ROW_ACTION,
} RowKind;

typedef struct
{
    RowKind kind;
    const char *name;
    int *value;       /* int-backed choice */
    bool *flag;       /* bool-backed choice */
    const char *const *labels;
    int count;
    int base;         /* first value (upscale starts at 1) */
    const char *note; /* shown on the right */
} Row;

static const char *const OFF_ON[] = {"Off", "On"};
static const char *const ASPECTS[] = {"Auto (game)", "4:3", "16:9", "16:10", "1:1 pixels",
                                      "Stretch to screen"};
static const char *const INTERNAL[] = {"Native", "2x"};
static const char *const UPSCALE[] = {"Off", "2x", "3x", "4x"};
static const char *const FILTERS[] = {"Sharp pixels", "Smooth pixels (Scale2x)", "xBR (smoothest)"};
static const char *const REGIONS[] = {"Auto", "NTSC (60 Hz)", "PAL (50 Hz)"};
static const char *const BIOS[] = {"Real BIOS if present", "Built-in HLE"};
static const char *const PADS[] = {"Digital pad", "DualShock (analog)"};
static const char *const COVER_STYLES[] = {"Flat", "3D box"};
static const char *const SOUND_STYLES[] = {"Soft", "Wood", "Pop", "Chime", "Classic", "Off"};
static const char *const VOLUMES[] = {"25%", "50%", "75%", "100%"};
static const char *const STICK_MODES[] = {"Auto (digital games)", "Always", "Off"};
static bool hardcore_setting;
static const char *LANGUAGES[LANG_COUNT];

static int build_rows(Row *rows)
{
    int n = 0;
    rows[n++] = (Row){ROW_SECTION, "Video", 0, 0, 0, 0, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Internal resolution", &settings.internal_res, 0, INTERNAL, 2, 1,
                      "sharper 3D"};
    rows[n++] = (Row){ROW_CHOICE, "Upscale", &settings.upscale, 0, UPSCALE, 4, 1, 0};
    rows[n++] = (Row){ROW_CHOICE, "Upscale filter", &settings.upscale_filter, 0, FILTERS, UPSCALE_FILTER_COUNT, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Aspect ratio", &settings.aspect, 0, ASPECTS, ASPECT_COUNT, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Integer scaling", 0, &settings.integer_scale, OFF_ON, 2, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Smooth final scaling", 0, &settings.smooth, OFF_ON, 2, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Dithering", 0, &settings.dithering, OFF_ON, 2, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Show FPS", 0, &settings.show_fps, OFF_ON, 2, 0, 0};
    rows[n++] = (Row){ROW_SECTION, "System", 0, 0, 0, 0, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Region", &settings.region, 0, REGIONS, REGION_COUNT, 0, "next game"};
    rows[n++] = (Row){ROW_CHOICE, "BIOS", 0, &settings.force_hle, BIOS, 2, 0, "next game"};
    rows[n++] = (Row){ROW_CHOICE, "Controller", 0, &settings.analog, PADS, 2, 0, "next game"};
    rows[n++] = (Row){ROW_CHOICE, "Left stick as D-pad", &settings.stick_dpad, 0, STICK_MODES,
                      STICK_DPAD_COUNT, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Fast CD loading", 0, &settings.cd_fast, OFF_ON, 2, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Unlock /data with etaHEN", 0, &unlock_setting, OFF_ON, 2, 0,
                      "next launch"};
    rows[n++] = (Row){ROW_SECTION, "RetroAchievements", 0, 0, 0, 0, 0, 0};
    static char account[96];
    if (ra_user()[0])
        snprintf(account, sizeof(account), "%s%s", ra_user(), ra_signed_in() ? "" : tr(" (not signed in)"));
    else
        str_copy(account, sizeof(account), tr("set up with psxs5_sync.py ra-login"));
    hardcore_setting = ra_hardcore();
    rows[n++] = (Row){ROW_ACTION, "Account", 0, 0, 0, 0, 0, account};
    rows[n++] = (Row){ROW_CHOICE, "Hardcore mode", 0, &hardcore_setting, OFF_ON, 2, 0,
                      "no states or cheats"};
    rows[n++] = (Row){ROW_SECTION, "Library", 0, 0, 0, 0, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Cover art", &settings.cover_style, 0, COVER_STYLES, 2, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Download missing covers", 0, &settings.cover_download, OFF_ON, 2, 0, 0};
    for (int i = 0; i < LANG_COUNT; ++i)
        LANGUAGES[i] = i18n_name(i);
    rows[n++] = (Row){ROW_CHOICE, "Language", &settings.language, 0, LANGUAGES, LANG_COUNT, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Interface sound", &settings.ui_sound, 0, SOUND_STYLES, 6, 0, 0};
    rows[n++] = (Row){ROW_CHOICE, "Interface volume", &settings.ui_volume, 0, VOLUMES, 4, 0, 0};
    rows[n++] = (Row){ROW_ACTION, "Rescan library", 0, 0, 0, 0, 0, host_loaded() ? "quit the game first" : 0};
    return n;
}

static int row_value(const Row *r)
{
    return r->flag ? (*r->flag ? 1 : 0) : *r->value - r->base;
}

static void settings_screen(uint32_t pressed)
{
    Row rows[32];
    int n = build_rows(rows);
    int step = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
    if (step)
    {
        do
            settings_cursor = (settings_cursor + n + step) % n;
        while (rows[settings_cursor].kind == ROW_SECTION);
        sfx_play(SFX_CLICK);
    }
    if (rows[settings_cursor].kind == ROW_SECTION)
        settings_cursor = (settings_cursor + 1) % n;

    Row *r = &rows[settings_cursor];
    int delta = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
    if ((pressed & BIT(BTN_CROSS)) && r->kind == ROW_CHOICE)
        delta = 1;
    if (delta && r->kind == ROW_CHOICE)
    {
        int v = (row_value(r) + r->count + delta) % r->count;
        if (r->flag)
            *r->flag = v != 0;
        else
            *r->value = v + r->base;
        if (host_loaded())
            host_apply_settings(&settings);
        if (r->flag == &unlock_setting)
            plat_set_unlock_disabled(!unlock_setting);
        if (r->flag == &hardcore_setting)
        {
            if (!ra_user()[0])
                show_toast("Sign in first: psxs5_sync.py ra-login");
            else
            {
                ra_set_hardcore(hardcore_setting); /* turning it on restarts the game */
                if (hardcore_setting)
                    cheats_clear(&cheats);
            }
        }
        if (r->value == &settings.language)
            i18n_set(settings.language);
        sfx_configure(settings.ui_sound, (settings.ui_volume + 1) * 25);
        sfx_play(SFX_CLICK); /* also previews the chosen sound */
    }
    if ((pressed & BIT(BTN_CROSS)) && r->kind == ROW_ACTION)
    {
        /* a rescan reallocates the library, which the running game points into */
        if (strcmp(r->name, "Rescan library") == 0 && !storage_error[0] && !host_loaded())
        {
            rescan();
            char msg[64];
            snprintf(msg, sizeof(msg), tr(library.count == 1 ? "Found %d game" : "Found %d games"), library.count);
            show_toast(msg);
        }
    }
    if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_SQUARE)))
    {
        config_save(&settings, paths.config);
        if (settings.cover_style != covers_style_loaded ||
            settings.cover_download != covers_download_loaded)
            restart_covers();
        sfx_play(SFX_BACK);
        screen = settings_return;
        return;
    }

    if (host_loaded())
        draw_game_frame(45);
    else
        coverflow_backdrop();
    draw_title("Settings", current_game ? current_game->title : tr("Applies to every game"));

    const int visible = 12;
    const float top = 170, row_h = 62, x = 64, w = plat_width() - 128.0f;
    if (settings_cursor < settings_scroll + 1)
        settings_scroll = settings_cursor > 0 ? settings_cursor - 1 : 0;
    if (settings_cursor >= settings_scroll + visible)
        settings_scroll = settings_cursor - visible + 1;
    draw_rect(x - 16, top - 16, w + 32, visible * row_h + 24, COL_PANEL);
    for (int i = 0; i < visible && settings_scroll + i < n; ++i)
    {
        const Row *row = &rows[settings_scroll + i];
        float y = top + i * row_h;
        bool sel = settings_scroll + i == settings_cursor;
        if (row->kind == ROW_SECTION)
        {
            text_draw(x + 8, y + 18, 24, FONT_BOLD, 0xff8fb0ffu, ALIGN_LEFT, tr(row->name));
            continue;
        }
        draw_rect(x, y, w, row_h - 8, sel ? COL_SELECT : COL_ROW);
        text_draw(x + 28, y + 12, 28, FONT_REGULAR, COL_TEXT, ALIGN_LEFT, tr(row->name));
        if (row->kind == ROW_CHOICE)
        {
            char v[80];
            snprintf(v, sizeof(v), "<   %s   >", tr(row->labels[row_value(row)]));
            text_draw(x + 900, y + 12, 28, FONT_REGULAR, COL_TEXT, ALIGN_LEFT, v);
        }
        if (row->note)
            text_draw(x + w - 28, y + 16, 22, FONT_REGULAR, sel ? COL_TEXT : COL_DIM, ALIGN_RIGHT,
                      tr(row->note));
    }
    char bios[96];
    describe_bios(bios, sizeof(bios));
    char info[PSXS5_PATH_MAX + 128];
    snprintf(info, sizeof(info), "%s   \xc2\xb7   Data: %s", bios, paths.root);
    text_draw_fit(64, 950, 22, FONT_REGULAR, COL_DIM, ALIGN_LEFT, 1790, info);
    draw_hints3(GLYPH_CROSS, "Change", GLYPH_CIRCLE, "Back", "Left / Right  Change");
    draw_toast();
}

/* ---------------------------------------------------------------- cheats */

static void cheats_screen(uint32_t pressed)
{
    const int rows = 11;
    const float row_h = 66, top = 200;
    if (cheats.count > 0)
    {
        int before = cheat_cursor;
        if (pressed & BIT(BTN_UP))
            cheat_cursor = (cheat_cursor + cheats.count - 1) % cheats.count;
        if (pressed & BIT(BTN_DOWN))
            cheat_cursor = (cheat_cursor + 1) % cheats.count;
        if (pressed & BIT(BTN_L1))
            cheat_cursor = cheat_cursor > rows ? cheat_cursor - rows : 0;
        if (pressed & BIT(BTN_R1))
            cheat_cursor = cheat_cursor + rows < cheats.count ? cheat_cursor + rows : cheats.count - 1;
        if (cheat_cursor != before)
            sfx_play(SFX_CLICK);
        if (pressed & BIT(BTN_CROSS))
        {
            cheats.items[cheat_cursor].enabled = !cheats.items[cheat_cursor].enabled;
            cheats_apply(&cheats);
            cheats_save_selection(&cheats);
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_SQUARE))
        {
            for (int i = 0; i < cheats.count; ++i)
                cheats.items[i].enabled = false;
            cheats_apply(&cheats);
            cheats_save_selection(&cheats);
            show_toast("All cheats off");
        }
    }
    if (cheat_cursor < cheat_scroll)
        cheat_scroll = cheat_cursor;
    if (cheat_cursor >= cheat_scroll + rows)
        cheat_scroll = cheat_cursor - rows + 1;
    if (pressed & BIT(BTN_CIRCLE))
    {
        sfx_play(SFX_BACK);
        screen = SCREEN_MENU;
        return;
    }

    draw_game_frame(40);
    const char *src = strrchr(cheats.source, '/');
    draw_title("Cheats", cheats.count ? (src ? src + 1 : cheats.source) : NULL);
    if (cheats.count == 0)
    {
        text_draw(960, 420, 40, FONT_BOLD, COL_TEXT, ALIGN_CENTER, tr("No cheats for this game"));
        char where[PSXS5_PATH_MAX + 48];
        snprintf(where, sizeof(where), tr("Put .cht files in %s, or one next to the game."), paths.cheats);
        text_draw_fit(960, 490, 26, FONT_REGULAR, COL_DIM, ALIGN_CENTER, 1700, where);
        text_draw(960, 534, 26, FONT_REGULAR, COL_DIM, ALIGN_CENTER,
                  tr("tools/psxs5_sync.py cheats installs the libretro cheat library."));
    }
    else
    {
        float x = 64, w = plat_width() - 128.0f;
        draw_rect(x - 16, top - 16, w + 32, rows * row_h + 24, COL_PANEL);
        for (int i = 0; i < rows && cheat_scroll + i < cheats.count; ++i)
        {
            const Cheat *c = &cheats.items[cheat_scroll + i];
            float y = top + i * row_h;
            bool sel = cheat_scroll + i == cheat_cursor;
            draw_rect(x, y, w, row_h - 8, sel ? COL_SELECT : COL_ROW);
            draw_rect(x + 22, y + 12, 84, 34, c->enabled ? COL_GOOD : 0x60ffffffu);
            text_draw(x + 64, y + 16, 22, FONT_BOLD, c->enabled ? 0xff0b2a1cu : COL_TEXT,
                      ALIGN_CENTER, c->enabled ? "ON" : "OFF");
            text_draw_fit(x + 136, y + 12, 28, FONT_REGULAR, COL_TEXT, ALIGN_LEFT, w - 170, c->desc);
        }
    }
    draw_hints3(GLYPH_CROSS, "Toggle", GLYPH_SQUARE, "All off", "L1 / R1  Page");
    draw_toast();
}

/* ---------------------------------------------------------------- emulation */

static void game_screen(PadState *pads, uint32_t pressed)
{
    (void)pressed;
    static uint32_t combo_prev;
    uint32_t combo = pads[0].buttons & (BIT(BTN_L3) | BIT(BTN_R3));
    bool combo_hit = combo == (BIT(BTN_L3) | BIT(BTN_R3)) && combo_prev != combo;
    combo_prev = combo;

    /* Touchpad: a tap is the PS1's Select (the PS5 SDL driver has no Create
     * button); holding it opens the PSXS5 menu. */
    static uint64_t touch_since;
    static bool touch_used;
    static int select_frames;
    bool touch = (pads[0].buttons | pads[1].buttons) & BIT(BTN_MENU);
    uint64_t t_now = plat_ticks_us();
    bool open_menu = combo_hit;
    if (touch)
    {
        if (!touch_since)
            touch_since = t_now;
        else if (!touch_used && t_now - touch_since > 700000)
        {
            touch_used = true;
            open_menu = true;
        }
    }
    else
    {
        if (touch_since && !touch_used)
            select_frames = 6; /* ~100 ms: long enough for every game to see it */
        touch_since = 0;
        touch_used = false;
    }
    if (open_menu)
    {
        menu_cursor = MI_RESUME;
        screen = SCREEN_MENU;
        draw_game_frame(70);
        return;
    }
    if (select_frames > 0)
    {
        pads[0].buttons |= BIT(BTN_SELECT);
        --select_frames;
    }
    for (int i = 0; i < PSXS5_MAX_PADS; ++i)
    {
        pads[i].buttons &= ~BIT(BTN_MENU);
        /* Digital-only games (or digital mode) ignore the sticks: let the left
         * stick drive the D-pad there, while analog games keep real analog. */
        bool map = settings.stick_dpad == STICK_DPAD_ALWAYS ||
                   (settings.stick_dpad == STICK_DPAD_AUTO && host_pad_digital(i));
        if (map)
        {
            const int16_t dead = 16000;
            if (pads[i].lx < -dead) pads[i].buttons |= BIT(BTN_LEFT);
            if (pads[i].lx > dead) pads[i].buttons |= BIT(BTN_RIGHT);
            if (pads[i].ly < -dead) pads[i].buttons |= BIT(BTN_UP);
            if (pads[i].ly > dead) pads[i].buttons |= BIT(BTN_DOWN);
        }
    }
    host_set_pads(pads);

    /* Pace by the audio queue: the core's 59.94/50 Hz never matches the TV
     * exactly, so run 0-2 frames per refresh to keep ~3 frames of sound queued. */
    static uint64_t last_us, fps_window_start;
    static int fps_frames;
    uint64_t now = plat_ticks_us();
    size_t per_frame = (size_t)(host_sample_rate() / host_fps());
    size_t queued = plat_audio_queued_frames();
    int runs = 1;
    if (queued < per_frame * 2)
        runs = 2;
    else if (queued > per_frame * 5)
        runs = 0;
    static uint64_t emu_us;
    static int emu_frames;
    uint64_t emu_start = plat_ticks_us();
    for (int i = 0; i < runs; ++i)
    {
        host_run_frame();
        ra_frame();
    }
    emu_us += plat_ticks_us() - emu_start;
    emu_frames += runs;
    if (emu_frames >= 240)
    {
        psxs5_log("emu: %.1f ms per emulated frame, %.1f fps measured", emu_us / 1000.0 / emu_frames,
                  fps_measured);
        emu_us = 0;
        emu_frames = 0;
    }
    fps_frames += runs;
    if (now - fps_window_start >= 1000000)
    {
        fps_measured = fps_frames * 1000000.0f / (float)(now - fps_window_start);
        fps_frames = 0;
        fps_window_start = now;
    }
    if (runs == 0 && now - last_us < 4000)
        plat_sleep_us(1000);
    last_us = now;

    draw_game_frame(255);
    if (settings.show_fps)
    {
        char f[32];
        snprintf(f, sizeof(f), "%.1f FPS", fps_measured);
        draw_rect(24, 24, text_width(26, FONT_BOLD, f) + 36, 52, 0xa0000000u);
        text_draw(42, 36, 26, FONT_BOLD, COL_GOOD, ALIGN_LEFT, f);
    }
    draw_toast();
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    /* First thing: proves the loader accepted the title and main() runs. */
    plat_notify("PSXS5 " PSXS5_VERSION " starting...");
    char root[PSXS5_PATH_MAX];
    plat_default_root(root, sizeof(root));
    config_paths(&paths, root);

    /* Unlock before SDL starts any thread: the HEN changes this process's
     * credentials, which Porpoise did not survive with threads running. */
    sandboxed = !plat_prepare_storage(sandbox_reason, sizeof(sandbox_reason));

    if (!plat_init())
    {
        char msg[300];
        snprintf(msg, sizeof(msg), "PSXS5: %s", plat_init_error());
        plat_notify(msg);
        for (;;)
            plat_sleep_us(1000000);
    }
    if (!text_init())
        plat_notify("PSXS5: interface font missing from assets/fonts");

    /* Sandboxed or not, files in /data open and save; only listing differs. */
    const char *dirs[] = {paths.root, paths.games, paths.bios, paths.saves, paths.states,
                          paths.cheats, paths.covers, paths.logs};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        make_dirs(dirs[i]);
    char probe[PSXS5_PATH_MAX];
    path_join(probe, sizeof(probe), paths.root, ".write-test");
    FILE *pf = fopen(probe, "w");
    if (pf)
    {
        fclose(pf);
        remove(probe);
    }
    else
        snprintf(storage_error, sizeof(storage_error),
                 "%s isn't writable. Is the HEN running? (%s)", paths.root, sandbox_reason);
    char log_path[PSXS5_PATH_MAX];
    path_join(log_path, sizeof(log_path), paths.logs, "psxs5.log");
    psxs5_log_open(log_path);
    ps5_crash_install(log_path);
    psxs5_log("PSXS5 %s starting, data root %s", PSXS5_VERSION, paths.root);
#if defined(__PROSPERO__)
    extern size_t ps5_heap_size_mb(void);
    psxs5_log("heap: %zu MB of direct memory%s", ps5_heap_size_mb(),
              ps5_heap_size_mb() ? "" : " (unavailable: using the system heap)");
#endif
    psxs5_log(sandboxed ? "storage: sandboxed (%s)" : "storage: unlocked%s",
              sandboxed ? sandbox_reason : "");
    psxs5_log("storage probe before unlock: %s", plat_sandbox_probe());
    vk_probe(paths.root); /* v2: proves the Vulkan driver runs; logs only */
    unlock_setting = !plat_unlock_disabled();

    config_load(&settings, paths.config);
    i18n_set(settings.language);
    plat_audio_open(UI_RATE);
    sfx_init(UI_RATE);
    sfx_configure(settings.ui_sound, (settings.ui_volume + 1) * 25);
    if (!storage_error[0])
        ra_init(&paths);
    coverflow_init(&shelf, 0);
    if (!storage_error[0])
        rescan();
    coverflow_init(&shelf, settings.last_game < library.count ? settings.last_game : 0);
    plat_notify("PSXS5 ready");

    bool quit = false;
    PadState pads[PSXS5_MAX_PADS];
    uint64_t last = plat_ticks_us();
    while (!quit)
    {
        uint64_t now = plat_ticks_us();
        frame_dt = (float)(now - last) / 1e6f;
        if (frame_dt > 0.1f)
            frame_dt = 0.1f;
        last = now;

        plat_poll(pads, &quit);
        uint32_t pressed = nav_pressed(pads);
        plat_begin_frame(0xff000000u);
        static const char *const screen_names[] = {"library", "game", "menu", "settings", "cheats"};
        ps5_crash_step(screen_names[screen]);
        switch (screen)
        {
        case SCREEN_LIBRARY: library_screen(pressed); break;
        case SCREEN_GAME: game_screen(pads, pressed); break;
        case SCREEN_MENU: menu_screen(pressed); break;
        case SCREEN_SETTINGS: settings_screen(pressed); break;
        case SCREEN_CHEATS: cheats_screen(pressed); break;
        }
        if (screen != SCREEN_GAME)
            ra_idle();
        draw_achievement();
        plat_end_frame();
    }

    /* Only the desktop build gets here; the PS5 shell closes the title. */
    stop_game();
    ra_shutdown();
    config_save(&settings, paths.config);
    covers_stop();
    text_shutdown();
    library_free(&library);
    plat_shutdown();
    return 0;
}
