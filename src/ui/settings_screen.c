/*
 * PSXS5 - settings: tabs down the left, grouped rows, a help panel, and
 * per-game settings (L2 / R2 switch between this game and all games).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../config.h"
#include "../core/host.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../platform/xbr.h"
#include "../ra/achievements.h"
#include "../remote.h"
#include "../update.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "coverflow.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include "stb_image.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the rows */

enum Kind
{
    K_CHOICE,
    K_TOGGLE,
    K_ACTION,
    K_INFO,
};

enum Apply
{
    APPLY_NOW,
    APPLY_NEXT_GAME,
    APPLY_NEXT_LAUNCH,
};

enum Special
{
    SP_NONE,
    SP_LANGUAGE,
    SP_HARDCORE,
    SP_UNLOCK,
    SP_ACCOUNT,
    SP_BIOS,
    SP_GAMES,
    SP_DATA,
    SP_VERSION,
    SP_REMAP,
    SP_RESCAN,
    SP_SOUND,
    SP_MEMCARDS,
    SP_REMOTE,
    SP_UPDATE,
    SP_HOTKEYS,
    SP_WHITELIST,
};

typedef struct
{
    const char *group; /* starts a new card with this caption */
    const char *name;
    const char *help;
    unsigned char kind, apply, special;
    bool global_only; /* the same for every game */
    int offset;       /* into Settings, -1 for specials */
    bool is_bool;
    const char *const *labels;
    int count, base;
} Row;

#define INT_FIELD(f) offsetof(Settings, f), false
#define BOOL_FIELD(f) offsetof(Settings, f), true
#define NO_FIELD -1, false

static const char *const OFF_ON[] = {"Off", "On"};
static const char *const ASPECTS[] = {"Auto (game)", "4:3", "16:9", "16:10", "1:1 pixels",
                                      "Stretch to screen"};
static const char *const INTERNAL[] = {"Native", "2x", "4x", "8x", "16x"};
static const char *const EMULATORS[] = {"Automatic", "PCSX-ReARMed", "Beetle PSX HW"};
static const char *const UPSCALE[] = {"Off", "2x", "3x", "4x"};
static const char *const FILTERS[] = {"Sharp pixels", "Smooth pixels (Scale2x)", "xBR (smoothest)"};
static const char *const REGIONS[] = {"Auto", "NTSC (60 Hz)", "PAL (50 Hz)"};
static const char *const BIOS[] = {"Real BIOS if present", "Built-in HLE"};
static const char *const PADS[] = {"Digital pad", "DualShock (analog)"};
static const char *const COVER_STYLES[] = {"Flat", "3D box"};
static const char *const SOUND_STYLES[] = {"Soft", "Wood", "Pop", "Chime", "Classic", "Off"};
static const char *const VOLUMES[] = {"25%", "50%", "75%", "100%"};
static const char *const STICK_MODES[] = {"Auto (digital games)", "Always", "Off"};
static const char *const BACKGROUNDS[] = {"Dark", "Cover colour"};
static const char *const CRT_LEVELS[] = {"Off", "Light", "Strong"};
static const char *const BORDERS[] = {"Black", "Soft glow", "TV frame"};
static const char *const PLAYERS[] = {"1 or 2", "Up to 4 (multitap)"};
static const char *const SORTS[] = {"Title", "Recently played", "Most played", "Region"};

static const Row DISPLAY[] = {
    {"Picture", "Upscale filter", "How pixels are smoothed when the picture is enlarged.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(upscale_filter), FILTERS, 3, 0},
    {NULL, "Upscale", "Enlarges the picture before it is scaled to the screen.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(upscale), UPSCALE, 4, 1},
    {NULL, "Aspect ratio", "The shape of the picture. Pair 16:9 with a widescreen cheat.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(aspect), ASPECTS, 6, 0},
    {NULL, "Widescreen", "Turns on the game's widescreen code from the cheat library and shows 16:9. Games without one stay 4:3.",
     K_TOGGLE, APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(widescreen), OFF_ON, 2, 0},
    {"Screen fit", "Integer scaling", "Whole-number scale factors only: even pixels, black borders.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(integer_scale), OFF_ON, 2, 0},
    {NULL, "Smooth final scaling", "Softens the last step up to your TV's resolution.", K_TOGGLE,
     APPLY_NOW, SP_NONE, false, BOOL_FIELD(smooth), OFF_ON, 2, 0},
    {"Look", "CRT scanlines", "Dark lines between the picture's lines, like an old TV.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(crt), CRT_LEVELS, 3, 0},
    {NULL, "Border", "What surrounds a 4:3 picture: black, a soft glow, or a TV.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(border), BORDERS, 3, 0},
    {"Overlay", "Show FPS", "Frames per second in the corner while you play.", K_TOGGLE, APPLY_NOW,
     SP_NONE, false, BOOL_FIELD(show_fps), OFF_ON, 2, 0},
};

static const Row GRAPHICS[] = {
    {"Rendering", "Internal resolution", "Draws 3D at a higher resolution: sharper polygons. Above 2x needs Beetle PSX HW.",
     K_CHOICE, APPLY_NEXT_GAME, SP_NONE, false, INT_FIELD(internal_res), INTERNAL, 5, 1},
    {NULL, "Precise geometry (PGXP)", "Beetle PSX HW: stops polygons wobbling and textures warping.",
     K_TOGGLE, APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(pgxp), OFF_ON, 2, 0},
    {NULL, "Dithering", "The PS1's dot pattern that fakes more colours. Off looks cleaner.", K_TOGGLE,
     APPLY_NOW, SP_NONE, false, BOOL_FIELD(dithering), OFF_ON, 2, 0},
};

static const Row CONTROLS[] = {
    {"Controller", "Controller", "DualShock gives games the analog sticks; some old games want the digital pad.",
     K_CHOICE, APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(analog), PADS, 2, 0},
    {NULL, "Left stick as D-pad", "Lets the left stick move in games that only read the D-pad.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(stick_dpad), STICK_MODES, 3, 0},
    {NULL, "Vibration", "Passes the game's rumble to your controller.", K_TOGGLE, APPLY_NOW, SP_NONE,
     false, BOOL_FIELD(rumble), OFF_ON, 2, 0},
    {NULL, "Vibration strength", "How strong the rumble feels.", K_CHOICE, APPLY_NOW, SP_NONE, false,
     INT_FIELD(rumble_strength), VOLUMES, 4, 0},
    {NULL, "Players", "Up to 4 with a multitap, for games that support it: each PS5 controller is a player.",
     K_CHOICE, APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(multitap), PLAYERS, 2, 0},
    {"Buttons", "Fast forward and rewind", "Hold the touchpad and press R2 to fast forward, or L2 to rewind (turn Rewind on in System).",
     K_INFO, APPLY_NOW, SP_HOTKEYS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Button mapping", "Choose what each button of your controller presses.", K_ACTION,
     APPLY_NOW, SP_REMAP, false, NO_FIELD, NULL, 0, 0},
};

static const Row SOUND[] = {
    {"Interface", "Interface sound", "The click when you move through menus and the shelf.", K_CHOICE,
     APPLY_NOW, SP_SOUND, true, INT_FIELD(ui_sound), SOUND_STYLES, 6, 0},
    {NULL, "Interface volume", "How loud the interface sounds are.", K_CHOICE, APPLY_NOW, SP_SOUND,
     true, INT_FIELD(ui_volume), VOLUMES, 4, 0},
};

static const Row ACHIEVEMENTS[] = {
    {"Account", "Signed in as", "Sign in on your PC: python tools/psxs5_sync.py ra-login. Only a token reaches the PS5.",
     K_INFO, APPLY_NOW, SP_ACCOUNT, true, NO_FIELD, NULL, 0, 0},
    {"While playing", "Unlock pop-ups", "The banner when you earn an achievement. Off only hides it: achievements still unlock.",
     K_TOGGLE, APPLY_NOW, SP_NONE, true, BOOL_FIELD(ra_popups), OFF_ON, 2, 0},
    {NULL, "Progress tracker", "The small card when a counted achievement moves (18/80 dragons).", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(ra_tracker), OFF_ON, 2, 0},
    {"Play", "Hardcore mode", "Earn hardcore achievements: save states and cheats are off. Turning it on restarts the game.",
     K_TOGGLE, APPLY_NOW, SP_HARDCORE, true, NO_FIELD, OFF_ON, 2, 0},
};

static const Row LIBRARY[] = {
    {"Shelf", "Cover art", "Flat front covers, or 3D boxes.", K_CHOICE, APPLY_NOW, SP_NONE, true,
     INT_FIELD(cover_style), COVER_STYLES, 2, 0},
    {NULL, "Download missing covers", "Fetches covers for new games over the internet.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(cover_download), OFF_ON, 2, 0},
    {NULL, "Background", "Dark keeps the screen deep navy; Cover colour tints it with the selected game.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(background), BACKGROUNDS, 2, 0},
    {NULL, "Sort by", "The order of games on the shelf (OPTIONS on the shelf changes it too).", K_CHOICE,
     APPLY_NOW, SP_NONE, true, INT_FIELD(sort_mode), SORTS, 4, 0},
    {"Games", "Memory cards", "Every game's memory card: see the saves, export a card, import one.", K_ACTION,
     APPLY_NOW, SP_MEMCARDS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Rescan library", "Looks for games added to /data/PSXS5/games or a USB drive.", K_ACTION,
     APPLY_NOW, SP_RESCAN, true, NO_FIELD, NULL, 0, 0},
};

static const Row SYSTEM[] = {
    {"Emulation", "Emulator", "Beetle PSX HW is more accurate and renders on the GPU, but needs your BIOS. PCSX-ReARMed also runs without one.",
     K_CHOICE, APPLY_NEXT_GAME, SP_NONE, false, INT_FIELD(emulator), EMULATORS, 3, 0},
    {NULL, "Region", "Auto follows the disc; force 50 or 60 Hz if a game misbehaves.", K_CHOICE,
     APPLY_NEXT_GAME, SP_NONE, false, INT_FIELD(region), REGIONS, 3, 0},
    {NULL, "BIOS", "Your own BIOS dump in /data/PSXS5/bios, or the built-in one.", K_CHOICE,
     APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(force_hle), BIOS, 2, 0},
    {NULL, "Fast CD loading", "Shorter loading screens. Rarely, a game glitches.", K_TOGGLE, APPLY_NOW,
     SP_NONE, false, BOOL_FIELD(cd_fast), OFF_ON, 2, 0},
    {"Playing", "Quick resume", "Saves when you leave a game, so the shelf can offer Continue.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(quick_resume), OFF_ON, 2, 0},
    {NULL, "Rewind", "Keeps the last 8 seconds so you can go back (touchpad + L2). Uses about 200 MB of memory.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(rewind), OFF_ON, 2, 0},
    {"Phone", "Settings from your phone", "Change settings from a phone on the same network: scan the code.",
     K_TOGGLE, APPLY_NOW, SP_REMOTE, true, BOOL_FIELD(remote), OFF_ON, 2, 0},
    {"Console", "Language", "The language of PSXS5's menus.", K_CHOICE, APPLY_NOW, SP_LANGUAGE, true,
     INT_FIELD(language), NULL, LANG_COUNT, 0},
    {NULL, "Unlock /data with etaHEN", "PSXS5 asks etaHEN for access to your games. Turn off if closing PSXS5 crashes the console.",
     K_TOGGLE, APPLY_NEXT_LAUNCH, SP_UNLOCK, true, NO_FIELD, OFF_ON, 2, 0},
    {NULL, "Allow PSXS5 in PS5SX2 Helper", "PS5SX2 Helper only unlocks the apps listed in /data/whitelist.txt. This adds PSXS5; reload the helper (or restart the console) afterwards.",
     K_ACTION, APPLY_NOW, SP_WHITELIST, true, NO_FIELD, NULL, 0, 0},
};

static const Row ABOUT[] = {
    {"PSXS5", "Version", "PlayStation X Super 5: PS1 emulation for jailbroken PS5, built on PCSX-ReARMed.",
     K_INFO, APPLY_NOW, SP_VERSION, true, NO_FIELD, NULL, 0, 0},
    {NULL, "BIOS", "PSXS5 never includes a BIOS: use a dump of your own console, or the built-in one.",
     K_INFO, APPLY_NOW, SP_BIOS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Games", "Games are your own backups, in /data/PSXS5/games.", K_INFO, APPLY_NOW, SP_GAMES,
     true, NO_FIELD, NULL, 0, 0},
    {NULL, "Data folder", "Saves, states, covers and settings live here.", K_INFO, APPLY_NOW, SP_DATA,
     true, NO_FIELD, NULL, 0, 0},
    {"Updates", "Check for updates", "Looks for a newer PSXS5 on GitHub and installs it.", K_ACTION, APPLY_NOW,
     SP_UPDATE, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Check when PSXS5 starts", "Tells you on the shelf when a new version is out.", K_TOGGLE, APPLY_NOW,
     SP_NONE, true, BOOL_FIELD(update_check), OFF_ON, 2, 0},
};

typedef struct
{
    const char *name;
    int icon;
    const Row *rows;
    int count;
} Tab;

#define TAB(n, i, r) {n, i, r, (int)(sizeof(r) / sizeof(r[0]))}
static const Tab TABS[] = {
    TAB("Display", ICON_DEVICE_TV, DISPLAY),
    TAB("Graphics", ICON_SPARKLES, GRAPHICS),
    TAB("Controls", ICON_DEVICE_GAMEPAD_2, CONTROLS),
    TAB("Sound", ICON_VOLUME, SOUND),
    TAB("Achievements", ICON_TROPHY, ACHIEVEMENTS),
    TAB("Library", ICON_BOOKS, LIBRARY),
    TAB("System", ICON_CPU, SYSTEM),
    TAB("About", ICON_INFO_CIRCLE, ABOUT),
};
#define TAB_COUNT (int)(sizeof(TABS) / sizeof(TABS[0]))

/* ---------------------------------------------------------------- state */

static struct
{
    int tab, cursor;
    bool game_scope; /* editing this game's own settings */
    bool remap;      /* the button-mapping page */
    int remap_cursor;
    float tab_y, sel_y, scroll, scope_x, remap_y;
    float switch_t[16];
} S;

void settings_opened(void)
{
    S.cursor = -1;
    S.remap = false;
    S.game_scope = app.game != NULL && app.game_has_own;
    S.tab_y = 0;
    S.sel_y = -1;
    S.scroll = 0;
}

static const Tab *tab(void)
{
    return &TABS[S.tab];
}

static bool selectable(const Row *r)
{
    return r->kind != K_INFO;
}

static void first_selectable(void)
{
    S.cursor = -1;
    for (int i = 0; i < tab()->count; ++i)
        if (selectable(&tab()->rows[i]))
        {
            S.cursor = i;
            break;
        }
}

/* The settings a row edits: this game's own, or the console-wide ones. */
static Settings *target(const Row *r)
{
    return (!r->global_only && S.game_scope && app.game) ? &app.settings : &app.global;
}

static int read_value(const Row *r)
{
    switch (r->special)
    {
    case SP_HARDCORE: return ra_hardcore();
    case SP_UNLOCK: return app.unlock_setting;
    default: break;
    }
    if (r->offset < 0)
        return 0;
    const char *base = (const char *)target(r) + r->offset;
    return r->is_bool ? (*(const bool *)base ? 1 : 0) : *(const int *)base - r->base;
}

static void write_value(const Row *r, int v)
{
    switch (r->special)
    {
    case SP_HARDCORE:
        if (!ra_user()[0])
        {
            app_toast("Sign in first: psxs5_sync.py ra-login");
            return;
        }
        ra_set_hardcore(v != 0); /* turning it on restarts the game */
        if (v)
            cheats_clear(&app.cheats);
        return;
    case SP_UNLOCK:
        app.unlock_setting = v != 0;
        plat_set_unlock_disabled(!app.unlock_setting);
        return;
    default:
        break;
    }
    if (r->offset < 0)
        return;
    Settings *t = target(r);
    char *base = (char *)t + r->offset;
    if (r->is_bool)
        *(bool *)base = v != 0;
    else
        *(int *)base = v + r->base;
    /* console-wide edits reach the running game unless it has its own settings */
    if (t == &app.global && (!app.game || !app.game_has_own || r->global_only))
    {
        char *eff = (char *)&app.settings + r->offset;
        if (r->is_bool)
            *(bool *)eff = v != 0;
        else
            *(int *)eff = v + r->base;
    }
    if (r->special == SP_LANGUAGE)
        i18n_set(app.global.language);
    if (r->special == SP_REMOTE)
        remote_update(app.global.remote);
    if (r->special == SP_SOUND)
        sfx_configure(app.global.ui_sound, (app.global.ui_volume + 1) * 25);
    if (app.game)
        host_apply_settings(&app.settings);
}

static const char *value_label(const Row *r, char *buf, size_t size)
{
    int v = read_value(r);
    switch (r->special)
    {
    case SP_LANGUAGE: return i18n_name(v);
    case SP_ACCOUNT:
        if (!ra_user()[0])
            return tr("Not signed in");
        snprintf(buf, size, "%s%s", ra_user(), ra_signed_in() ? "" : tr(" (not signed in)"));
        return buf;
    case SP_VERSION: return PSXS5_VERSION;
    case SP_BIOS: app_describe_bios(buf, size); return buf;
    case SP_GAMES: snprintf(buf, size, "%d", app.library.count); return buf;
    case SP_DATA: return app.paths.root;
    case SP_HOTKEYS: return tr("Touchpad + R2 / L2");
    case SP_UPDATE:
        switch (update_state())
        {
        case UPDATE_CHECKING: return tr("Checking...");
        case UPDATE_NONE: return tr("Up to date");
        case UPDATE_AVAILABLE: snprintf(buf, size, tr("%s available: install"), update_version()); return buf;
        case UPDATE_INSTALLING: return tr("Installing...");
        case UPDATE_INSTALLED: return tr("Installed: restart PSXS5");
        case UPDATE_FAILED: return update_message();
        default: return PSXS5_VERSION;
        }
    default: break;
    }
    if (!r->labels || v < 0 || v >= r->count)
        return "";
    return tr(r->labels[v]);
}

static void default_value(const Row *r)
{
    if (r->offset < 0)
        return;
    Settings d;
    config_defaults(&d);
    const char *base = (const char *)&d + r->offset;
    write_value(r, r->is_bool ? (*(const bool *)base ? 1 : 0) : *(const int *)base - r->base);
}

/* ---------------------------------------------------------------- the button-mapping page */

typedef struct
{
    int button; /* BTN_* on the controller */
    int glyph;
    const char *name;
    float x, y; /* on the controller drawing, relative to its centre */
} PadButton;

static const PadButton PAD[] = {
    {BTN_CROSS, GLYPH_CROSS, "Cross", 230, 46},
    {BTN_CIRCLE, GLYPH_CIRCLE, "Circle", 290, -14},
    {BTN_SQUARE, GLYPH_SQUARE, "Square", 170, -14},
    {BTN_TRIANGLE, GLYPH_TRIANGLE, "Triangle", 230, -74},
    {BTN_UP, GLYPH_UP, "Up", -230, -70},
    {BTN_DOWN, GLYPH_DOWN, "Down", -230, 42},
    {BTN_LEFT, GLYPH_LEFT, "Left", -286, -14},
    {BTN_RIGHT, GLYPH_RIGHT, "Right", -174, -14},
    {BTN_L1, GLYPH_L1, "L1", -230, -168},
    {BTN_R1, GLYPH_R1, "R1", 230, -168},
    {BTN_L2, GLYPH_L2, "L2", -230, -226},
    {BTN_R2, GLYPH_R2, "R2", 230, -226},
    {BTN_L3, GLYPH_L3, "L3 (left stick)", -110, 110},
    {BTN_R3, GLYPH_R3, "R3 (right stick)", 110, 110},
    {BTN_START, GLYPH_START, "OPTIONS", 214, -122},
};
#define PAD_COUNT (int)(sizeof(PAD) / sizeof(PAD[0]))

/* What the PS1 controller has, in the order Left/Right cycles through. */
static const struct
{
    int button, glyph;
    const char *name;
} PS1[] = {
    {BTN_CROSS, GLYPH_CROSS, "Cross"},       {BTN_CIRCLE, GLYPH_CIRCLE, "Circle"},
    {BTN_SQUARE, GLYPH_SQUARE, "Square"},    {BTN_TRIANGLE, GLYPH_TRIANGLE, "Triangle"},
    {BTN_UP, GLYPH_UP, "Up"},                {BTN_DOWN, GLYPH_DOWN, "Down"},
    {BTN_LEFT, GLYPH_LEFT, "Left"},          {BTN_RIGHT, GLYPH_RIGHT, "Right"},
    {BTN_L1, GLYPH_L1, "L1"},                {BTN_R1, GLYPH_R1, "R1"},
    {BTN_L2, GLYPH_L2, "L2"},                {BTN_R2, GLYPH_R2, "R2"},
    {BTN_L3, GLYPH_L3, "L3"},                {BTN_R3, GLYPH_R3, "R3"},
    {BTN_START, GLYPH_PS1_START, "Start"},   {BTN_SELECT, GLYPH_SELECT, "Select"},
    {-1, GLYPH_NONE, "Nothing"},
};
#define PS1_COUNT (int)(sizeof(PS1) / sizeof(PS1[0]))

static int ps1_index(int button)
{
    for (int i = 0; i < PS1_COUNT; ++i)
        if (PS1[i].button == button)
            return i;
    return PS1_COUNT - 1;
}

static Settings *map_target(void)
{
    return (S.game_scope && app.game) ? &app.settings : &app.global;
}

static void set_map(int button, int to)
{
    map_target()->button_map[button] = (int8_t)to;
    if (map_target() == &app.global && (!app.game || !app.game_has_own))
        app.settings.button_map[button] = (int8_t)to;
}

static const char *const PRESETS[] = {"Default", "Swap Cross and Circle", "Swap shoulders and triggers"};

static void apply_preset(int preset)
{
    for (int b = 0; b < 16; ++b)
        set_map(b, b);
    if (preset == 1)
    {
        set_map(BTN_CROSS, BTN_CIRCLE);
        set_map(BTN_CIRCLE, BTN_CROSS);
    }
    else if (preset == 2)
    {
        set_map(BTN_L1, BTN_L2);
        set_map(BTN_L2, BTN_L1);
        set_map(BTN_R1, BTN_R2);
        set_map(BTN_R2, BTN_R1);
    }
    char msg[96];
    snprintf(msg, sizeof(msg), tr("Mapping: %s"), tr(PRESETS[preset]));
    app_toast(msg);
}

static void draw_controller(float cx, float cy, int highlight)
{
    /* a DualSense, simplified: body, grips, touchpad, sticks */
    const uint32_t body = 0xff1c2250u, edge = 0xff2f3a80u;
    draw_rrect(cx - 360, cy - 130, 720, 250, 120, edge);
    draw_rrect(cx - 354, cy - 124, 708, 238, 116, body);
    draw_circle(cx - 250, cy + 140, 92, edge);
    draw_circle(cx + 250, cy + 140, 92, edge);
    draw_circle(cx - 250, cy + 140, 86, body);
    draw_circle(cx + 250, cy + 140, 86, body);
    draw_rrect(cx - 354, cy - 60, 708, 170, 60, body);
    draw_rrect(cx - 110, cy - 118, 220, 110, 18, 0xff141938u); /* touchpad */
    text_draw(cx, cy - 80, 18, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("Touchpad: tap = Select, hold = menu"));
    for (int s = -1; s <= 1; s += 2)
    {
        draw_circle(cx + s * 110, cy + 110, 46, 0xff0f1330u);
        draw_circle(cx + s * 110, cy + 110, 34, edge);
    }
    for (int i = 0; i < PAD_COUNT; ++i)
    {
        float x = cx + PAD[i].x, y = cy + PAD[i].y;
        bool on = i == highlight;
        if (on)
        {
            float pulse = 0.6f + 0.4f * sinf((float)plat_ticks_us() / 1e6f * 6.0f);
            float w = pad_glyph_width((enum PadGlyph)PAD[i].glyph, 44) + 20;
            draw_rrect(x - w * 0.5f, y - 32, w, 64, 32, argb_alpha(TH_FOCUS, 0.35f * pulse));
            draw_rrect_outline(x - w * 0.5f, y - 32, w, 64, 32, 3, TH_FOCUS);
        }
        draw_pad_glyph((enum PadGlyph)PAD[i].glyph, x, y, 44);
    }
}

static void remap_page(uint32_t pressed)
{
    int before = S.remap_cursor;
    if (pressed & BIT(BTN_UP))
        S.remap_cursor = (S.remap_cursor + PAD_COUNT - 1) % PAD_COUNT;
    if (pressed & BIT(BTN_DOWN))
        S.remap_cursor = (S.remap_cursor + 1) % PAD_COUNT;
    if (S.remap_cursor != before)
        sfx_play(SFX_CLICK);
    const PadButton *pb = &PAD[S.remap_cursor];
    int current = ps1_index(map_target()->button_map[pb->button]);
    int step = (pressed & (BIT(BTN_RIGHT) | BIT(BTN_CROSS))) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
    if (step)
    {
        current = (current + step + PS1_COUNT) % PS1_COUNT;
        set_map(pb->button, PS1[current].button);
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_TRIANGLE))
    {
        set_map(pb->button, pb->button);
        sfx_play(SFX_CLICK);
    }
    static int preset;
    if (pressed & BIT(BTN_SQUARE))
    {
        preset = (preset + 1) % 3;
        apply_preset(preset);
        sfx_play(SFX_SELECT);
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        S.remap = false;
        app_save_settings();
        sfx_play(SFX_BACK);
        return;
    }

    draw_controller(800, 560, S.remap_cursor);

    /* the list */
    const float x = 1260, w = 600, top = 170, row_h = 50;
    draw_rrect(x - 16, top - 16, w + 32, PAD_COUNT * row_h + 32, TH_RADIUS, TH_CARD);
    float target_y = top + S.remap_cursor * row_h;
    if (S.remap_y == 0)
        S.remap_y = target_y;
    anim_approach(&S.remap_y, target_y, app.dt, TH_SNAP);
    draw_rrect(x - 4, S.remap_y, w + 8, row_h - 4, 10, TH_ROW_SELECTED);
    for (int i = 0; i < PAD_COUNT; ++i)
    {
        float y = top + i * row_h;
        const PadButton *b = &PAD[i];
        int to = ps1_index(map_target()->button_map[b->button]);
        bool changed = map_target()->button_map[b->button] != b->button;
        draw_pad_glyph((enum PadGlyph)b->glyph, x + 52, y + row_h * 0.45f, 28);
        text_draw(x + 108, y + 9, 22, FONT_REGULAR, TH_TEXT, ALIGN_LEFT, tr(b->name));
        icon_draw(ICON_ARROWS_EXCHANGE, x + 318, y + 9, 26, changed ? TH_FOCUS : TH_TEXT_DIM);
        draw_pad_glyph((enum PadGlyph)PS1[to].glyph, x + 408, y + row_h * 0.45f, 28);
        text_draw(x + 464, y + 9, 22, FONT_REGULAR, changed ? TH_FOCUS : TH_TEXT_SOFT, ALIGN_LEFT,
                  tr(PS1[to].name));
    }

    static const int glyphs[] = {GLYPH_LEFT, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Change", "Reset button", "Presets", "Back"};
    app_draw_hints(glyphs, labels, 4, NULL);
}

/* ---------------------------------------------------------------- help panel */

/* Wrapped text; returns the height used. */
static float draw_wrapped(float x, float y, float w, float size, uint32_t argb, const char *s)
{
    char line[256];
    float yy = y;
    while (*s)
    {
        size_t best = 0, i = 0;
        for (;;)
        {
            size_t j = i;
            while (s[j] && s[j] != ' ')
                ++j;
            size_t n = j < sizeof(line) - 1 ? j : sizeof(line) - 1;
            memcpy(line, s, n);
            line[n] = '\0';
            if (best > 0 && text_width(size, FONT_REGULAR, line) > w)
                break;
            best = n;
            if (!s[j])
                break;
            i = j + 1;
        }
        if (best == 0)
            best = strlen(s) < sizeof(line) - 1 ? strlen(s) : sizeof(line) - 1;
        memcpy(line, s, best);
        line[best] = '\0';
        text_draw(x, yy, size, FONT_REGULAR, argb, ALIGN_LEFT, line);
        yy += size * 1.45f;
        s += best;
        while (*s == ' ')
            ++s;
    }
    return yy - y;
}

/* Before and after for the upscale filter: a tiny sprite, enlarged. */
static void draw_filter_preview(float x, float y, float size, int filter)
{
    enum { N = 24 };
    static PlatTexture *sharp, *smooth, *xbr;
    if (!sharp)
    {
        uint32_t src[N * N], big[N * N * 4];
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
            {
                float dx = i - 11.5f, dy = j - 11.5f;
                bool disc = dx * dx + dy * dy < 90.0f, stripe = (i + j) % 8 < 3 && !disc;
                src[j * N + i] = disc ? 0xff8fb0ffu : stripe ? 0xfff0b429u : 0xff1c2250u;
            }
        uint8_t rgba[N * N * 4 * 4];
        for (int k = 0; k < N * N; ++k)
        {
            rgba[k * 4 + 0] = (src[k] >> 16) & 0xff;
            rgba[k * 4 + 1] = (src[k] >> 8) & 0xff;
            rgba[k * 4 + 2] = src[k] & 0xff;
            rgba[k * 4 + 3] = 255;
        }
        sharp = plat_texture_create(rgba, N, N, false);
        smooth = plat_texture_create(rgba, N, N, true);
        xbr2x(src, N, N, N, big);
        for (int k = 0; k < N * N * 4; ++k)
        {
            rgba[k * 4 + 0] = (big[k] >> 16) & 0xff;
            rgba[k * 4 + 1] = (big[k] >> 8) & 0xff;
            rgba[k * 4 + 2] = big[k] & 0xff;
            rgba[k * 4 + 3] = 255;
        }
        xbr = plat_texture_create(rgba, N * 2, N * 2, true);
    }
    float half = (size - 16) * 0.5f;
    plat_draw_texture(sharp, x, y, half, half, 0xffffffffu, false);
    PlatTexture *after = filter == UPSCALE_XBR ? xbr : filter == UPSCALE_SMOOTH_PIXELS ? smooth : sharp;
    plat_draw_texture(after, x + half + 16, y, half, half, 0xffffffffu, false);
    text_draw(x + half * 0.5f, y + half + 10, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("Original"));
    text_draw(x + half * 1.5f + 16, y + half + 10, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
              tr(FILTERS[filter]));
}

/* The project page as a QR code (assets/qr-github.png, one pixel per module,
 * made with segno: see tools/make_qr.py). Drawn on a white card
 * so phones read it on any background. */
static void draw_project_qr(float x, float y, float size)
{
    static PlatTexture *qr;
    static int modules;
    static bool tried;
    if (!tried)
    {
        tried = true;
        char path[PSXS5_PATH_MAX];
        plat_asset_path(path, sizeof(path), "qr-github.png");
        int w = 0, h = 0, comp = 0;
        unsigned char *px = stbi_load(path, &w, &h, &comp, 4);
        if (px)
        {
            qr = plat_texture_create(px, w, h, false);
            modules = w;
            stbi_image_free(px);
        }
    }
    if (!qr || modules <= 0)
        return;
    int scale = (int)(size / modules);
    float side = (float)(scale * modules), pad = 14;
    draw_rrect(x - pad, y - pad, side + 2 * pad, side + 2 * pad, TH_RADIUS_SMALL, 0xffffffffu);
    plat_draw_texture(qr, x, y, side, side, 0xffffffffu, false);
}

/* Any text as a QR code (the phone page's address), made on the console. */
static void draw_url_qr(const char *text, float x, float y, float size)
{
    static char made_for[128];
    static PlatTexture *tex;
    static int modules;
    if (strcmp(made_for, text) != 0)
    {
        str_copy(made_for, sizeof(made_for), text);
        plat_texture_free(tex);
        tex = NULL;
        static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], temp[qrcodegen_BUFFER_LEN_MAX];
        if (qrcodegen_encodeText(text, temp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                 qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        {
            int n = qrcodegen_getSize(qr), side = n + 4;
            uint8_t *px = malloc((size_t)side * side * 4);
            if (px)
            {
                for (int j = 0; j < side; ++j)
                    for (int i = 0; i < side; ++i)
                    {
                        bool dark = qrcodegen_getModule(qr, i - 2, j - 2);
                        uint8_t *p = &px[(j * side + i) * 4];
                        p[0] = dark ? 15 : 255, p[1] = dark ? 19 : 255, p[2] = dark ? 48 : 255, p[3] = 255;
                    }
                tex = plat_texture_create(px, side, side, false);
                modules = side;
                free(px);
            }
        }
    }
    if (!tex)
        return;
    int scale = (int)(size / modules);
    float side = (float)(scale * modules), pad = 14;
    draw_rrect(x - pad, y - pad, side + 2 * pad, side + 2 * pad, TH_RADIUS_SMALL, 0xffffffffu);
    plat_draw_texture(tex, x, y, side, side, 0xffffffffu, false);
}

static void draw_help(const Row *r)
{
    const float x = 1380, w = 476, top = 170;
    draw_rrect(x, top, w, 780, TH_RADIUS, TH_CARD);
    if (!r && tab()->rows == ABOUT)
        r = &ABOUT[0]; /* nothing to select on About: describe PSXS5 */
    if (!r)
        return;
    icon_draw(tab()->icon, x + 32, top + 32, 40, TH_FOCUS);
    text_draw_fit(x + 32, top + 92, 30, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w - 64, tr(r->name));
    float y = top + 150;
    if (r->offset == offsetof(Settings, upscale_filter))
    {
        draw_filter_preview(x + 32, y, w - 64, app.settings.upscale_filter);
        y += (w - 64 - 16) * 0.5f + 56;
    }
    y += draw_wrapped(x + 32, y, w - 64, 24, TH_TEXT_SOFT, tr(r->help)) + 20;
    static const char *const notes[] = {"Applies right away", "From the next game you start",
                                        "After PSXS5 restarts"};
    if (r->special == SP_REMOTE)
    {
        const char *url = remote_address();
        if (app.global.remote && url[0])
        {
            float qs = 264, qx = x + (w - qs) * 0.5f, qy = top + 330;
            draw_url_qr(url, qx, qy, qs);
            text_draw(x + w * 0.5f, qy + qs + 30, 22, FONT_BOLD, TH_TEXT, ALIGN_CENTER,
                      tr("Scan with your phone"));
            text_draw(x + w * 0.5f, qy + qs + 64, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, url);
        }
        else if (app.global.remote)
            text_draw(x + 32, top + 360, 22, FONT_REGULAR, TH_DANGER, ALIGN_LEFT, tr("No network connection"));
    }
    else if (tab()->rows == ABOUT)
    {
        /* About: the GitHub page, for a phone */
        float qs = 264, qx = x + (w - qs) * 0.5f, qy = top + 430;
        draw_project_qr(qx, qy, qs);
        text_draw(x + w * 0.5f, qy + qs + 30, 22, FONT_BOLD, TH_TEXT, ALIGN_CENTER,
                  tr("Scan for the GitHub page"));
        text_draw(x + w * 0.5f, qy + qs + 64, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  "github.com/SynoPiia/PSXS5");
    }
    if (r->kind != K_INFO && r->kind != K_ACTION)
    {
        icon_draw(r->apply == APPLY_NOW ? ICON_CHECK : ICON_CLOCK, x + 32, top + 720, 28, TH_FOCUS);
        text_draw(x + 72, top + 722, 22, FONT_REGULAR, TH_FOCUS, ALIGN_LEFT, tr(notes[r->apply]));
    }
    if (r->global_only && S.game_scope && app.game)
    {
        icon_draw(ICON_WORLD, x + 32, top + 670, 28, TH_TEXT_DIM);
        text_draw(x + 72, top + 672, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, tr("Same for every game"));
    }
}

/* ---------------------------------------------------------------- the screen */

static void draw_header(void)
{
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr(S.remap ? "Button mapping" : "Settings"));
    char sub[200];
    if (app.game)
        snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s", app.game->title,
                 tr(S.game_scope ? "This game" : "All games"));
    else
        str_copy(sub, sizeof(sub), tr("Applies to every game"));
    text_draw_fit(TH_MARGIN + 2, 100, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, 1000, sub);

    if (!app.game)
        return;
    /* scope switch, top right */
    const char *a = tr("This game"), *b = tr("All games");
    float wa = text_width(22, FONT_REGULAR, a) + 40, wb = text_width(22, FONT_REGULAR, b) + 40;
    float right = plat_width() - TH_MARGIN, hintw = pad_glyph_width(GLYPH_L2, 26) * 2 + 30;
    float xb = right - hintw - wb, xa = xb - wa - 6, y = 46;
    float target = S.game_scope ? xa : xb;
    if (S.scope_x == 0)
        S.scope_x = target;
    anim_approach(&S.scope_x, target, app.dt, TH_SNAP);
    draw_rrect(xa - 6, y - 6, wa + wb + 18, 58, 29, 0xc01c2250u);
    draw_rrect(S.scope_x, y, S.game_scope ? wa : wb, 46, 23, TH_TEXT);
    text_draw(xa + 20, y + 11, 22, FONT_REGULAR, S.game_scope ? TH_BG : TH_HINT, ALIGN_LEFT, a);
    text_draw(xb + 20, y + 11, 22, FONT_REGULAR, S.game_scope ? TH_HINT : TH_BG, ALIGN_LEFT, b);
    float gx = right - hintw + 16;
    gx += draw_pad_glyph(GLYPH_L2, gx + pad_glyph_width(GLYPH_L2, 26) * 0.5f, y + 23, 26) + 6;
    draw_pad_glyph(GLYPH_R2, gx + pad_glyph_width(GLYPH_R2, 26) * 0.5f, y + 23, 26);
}

static void draw_tabs(void)
{
    const float x = TH_MARGIN, w = 300, top = 170, h = 66;
    float target = top + S.tab * (h + 6);
    if (S.tab_y == 0)
        S.tab_y = target;
    anim_approach(&S.tab_y, target, app.dt, TH_SNAP);
    draw_rrect(x, S.tab_y, w, h, TH_RADIUS_SMALL, TH_ROW_SELECTED);
    for (int i = 0; i < TAB_COUNT; ++i)
    {
        float y = top + i * (h + 6);
        bool on = i == S.tab;
        icon_draw(TABS[i].icon, x + 20, y + 17, 32, on ? TH_TEXT : TH_HINT);
        text_draw(x + 70, y + 18, 26, on ? FONT_BOLD : FONT_REGULAR, on ? TH_TEXT : TH_HINT, ALIGN_LEFT,
                  tr(TABS[i].name));
    }
}

static void draw_rows(void)
{
    const float x = 410, w = 940, top = 170, bottom = 960, row_h = 78, cap_h = 50;
    const Tab *t = tab();
    /* layout pass: row positions in content space */
    float ys[32], y = 0, sel_top = 0;
    for (int i = 0; i < t->count && i < 32; ++i)
    {
        if (t->rows[i].group)
            y += (i ? 22 : 0) + cap_h;
        ys[i] = y;
        if (i == S.cursor)
            sel_top = y;
        y += row_h;
    }
    float content = y + 8;
    /* keep the selected row in view */
    float view = bottom - top, want = S.scroll;
    if (S.cursor >= 0)
    {
        if (sel_top - want < 60)
            want = sel_top - 60;
        if (sel_top + row_h - want > view - 20)
            want = sel_top + row_h - view + 20;
    }
    if (want > content - view)
        want = content - view;
    if (want < 0)
        want = 0;
    anim_approach(&S.scroll, want, app.dt, TH_SNAP);

    plat_set_clip((int)x - 8, (int)top - 8, (int)w + 16, (int)(bottom - top) + 16);
    /* cards behind each group */
    for (int i = 0; i < t->count; ++i)
    {
        if (!t->rows[i].group)
            continue;
        int last = i;
        while (last + 1 < t->count && !t->rows[last + 1].group)
            ++last;
        float cy = top + ys[i] - S.scroll;
        text_draw(x + 8, cy - cap_h + 12, 22, FONT_BOLD, TH_TEXT_DIM, ALIGN_LEFT, tr(t->rows[i].group));
        draw_rrect(x, cy - 6, w, (ys[last] - ys[i]) + row_h + 12, TH_RADIUS, TH_CARD);
    }
    /* the highlight slides between rows */
    if (S.cursor >= 0)
    {
        float target = top + sel_top - S.scroll;
        if (S.sel_y < 0)
            S.sel_y = target;
        anim_approach(&S.sel_y, target, app.dt, TH_SNAP);
        draw_rrect(x + 6, S.sel_y, w - 12, row_h - 6, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rrect_outline(x + 6, S.sel_y, w - 12, row_h - 6, TH_RADIUS_SMALL, 3, TH_FOCUS);
    }
    for (int i = 0; i < t->count; ++i)
    {
        const Row *r = &t->rows[i];
        float ry = top + ys[i] - S.scroll;
        if (ry > bottom || ry + row_h < top - 60)
            continue;
        bool sel = i == S.cursor;
        bool dimmed = r->global_only && S.game_scope && app.game;
        text_draw(x + 30, ry + 21, 28, FONT_REGULAR, dimmed && !sel ? TH_TEXT_DIM : TH_TEXT, ALIGN_LEFT,
                  tr(r->name));
        if (dimmed)
            icon_draw(ICON_WORLD, x + 40 + text_width(28, FONT_REGULAR, tr(r->name)), ry + 24, 24,
                      TH_TEXT_DIM);
        float right = x + w - 28;
        char buf[PSXS5_PATH_MAX + 64];
        switch (r->kind)
        {
        case K_CHOICE:
            draw_choice(right, ry + 15, 46, 24, sel ? 0xff3a4590u : TH_PILL, TH_TEXT_SOFT,
                        value_label(r, buf, sizeof(buf)));
            break;
        case K_TOGGLE:
        {
            float *st = &S.switch_t[i < 16 ? i : 15];
            anim_approach(st, (float)read_value(r), app.dt, TH_SNAP);
            draw_switch(right - 40 * 2.1f, ry + 18, 40, *st);
            break;
        }
        case K_ACTION:
            icon_draw(ICON_CHEVRON_RIGHT, right - 36, ry + 21, 34, TH_TEXT_SOFT);
            if (r->special == SP_UPDATE)
                text_draw_fit(right - 50, ry + 24, 22, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, w - 520,
                              value_label(r, buf, sizeof(buf)));
            break;
        case K_INFO:
            text_draw_fit(right, ry + 24, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, w - 420,
                          value_label(r, buf, sizeof(buf)));
            break;
        }
    }
    plat_set_clip(0, 0, 0, 0);
}

static void activate(const Row *r)
{
    switch (r->special)
    {
    case SP_REMAP:
        S.remap = true;
        S.remap_cursor = 0;
        S.remap_y = 0;
        sfx_play(SFX_SELECT);
        return;
    case SP_MEMCARDS:
        sfx_play(SFX_SELECT);
        memcards_open(SCREEN_SETTINGS);
        return;
    case SP_UPDATE:
        sfx_play(SFX_SELECT);
        if (update_state() == UPDATE_AVAILABLE)
            update_install();
        else
            update_check();
        return;
    case SP_WHITELIST:
    {
        /* one line appended to PS5SX2 Helper's list; nothing else changes */
        static const char *const list = "/data/whitelist.txt";
        FILE *f = fopen(list, "r");
        if (!f)
        {
            app_toast("No /data/whitelist.txt: PS5SX2 Helper isn't installed");
            return;
        }
        char line[64];
        bool present = false, ends_newline = true;
        while (fgets(line, sizeof(line), f))
        {
            size_t n = strlen(line);
            ends_newline = n && line[n - 1] == '\n';
            line[strcspn(line, "\r\n")] = '\0';
            present |= strcmp(line, PSXS5_TITLE_ID) == 0;
        }
        fclose(f);
        if (present)
        {
            app_toast("PSXS5 is already in the whitelist");
            return;
        }
        f = fopen(list, "a");
        bool ok = f && fprintf(f, "%s%s\n", ends_newline ? "" : "\n", PSXS5_TITLE_ID) > 0;
        if (f && fclose(f) != 0)
            ok = false;
        psxs5_log("whitelist: %s %s", PSXS5_TITLE_ID, ok ? "added to /data/whitelist.txt" : "could not be added");
        app_toast(ok ? "Added: reload PS5SX2 Helper or restart the console" : "Could not write /data/whitelist.txt");
        sfx_play(ok ? SFX_SELECT : SFX_BACK);
        return;
    }
    case SP_RESCAN:
        if (app.game)
        {
            app_toast("Quit the game first");
            return;
        }
        if (app.storage_error[0])
            return;
        app_rescan();
        char msg[64];
        snprintf(msg, sizeof(msg), tr(app.library.count == 1 ? "Found %d game" : "Found %d games"),
                 app.library.count);
        app_toast(msg);
        sfx_play(SFX_SELECT);
        return;
    default:
        return;
    }
}

static void set_scope(bool game)
{
    if (!app.game || game == S.game_scope)
        return;
    if (game && !app.game_has_own)
    {
        /* this game's own settings start as a copy of the console-wide ones */
        app.game_has_own = true;
        app_toast("This game now has its own settings");
    }
    S.game_scope = game;
    sfx_play(SFX_CLICK);
}

void settings_screen(uint32_t pressed)
{
    if (app.game)
        app_draw_game(45);
    else
        shelf_backdrop();
    if (app.game)
        draw_rect(0, 0, plat_width(), plat_height(), 0x900a0d24u);
    draw_header();
    draw_tabs();

    if (S.remap)
    {
        remap_page(pressed);
        app_draw_toast();
        return;
    }
    if (S.cursor < 0 || S.cursor >= tab()->count)
        first_selectable();

    /* ------------------------------------------------ input */
    int tab_step = (pressed & BIT(BTN_R1)) ? 1 : (pressed & BIT(BTN_L1)) ? -1 : 0;
    if (tab_step)
    {
        S.tab = (S.tab + tab_step + TAB_COUNT) % TAB_COUNT;
        first_selectable();
        S.scroll = 0;
        S.sel_y = -1;
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_L2))
        set_scope(true);
    if (pressed & BIT(BTN_R2))
        set_scope(false);
    const Tab *t = tab();
    int row_step = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
    if (row_step && S.cursor >= 0)
    {
        int c = S.cursor;
        do
            c = (c + row_step + t->count) % t->count;
        while (!selectable(&t->rows[c]) && c != S.cursor);
        if (c != S.cursor)
        {
            S.cursor = c;
            sfx_play(SFX_CLICK);
        }
    }
    const Row *r = S.cursor >= 0 ? &t->rows[S.cursor] : NULL;
    if (r)
    {
        int delta = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
        if ((pressed & BIT(BTN_CROSS)) && (r->kind == K_CHOICE || r->kind == K_TOGGLE))
            delta = 1;
        if (delta && (r->kind == K_CHOICE || r->kind == K_TOGGLE))
        {
            int count = r->kind == K_TOGGLE ? 2 : r->count;
            write_value(r, (read_value(r) + count + delta) % count);
            sfx_play(SFX_CLICK); /* also previews a new interface sound */
        }
        if ((pressed & BIT(BTN_CROSS)) && r->kind == K_ACTION)
            activate(r);
        if (pressed & BIT(BTN_TRIANGLE))
        {
            default_value(r);
            sfx_play(SFX_CLICK);
        }
    }
    if ((pressed & BIT(BTN_SQUARE)) && app.game && app.game_has_own)
    {
        /* back to the console-wide settings for this game */
        char path[PSXS5_PATH_MAX];
        app_game_config_path(path, sizeof(path), app.game);
        remove(path);
        app.game_has_own = false;
        app.settings = app.global;
        host_apply_settings(&app.settings);
        S.game_scope = false;
        app_toast("This game uses the settings for all games again");
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        app_save_settings();
        sfx_play(SFX_BACK);
        app.screen = app.settings_return;
        return;
    }

    draw_rows();
    draw_help(r);
    if (app.game && app.game_has_own && S.game_scope)
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE};
        static const char *const labels[] = {"Change", "Back", "Default", "Use all-games settings"};
        app_draw_hints(glyphs, labels, 4, "L1 / R1  Tabs");
    }
    else
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE};
        static const char *const labels[] = {"Change", "Back", "Default"};
        app_draw_hints(glyphs, labels, 3, "L1 / R1  Tabs");
    }
    app_draw_toast();
}

/* ---------------------------------------------------------------- the phone page */

static size_t json_text(char *out, size_t size, size_t at, const char *s)
{
    if (at + 2 >= size)
        return at;
    out[at++] = '"';
    for (; *s && at + 8 < size; ++s)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
        {
            out[at++] = '\\';
            out[at++] = (char)c;
        }
        else if (c < 0x20)
            at += (size_t)snprintf(out + at, size - at, "\\u%04x", c);
        else
            out[at++] = (char)c;
    }
    out[at++] = '"';
    out[at] = '\0';
    return at;
}

#define PUT(...) (at += (size_t)snprintf(out + at, at < size ? size - at : 0, __VA_ARGS__))

/* Every tab and setting the phone page shows, with values, in the menus' language. */
int settings_json(char *out, size_t size)
{
    size_t at = 0;
    out[0] = '\0';
    PUT("{\"game\":");
    at = json_text(out, size, at, app.game ? app.game->title : tr("Applies to every game"));
    PUT(",\"tabs\":[");
    for (int t = 0; t < TAB_COUNT && at < size; ++t)
    {
        PUT("%s{\"name\":", t ? "," : "");
        at = json_text(out, size, at, tr(TABS[t].name));
        PUT(",\"rows\":[");
        bool first = true;
        for (int i = 0; i < TABS[t].count && at < size; ++i)
        {
            const Row *r = &TABS[t].rows[i];
            if (r->kind == K_ACTION)
                continue; /* menus and pages stay on the console */
            PUT("%s{\"key\":\"%d.%d\",\"name\":", first ? "" : ",", t, i);
            first = false;
            at = json_text(out, size, at, tr(r->name));
            if (r->group)
            {
                PUT(",\"group\":");
                at = json_text(out, size, at, tr(r->group));
            }
            PUT(",\"help\":");
            at = json_text(out, size, at, tr(r->help));
            if (r->kind == K_INFO)
            {
                char buf[PSXS5_PATH_MAX + 64];
                PUT(",\"kind\":\"info\",\"text\":");
                at = json_text(out, size, at, value_label(r, buf, sizeof(buf)));
            }
            else
            {
                PUT(",\"kind\":\"%s\",\"value\":%d,\"labels\":[", r->kind == K_TOGGLE ? "toggle" : "choice",
                    read_value(r));
                int count = r->kind == K_TOGGLE ? 2 : r->count;
                for (int v = 0; v < count && at < size; ++v)
                {
                    PUT("%s", v ? "," : "");
                    at = json_text(out, size, at, r->special == SP_LANGUAGE ? i18n_name(v) : tr(r->labels[v]));
                }
                PUT("]");
            }
            PUT("}");
        }
        PUT("]}");
    }
    PUT("]}");
    return (int)(at < size ? at : size - 1);
}

/* A change from the phone: "tab.row" and the new value. Returns the setting's
 * name, or NULL when the key is unknown. */
const char *settings_set_by_key(const char *key, int value)
{
    int t = -1, i = -1;
    if (sscanf(key, "%d.%d", &t, &i) != 2 || t < 0 || t >= TAB_COUNT || i < 0 || i >= TABS[t].count)
        return NULL;
    const Row *r = &TABS[t].rows[i];
    if (r->kind != K_CHOICE && r->kind != K_TOGGLE)
        return NULL;
    int count = r->kind == K_TOGGLE ? 2 : r->count;
    if (value < 0 || value >= count)
        return NULL;
    /* the phone edits what the console's settings would: this game's own when it has them */
    bool scope = S.game_scope;
    S.game_scope = app.game && app.game_has_own;
    write_value(r, value);
    S.game_scope = scope;
    return r->name;
}
