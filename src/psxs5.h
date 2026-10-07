/*
 * PSXS5 - PlayStation X Super 5
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared types for the frontend. Everything here is plain C11 so the same
 * sources build for the PS5 (no full libc++) and for the desktop test build.
 */
#ifndef PSXS5_H
#define PSXS5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PSXS5_NAME "PSXS5"
#define PSXS5_TITLE_ID "PPSA97510"
#define PSXS5_VERSION "1.2.0"
#define PSXS5_PATH_MAX 512

/* Pad bits use RetroPad numbering so the host can hand the mask to the core
 * unchanged. PlayStation names: Cross = B, Circle = A, Square = Y, Triangle = X. */
enum
{
    BTN_CROSS = 0,
    BTN_SQUARE = 1,
    BTN_SELECT = 2,
    BTN_START = 3,
    BTN_UP = 4,
    BTN_DOWN = 5,
    BTN_LEFT = 6,
    BTN_RIGHT = 7,
    BTN_CIRCLE = 8,
    BTN_TRIANGLE = 9,
    BTN_L1 = 10,
    BTN_R1 = 11,
    BTN_L2 = 12,
    BTN_R2 = 13,
    BTN_L3 = 14,
    BTN_R3 = 15,
    BTN_MENU = 16, /* touchpad click: opens the PSXS5 in-game menu */
};
#define BIT(b) (1u << (b))
#define PSXS5_MAX_PADS 4 /* 3 and 4 play through a multitap */

typedef struct
{
    bool connected;
    uint32_t buttons;
    int16_t lx, ly, rx, ry;
} PadState;

enum AspectMode
{
    ASPECT_AUTO = 0, /* what the game/core reports (normally 4:3) */
    ASPECT_4_3,
    ASPECT_16_9,     /* pair with a widescreen cheat for true widescreen */
    ASPECT_16_10,
    ASPECT_PIXEL,    /* 1:1 square pixels */
    ASPECT_STRETCH,  /* fill the whole screen */
    ASPECT_COUNT
};

enum UpscaleFilter
{
    UPSCALE_SHARP = 0, /* nearest-neighbour prescale: crisp pixels */
    UPSCALE_SMOOTH_PIXELS, /* Scale2x/Scale3x edge smoothing */
    UPSCALE_XBR,           /* xBR: edge-directed, smoothest (2x passes) */
    UPSCALE_FILTER_COUNT
};

enum RegionMode
{
    REGION_AUTO = 0,
    REGION_NTSC,
    REGION_PAL,
    REGION_COUNT
};

typedef struct
{
    int aspect;       /* enum AspectMode */
    bool integer_scale; /* whole-number scale factors only */
    bool smooth;      /* bilinear for the final scale to the screen */
    int internal_res; /* 1..5 = native, 2x, 4x, 8x, 16x; PCSX-ReARMed stops at 2x */
    int upscale;      /* 1..4: prescale before the final scale */
    int upscale_filter; /* enum UpscaleFilter */
    bool show_fps;
    int region;       /* enum RegionMode */
    bool force_hle;   /* ignore BIOS files and use the built-in HLE BIOS */
    bool dithering;
    bool cd_fast;     /* faster CD reads (shorter loads, rare glitches) */
    bool analog;      /* DualShock instead of digital pad */
    int state_slot;   /* 0..9 */
    int last_game;    /* library cursor */
    int cover_style;  /* enum CoverStyle */
    bool cover_download; /* fetch missing covers over the network */
    int ui_sound;     /* SFX_STYLE_* in ui/sfx.h; SFX_STYLE_OFF mutes */
    int ui_volume;    /* 0..3 = 25/50/75/100 % */
    int stick_dpad;   /* enum StickDpad */
    int language;     /* enum Lang in i18n.h */
    bool rumble;          /* controller vibration */
    int rumble_strength;  /* 0..3 = 25/50/75/100 % */
    /* Button mapping: for each controller button (BTN_CROSS..BTN_R3), the PS1
     * button it presses (BTN_*), or -1 for nothing. */
    int8_t button_map[16];
    int sort_mode;        /* shelf order, enum SortMode in ui/coverflow.h */
    int shelf_category;   /* shelf filter, enum ShelfCategory in ui/coverflow.h */
    int background;       /* 0 dark, 1 the selected cover's colour */
    bool widescreen;      /* turn on the game's widescreen code, show 16:9 */
    bool multitap;        /* 4 players through a multitap in port 1 */
    bool rewind;          /* keep the last seconds for rewinding */
    bool quick_resume;    /* save on quitting, offer Continue on the shelf */
    int crt;              /* scanlines: 0 off, 1 light, 2 strong */
    int border;           /* around the picture: 0 black, 1 glow, 2 TV frame */
    bool remote;          /* settings page for phones on the local network */
    bool update_check;    /* look for new PSXS5 releases at start */
    int emulator;         /* enum Emulator */
    bool pgxp;            /* Beetle: precise geometry, no wobbling polygons */
} Settings;

enum Emulator
{
    EMU_AUTO,   /* Beetle when it can run the game, else PCSX-ReARMed */
    EMU_PCSX,
    EMU_BEETLE,
    EMU_COUNT
};

enum StickDpad
{
    STICK_DPAD_AUTO = 0, /* left stick drives the D-pad while the game uses digital mode */
    STICK_DPAD_ALWAYS,
    STICK_DPAD_OFF,
    STICK_DPAD_COUNT
};

enum CoverStyle
{
    COVER_FLAT = 0, /* front art, shown in perspective by PSXS5 */
    COVER_BOX3D,    /* pre-rendered 3D jewel case */
    COVER_STYLE_COUNT
};

/* Data layout below the PSXS5 root (default /data/PSXS5 on PS5). */
typedef struct
{
    char root[PSXS5_PATH_MAX];
    char games[PSXS5_PATH_MAX];
    char bios[PSXS5_PATH_MAX];
    char saves[PSXS5_PATH_MAX];
    char states[PSXS5_PATH_MAX];
    char cheats[PSXS5_PATH_MAX];
    char covers[PSXS5_PATH_MAX]; /* covers/default/<serial>.jpg, covers/3d/<serial>.png */
    char logs[PSXS5_PATH_MAX];
    char config[PSXS5_PATH_MAX];
} Paths;

void psxs5_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void psxs5_log_open(const char *path);

/* Small string helpers shared by the frontend. */
void str_copy(char *dst, size_t size, const char *src);
void path_join(char *dst, size_t size, const char *a, const char *b);
bool path_exists(const char *path);
bool path_is_dir(const char *path);
bool make_dirs(const char *path);
bool file_copy(const char *from, const char *to); /* replaces `to` */
const char *path_ext(const char *path);  /* lower-case-insensitive extension without dot, "" if none */
int str_icmp(const char *a, const char *b);
/* localtime into *out (the PS5 libc has no localtime_r); false on failure */
struct tm;
bool local_time(long long when, struct tm *out);

#endif
