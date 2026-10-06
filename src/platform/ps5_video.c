/*
 * PSXS5 - PS5 screen output without SDL's video driver.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PacBrew's SDL2 PS5 video driver can't hand out a window surface (its
 * CreateWindowFramebuffer leaves format/pixels/pitch unset, which SDL turns
 * into "Out of memory") and registers no render driver. PSXS5 therefore
 * draws with SDL's software renderer into its own linear buffer and shows
 * it here, using the same VideoOut sequence as SDL's driver init and the
 * boilerplate demo: two direct-memory framebuffers in the 64 KiB tiled
 * layout, filled by the CPU, flushed, flipped.
 */
#if defined(__PROSPERO__)
#include "ps5_video.h"

#include <stdint.h>
#include <string.h>

size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t length,
                                  size_t alignment, int memory_type, int64_t *physical_address);
int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                             int64_t physical_address, size_t alignment);
int sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void *param);
int sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
int sceVideoOutSubmitFlip(int32_t handle, int32_t buffer_index, uint32_t flip_mode,
                          int64_t flip_argument);
int sceVideoOutWaitVblank(int32_t handle);

typedef struct
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
} VideoBuffer;

typedef struct
{
    uint8_t reserved[80];
} VideoAttribute;

void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute, uint64_t pixel_format,
                                    uint32_t tiling_mode, uint32_t width, uint32_t height,
                                    uint64_t option, uint32_t dcc_control, uint64_t dcc_clear_color);
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set_index, int32_t buffer_index_start,
                                VideoBuffer *buffers, int32_t buffer_count,
                                VideoAttribute *attribute, int32_t category, void *option);

#define FRAME_BYTES 0x1000000u /* one 1920x1080 tiled frame, rounded up */
#define MEMORY_ALIGN 0x200000u
#define MEMORY_TYPE_WC_GARLIC 3
#define MAP_CPU_GPU_RW 0x33
#define PIXEL_FORMAT_RGBA8_SRGB 0x8000000022000000ull

static int handle = -1;
static uint8_t *frames[2];
static int back = 1;
static uint64_t flip_count = 1;
/* Tiled offset = 64 KiB block base + (row bits XOR column bits) inside a 128x128 block. */
static uint32_t row_bits[128], col_bits[128];

static void build_swizzle(void)
{
    for (uint32_t y = 0; y < 128; ++y)
        row_bits[y] = ((y << 4) & 0x70u) ^ ((y << 5) & 0xf00u) ^ ((y << 9) & 0x1000u) ^
                      ((y << 8) & 0x4000u);
    for (uint32_t x = 0; x < 128; ++x)
        col_bits[x] = ((x << 2) & 0xcu) ^ ((x << 5) & 0x380u) ^ ((x << 4) & 0x400u) ^
                      ((x << 6) & 0x800u) ^ ((x << 9) & 0xa000u);
}

bool ps5_video_open(char *error, size_t size)
{
    build_swizzle();
    handle = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (handle < 0)
    {
        snprintf(error, size, "sceVideoOutOpen failed (0x%x)", (unsigned)handle);
        return false;
    }
    const size_t total = (size_t)FRAME_BYTES * 2;
    int64_t physical = 0;
    int rc = sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), total,
                                           MEMORY_ALIGN, MEMORY_TYPE_WC_GARLIC, &physical);
    if (rc < 0)
    {
        snprintf(error, size, "direct memory allocation failed (0x%x)", (unsigned)rc);
        return false;
    }
    void *mapped = NULL;
    rc = sceKernelMapDirectMemory(&mapped, total, MAP_CPU_GPU_RW, 0, physical, MEMORY_ALIGN);
    if (rc < 0)
    {
        snprintf(error, size, "direct memory mapping failed (0x%x)", (unsigned)rc);
        return false;
    }
    frames[0] = mapped;
    frames[1] = (uint8_t *)mapped + FRAME_BYTES;
    memset(mapped, 0, total);

    VideoBuffer buffers[2] = {{frames[0], NULL, NULL, NULL}, {frames[1], NULL, NULL, NULL}};
    VideoAttribute attribute;
    memset(&attribute, 0, sizeof(attribute));
    sceVideoOutSetFlipRate(handle, 0);
    sceVideoOutSetBufferAttribute2(&attribute, PIXEL_FORMAT_RGBA8_SRGB, 0, PS5_SCREEN_W,
                                   PS5_SCREEN_H, 0, 0, 0);
    rc = sceVideoOutRegisterBuffers2(handle, 0, 0, buffers, 2, &attribute, 0, NULL);
    if (rc < 0)
    {
        snprintf(error, size, "sceVideoOutRegisterBuffers2 failed (0x%x)", (unsigned)rc);
        return false;
    }
    sceVideoOutSubmitFlip(handle, 0, 1, flip_count++);
    return true;
}

void ps5_video_present(const uint32_t *pixels, size_t pitch_bytes)
{
    if (handle < 0)
        return;
    uint8_t *dst = frames[back];
    const uint32_t blocks_per_row = (PS5_SCREEN_W + 127) / 128;
    for (uint32_t y = 0; y < PS5_SCREEN_H; ++y)
    {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)pixels + (size_t)y * pitch_bytes);
        const size_t block_row = (size_t)(y >> 7) * blocks_per_row;
        const uint32_t rb = row_bits[y & 127];
        for (uint32_t x = 0; x < PS5_SCREEN_W; ++x)
        {
            size_t offset = ((block_row + (x >> 7)) << 16) + (rb ^ col_bits[x & 127]);
            *(uint32_t *)(dst + offset) = src[x];
        }
    }
    /* The framebuffers are write-combined (WC_GARLIC): writes bypass the
     * cache, so a store fence is enough; no 8 MB clflush walk per frame. */
    __asm__ volatile("sfence" ::: "memory");
    sceVideoOutSubmitFlip(handle, back, 1, (int64_t)flip_count++);
    sceVideoOutWaitVblank(handle); /* paces the UI at the display rate */
    back ^= 1;
}
#endif
