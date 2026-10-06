/*
 * PSXS5 - platform layer (SDL2 on both PS5 and desktop).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_PLATFORM_H
#define PSXS5_PLATFORM_H

#include "../psxs5.h"

bool plat_init(void);
const char *plat_init_error(void); /* "SDL window failed: <reason>" after plat_init fails */
void plat_shutdown(void);

/* PS5: asks the HEN to let PSXS5 list /data. Call before plat_init, while the
 * process is single-threaded. Returns false (with the reason) when PSXS5 stays
 * sandboxed: files still open and save, but folders can't be listed, so the
 * library comes from the index the sync tool writes. Desktop: always true. */
bool plat_prepare_storage(char *error, size_t size);
void plat_default_root(char *out, size_t size);

/* Reads controllers. `quit` is set when the desktop window is closed. */
void plat_poll(PadState pads[PSXS5_MAX_PADS], bool *quit);
void plat_rumble(int port, uint16_t strong, uint16_t weak);

/* Audio: interleaved signed 16-bit stereo. */
bool plat_audio_open(int sample_rate);
void plat_audio_close(void);
void plat_audio_push(const int16_t *frames, size_t frame_count);
size_t plat_audio_queued_frames(void);
void plat_audio_clear(void);

/* Drawing in output pixels (1920x1080 on PS5). */
int plat_width(void);
int plat_height(void);
void plat_begin_frame(uint32_t clear_argb);
/* pixel_format: 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565 (libretro numbering). */
/* upscale 1..4 prescales 32-bit frames with `filter` (enum UpscaleFilter) before the final scale. */
void plat_upload_game(const void *pixels, int width, int height, size_t pitch, int pixel_format,
                      int upscale, int filter);
void plat_draw_game(const Settings *settings, float display_aspect, uint8_t dim);
void plat_fill_rect(int x, int y, int w, int h, uint32_t argb);
void plat_end_frame(void);

/* Textures and textured triangle meshes: the UI (covers in perspective,
 * reflections, gradients, TrueType text) is built from these. */
typedef struct PlatTexture PlatTexture;
typedef struct
{
    float x, y;    /* output pixels */
    float u, v;    /* 0..1 */
    uint32_t argb; /* vertex colour, multiplied with the texture */
} PlatVertex;

PlatTexture *plat_texture_create(const uint8_t *rgba, int width, int height, bool smooth);
void plat_texture_free(PlatTexture *texture);
void plat_texture_size(const PlatTexture *texture, int *width, int *height);
/* texture may be NULL for flat-coloured geometry. indices may be NULL for a plain triangle list. */
void plat_draw_mesh(PlatTexture *texture, const PlatVertex *vertices, int vertex_count,
                    const int *indices, int index_count);
/* Full-screen-relative path to packaged read-only assets (/app0/assets on PS5). */
void plat_asset_path(char *out, size_t size, const char *relative);

uint64_t plat_ticks_us(void);
void plat_sleep_us(uint32_t us);
void plat_notify(const char *message); /* PS5 system notification; desktop log */

#endif
