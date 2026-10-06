/*
 * PSXS5 - cover art: local lookup, background download and decoding.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lookup order for a game:
 *   1. cover.png / cover.jpg in the game's own folder (your choice wins)
 *   2. <root>/covers/default/<serial>.jpg or covers/3d/<serial>.png
 *   3. download from xlenore/psx-covers into (2), when enabled
 *   4. fallback-cover.png beside the game (picked up by the sync tool)
 *   5. a generated title card
 * One worker thread does the file I/O, downloads and JPEG/PNG decoding; the
 * main thread only uploads finished pixels, at most a few per frame.
 */
#include "covers.h"

#include "net.h"
#include "stb_image.h"
#include "ui/text.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEEP_RADIUS 14  /* textures kept around the cursor */
#define LOAD_RADIUS 9   /* covers requested around the cursor */
#define MAX_HEIGHT 640  /* decoded covers are scaled down to this */
#define QUEUE_SIZE 64

typedef enum
{
    SLOT_EMPTY,
    SLOT_QUEUED,
    SLOT_READY, /* pixels decoded, waiting for upload */
    SLOT_LOADED,
} SlotState;

typedef struct
{
    SlotState state;
    PlatTexture *texture;
    bool placeholder; /* texture is a title card, real art may still arrive */
    uint8_t *pixels;
    int w, h;
    /* copies, so the worker never touches the Library */
    char title[96];
    char serial[16];
    char folder[PSXS5_PATH_MAX];
} Slot;

static Slot *slots;
static int slot_count;
static Paths paths;
static int style;
static bool download_enabled;
static int network_failures;

static SDL_Thread *worker;
static SDL_mutex *lock;
static SDL_cond *wake;
static bool quit;
static int queue[QUEUE_SIZE];
static int queue_len;
static int downloading;

void covers_url(char *out, size_t size, int cover_style, const char *serial)
{
    snprintf(out, size, "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/%s/%s.%s",
             cover_style == COVER_BOX3D ? "3d" : "default", serial,
             cover_style == COVER_BOX3D ? "png" : "jpg");
}

static void cache_path(char *out, size_t size, const char *serial)
{
    char file[64];
    snprintf(file, sizeof(file), "%s/%s.%s", style == COVER_BOX3D ? "3d" : "default", serial,
             style == COVER_BOX3D ? "png" : "jpg");
    path_join(out, size, paths.covers, file);
}

/* ---------------------------------------------------------------- worker */

static uint8_t *downscale(uint8_t *src, int *w, int *h)
{
    if (*h <= MAX_HEIGHT)
        return src;
    int nh = MAX_HEIGHT, nw = *w * MAX_HEIGHT / *h;
    uint8_t *dst = malloc((size_t)nw * nh * 4);
    if (!dst)
        return src;
    /* box filter: average the source pixels covering each destination pixel */
    for (int y = 0; y < nh; ++y)
    {
        int sy0 = y * *h / nh, sy1 = (y + 1) * *h / nh;
        for (int x = 0; x < nw; ++x)
        {
            int sx0 = x * *w / nw, sx1 = (x + 1) * *w / nw;
            unsigned sum[4] = {0, 0, 0, 0}, n = 0;
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx, ++n)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += src[((size_t)sy * *w + sx) * 4 + c];
            for (int c = 0; c < 4; ++c)
                dst[((size_t)y * nw + x) * 4 + c] = (uint8_t)(n ? sum[c] / n : 0);
        }
    }
    stbi_image_free(src);
    *w = nw;
    *h = nh;
    return dst;
}

static uint8_t *title_card(const char *title, int *w, int *h)
{
    *w = 480;
    *h = 480;
    uint8_t *px = malloc((size_t)*w * *h * 4);
    if (!px)
        return NULL;
    for (int y = 0; y < *h; ++y)
        for (int x = 0; x < *w; ++x)
        {
            uint8_t *p = &px[((size_t)y * *w + x) * 4];
            float t = (float)y / *h;
            p[0] = (uint8_t)(30 + 12 * t);
            p[1] = (uint8_t)(36 + 10 * t);
            p[2] = (uint8_t)(78 + 30 * t);
            p[3] = 255;
            if (x < 6 || y < 6 || x >= *w - 6 || y >= *h - 6)
                p[0] = 70, p[1] = 86, p[2] = 170; /* frame */
        }
    text_render_rgba(px, *w, *h, 30, 30, FONT_BOLD, 0xff9fb4ffu, *w - 60, "PSXS5");
    text_render_rgba(px, *w, *h, 170, 46, FONT_BOLD, 0xffffffffu, *w - 70, title);
    return px;
}

static uint8_t *load_image(const char *path, int *w, int *h)
{
    int comp;
    uint8_t *px = stbi_load(path, w, h, &comp, 4);
    return px ? downscale(px, w, h) : NULL;
}

/* Finds or fetches the art for one slot. Runs on the worker thread. */
static uint8_t *produce(const Slot *job, int *w, int *h, bool *placeholder)
{
    static const char *local[] = {"cover.png", "cover.jpg", "cover.jpeg"};
    char path[PSXS5_PATH_MAX];
    *placeholder = false;
    for (size_t i = 0; i < 3 && job->folder[0]; ++i)
    {
        path_join(path, sizeof(path), job->folder, local[i]);
        uint8_t *px = path_exists(path) ? load_image(path, w, h) : NULL;
        if (px)
            return px;
    }
    if (job->serial[0])
    {
        cache_path(path, sizeof(path), job->serial);
        if (!path_exists(path) && download_enabled && network_failures < 3)
        {
            char url[256], dir[PSXS5_PATH_MAX];
            covers_url(url, sizeof(url), style, job->serial);
            str_copy(dir, sizeof(dir), path);
            *strrchr(dir, '/') = '\0';
            make_dirs(dir);
            NetResult r = net_download(url, path);
            if (r == NET_UNAVAILABLE && ++network_failures == 3)
                psxs5_log("covers: network unavailable, downloads paused");
            if (r == NET_OK)
                network_failures = 0;
        }
        uint8_t *px = path_exists(path) ? load_image(path, w, h) : NULL;
        if (px)
            return px;
    }
    /* art found beside the game files by tools/psxs5_sync.py: better than nothing */
    if (job->folder[0])
    {
        path_join(path, sizeof(path), job->folder, "fallback-cover.png");
        uint8_t *px = path_exists(path) ? load_image(path, w, h) : NULL;
        if (px)
            return px;
    }
    *placeholder = true;
    return title_card(job->title, w, h);
}

static int worker_main(void *unused)
{
    (void)unused;
    SDL_LockMutex(lock);
    while (!quit)
    {
        if (queue_len == 0)
        {
            SDL_CondWait(wake, lock);
            continue;
        }
        int index = queue[0];
        memmove(queue, queue + 1, (size_t)--queue_len * sizeof(int));
        if (index >= slot_count || slots[index].state != SLOT_QUEUED)
            continue;
        Slot job = slots[index]; /* copy: strings stay valid without the lock */
        ++downloading;
        SDL_UnlockMutex(lock);

        int w = 0, h = 0;
        bool placeholder;
        uint8_t *px = produce(&job, &w, &h, &placeholder);

        SDL_LockMutex(lock);
        --downloading;
        if (index < slot_count && slots[index].state == SLOT_QUEUED && px)
        {
            slots[index].pixels = px;
            slots[index].w = w;
            slots[index].h = h;
            slots[index].placeholder = placeholder;
            slots[index].state = SLOT_READY;
        }
        else
        {
            free(px);
            if (index < slot_count && slots[index].state == SLOT_QUEUED)
                slots[index].state = SLOT_EMPTY;
        }
    }
    SDL_UnlockMutex(lock);
    return 0;
}

/* ---------------------------------------------------------------- main thread */

static void free_slots(void)
{
    for (int i = 0; i < slot_count; ++i)
    {
        plat_texture_free(slots[i].texture);
        free(slots[i].pixels);
    }
    free(slots);
    slots = NULL;
    slot_count = 0;
}

void covers_start(const Library *lib, const Paths *p, const Settings *settings)
{
    if (!lock)
    {
        lock = SDL_CreateMutex();
        wake = SDL_CreateCond();
    }
    SDL_LockMutex(lock);
    free_slots();
    queue_len = 0;
    paths = *p;
    style = settings->cover_style;
    download_enabled = settings->cover_download && net_available();
    network_failures = 0;
    slot_count = lib->count;
    slots = calloc((size_t)(slot_count ? slot_count : 1), sizeof(Slot));
    for (int i = 0; i < slot_count; ++i)
    {
        str_copy(slots[i].title, sizeof(slots[i].title), lib->games[i].title);
        str_copy(slots[i].serial, sizeof(slots[i].serial), lib->games[i].serial);
        str_copy(slots[i].folder, sizeof(slots[i].folder), lib->games[i].folder);
    }
    SDL_UnlockMutex(lock);
    if (!worker)
        worker = SDL_CreateThread(worker_main, "covers", NULL);
    psxs5_log("covers: %d games, style %d, downloads %s", slot_count, style,
              download_enabled ? "on" : "off");
}

void covers_stop(void)
{
    if (!lock)
        return;
    SDL_LockMutex(lock);
    quit = true;
    SDL_CondSignal(wake);
    SDL_UnlockMutex(lock);
    if (worker)
        SDL_WaitThread(worker, NULL);
    worker = NULL;
    free_slots();
}

void covers_update(int center)
{
    if (!slots)
        return;
    SDL_LockMutex(lock);
    /* upload a few finished images per frame */
    int uploads = 0;
    for (int i = 0; i < slot_count && uploads < 3; ++i)
    {
        Slot *s = &slots[i];
        if (s->state != SLOT_READY)
            continue;
        PlatTexture *t = plat_texture_create(s->pixels, s->w, s->h, true);
        free(s->pixels);
        s->pixels = NULL;
        plat_texture_free(s->texture);
        s->texture = t;
        s->state = SLOT_LOADED;
        ++uploads;
    }
    /* free far textures, request near ones nearest-first */
    for (int i = 0; i < slot_count; ++i)
    {
        int d = i > center ? i - center : center - i;
        if (d > KEEP_RADIUS && slots[i].state == SLOT_LOADED)
        {
            plat_texture_free(slots[i].texture);
            slots[i].texture = NULL;
            slots[i].state = SLOT_EMPTY;
        }
    }
    for (int d = 0; d <= LOAD_RADIUS; ++d)
        for (int side = -1; side <= 1; side += 2)
        {
            int i = center + d * side;
            if (i < 0 || i >= slot_count || slots[i].state != SLOT_EMPTY || queue_len >= QUEUE_SIZE)
                continue;
            slots[i].state = SLOT_QUEUED;
            queue[queue_len++] = i;
            if (d == 0)
                break;
        }
    if (queue_len)
        SDL_CondSignal(wake);
    SDL_UnlockMutex(lock);
}

PlatTexture *covers_get(int index)
{
    return (slots && index >= 0 && index < slot_count) ? slots[index].texture : NULL;
}

int covers_downloading(void)
{
    if (!lock)
        return 0;
    SDL_LockMutex(lock);
    int n = queue_len + downloading;
    SDL_UnlockMutex(lock);
    return n;
}
