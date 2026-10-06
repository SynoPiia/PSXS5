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
#define PSXS5_VERSION "0.1.0"
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
#define PSXS5_MAX_PADS 2

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
    int internal_res; /* 1 or 2: the core renders 3D at 2x (enhanced GPU) */
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
    bool ui_sounds;   /* click when browsing the shelf */
} Settings;

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
const char *path_ext(const char *path);  /* lower-case-insensitive extension without dot, "" if none */
int str_icmp(const char *a, const char *b);

#endif
