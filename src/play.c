/*
 * PSXS5 - things around the running game: quick resume, state thumbnails,
 * rewind, fast forward, widescreen codes and fan-translation patches.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "play.h"

#include "app.h"
#include "core/host.h"
#include "i18n.h"
#include "ra/achievements.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ---------------------------------------------------------------- translation patches */

/* The core looks for <dir>/SLUS_004.21 (the disc's id, PPF's convention).
 * Players drop any *.ppf next to the game; it is copied under that name. */
bool play_prepare_patch(const Game *g)
{
    char dir[PSXS5_PATH_MAX], none[PSXS5_PATH_MAX];
    path_join(dir, sizeof(dir), app.paths.root, "cache/ppf");
    path_join(none, sizeof(none), app.paths.root, "cache/ppf-none");
    host_set_patches_dir(none);
    char letters[5] = "", digits[6] = "";
    int nl = 0, nd = 0;
    for (const char *p = g->serial; *p; ++p)
    {
        if (isalpha((unsigned char)*p) && nl < 4 && nd == 0)
            letters[nl++] = (char)toupper((unsigned char)*p);
        else if (isdigit((unsigned char)*p) && nd < 5)
            digits[nd++] = *p;
    }
    letters[nl] = digits[nd] = '\0';
    if (nl != 4 || nd != 5)
        return false;
    DIR *d = opendir(g->folder);
    if (!d)
        return false;
    char found[PSXS5_PATH_MAX] = "";
    struct dirent *e;
    while ((e = readdir(d)))
        if (str_icmp(path_ext(e->d_name), "ppf") == 0)
        {
            path_join(found, sizeof(found), g->folder, e->d_name);
            break;
        }
    closedir(d);
    if (!found[0])
        return false;
    char name[16], target[PSXS5_PATH_MAX];
    snprintf(name, sizeof(name), "%s_%.3s.%.2s", letters, digits, digits + 3);
    make_dirs(dir);
    path_join(target, sizeof(target), dir, name);
    FILE *in = fopen(found, "rb"), *out = in ? fopen(target, "wb") : NULL;
    bool ok = in && out;
    char buf[65536];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0)
        ok = fwrite(buf, 1, n, out) == n;
    if (in)
        fclose(in);
    if (out)
        ok = fclose(out) == 0 && ok;
    if (!ok)
        return false;
    char with_slash[PSXS5_PATH_MAX];
    snprintf(with_slash, sizeof(with_slash), "%s/", dir);
    host_set_patches_dir(with_slash);
    psxs5_log("patch: %s as %s", found, name);
    return true;
}

/* ---------------------------------------------------------------- widescreen */

static bool widescreen_on;

static bool contains_icase(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; ++hay)
    {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == n)
            return true;
    }
    return false;
}

bool play_widescreen(void)
{
    widescreen_on = false;
    if (!app.settings.widescreen || ra_hardcore())
        return false;
    for (int i = 0; i < app.cheats.count; ++i)
    {
        const char *desc = app.cheats.items[i].desc;
        if (contains_icase(desc, "widescreen") || contains_icase(desc, "16:9") ||
            contains_icase(desc, "wide screen"))
        {
            app.cheats.items[i].enabled = true;
            widescreen_on = true;
        }
    }
    if (widescreen_on)
        cheats_apply(&app.cheats);
    return widescreen_on;
}

bool play_widescreen_active(void)
{
    return widescreen_on;
}

/* ---------------------------------------------------------------- thumbnails */

void play_save_thumb(const char *state_path)
{
    static uint8_t rgba[THUMB_W * THUMB_H * 4];
    if (!host_capture(rgba, THUMB_W, THUMB_H))
        return;
    char path[PSXS5_PATH_MAX + 8];
    snprintf(path, sizeof(path), "%s.thumb", state_path);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    const uint8_t header[8] = {'P', 'S', 'X', 'T', THUMB_W & 0xff, THUMB_W >> 8, THUMB_H & 0xff,
                               THUMB_H >> 8};
    fwrite(header, 1, sizeof(header), f);
    fwrite(rgba, 1, sizeof(rgba), f);
    fclose(f);
}

bool play_load_thumb(const char *state_path, uint8_t *rgba)
{
    char path[PSXS5_PATH_MAX + 8];
    snprintf(path, sizeof(path), "%s.thumb", state_path);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t header[8];
    bool ok = fread(header, 1, 8, f) == 8 && !memcmp(header, "PSXT", 4) &&
              (header[4] | header[5] << 8) == THUMB_W && (header[6] | header[7] << 8) == THUMB_H &&
              fread(rgba, 1, THUMB_W * THUMB_H * 4, f) == THUMB_W * THUMB_H * 4;
    fclose(f);
    return ok;
}

/* ---------------------------------------------------------------- quick resume */

void play_resume_path(const Game *g, char *out, size_t size)
{
    char file[96];
    snprintf(file, sizeof(file), "%.80s.resume", g->id);
    path_join(out, size, app.paths.states, file);
}

bool play_has_resume(const Game *g, long *age_seconds)
{
    char path[PSXS5_PATH_MAX];
    play_resume_path(g, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    if (age_seconds)
        *age_seconds = (long)(time(NULL) - st.st_mtime);
    return true;
}

typedef struct
{
    char path[PSXS5_PATH_MAX];
    void *data;
    size_t size;
} Writer;

static SDL_atomic_t writing;

/* the file is written off the main thread, so play doesn't stutter */
static int write_thread(void *arg)
{
    Writer *w = arg;
    char temp[PSXS5_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.tmp", w->path);
    FILE *f = fopen(temp, "wb");
    bool ok = f && fwrite(w->data, 1, w->size, f) == w->size;
    if (f)
        ok = fclose(f) == 0 && ok;
    if (ok)
        rename(temp, w->path);
    free(w->data);
    free(w);
    SDL_AtomicSet(&writing, 0);
    return 0;
}

void play_save_resume(bool background)
{
    if (!app.game || !app.settings.quick_resume || ra_hardcore())
        return;
    if (SDL_AtomicGet(&writing))
        return; /* the previous one is still being written */
    size_t size = host_state_size();
    Writer *w = size ? calloc(1, sizeof(*w)) : NULL;
    if (!w || !(w->data = malloc(size)) || !host_serialize(w->data, size))
    {
        if (w)
            free(w->data);
        free(w);
        return;
    }
    w->size = size;
    make_dirs(app.paths.states);
    play_resume_path(app.game, w->path, sizeof(w->path));
    play_save_thumb(w->path);
    SDL_AtomicSet(&writing, 1);
    if (background)
    {
        SDL_Thread *t = SDL_CreateThread(write_thread, "resume", w);
        if (t)
        {
            SDL_DetachThread(t);
            return;
        }
    }
    write_thread(w);
}

bool play_load_resume(void)
{
    if (!app.game || ra_hardcore())
        return false;
    char path[PSXS5_PATH_MAX];
    play_resume_path(app.game, path, sizeof(path));
    return host_load_state(path);
}

/* ---------------------------------------------------------------- rewind */

/* A ring of states, one every STEP frames: about 8 seconds back. */
#define SLOTS 40
#define STEP 12

static uint8_t *ring;
static size_t state_size;
static int head, filled, counter;

void play_rewind_reset(void)
{
    free(ring);
    ring = NULL;
    state_size = 0;
    head = filled = counter = 0;
}

void play_rewind_record(void)
{
    if (!app.settings.rewind || ra_hardcore())
    {
        if (ring)
            play_rewind_reset();
        return;
    }
    if (++counter < STEP)
        return;
    counter = 0;
    if (!ring)
    {
        state_size = host_state_size();
        ring = state_size ? malloc(state_size * SLOTS) : NULL;
        if (!ring)
        {
            psxs5_log("rewind: no memory for %d x %zu bytes", SLOTS, state_size);
            app.settings.rewind = false;
            return;
        }
    }
    if (host_serialize(ring + (size_t)head * state_size, state_size))
    {
        head = (head + 1) % SLOTS;
        if (filled < SLOTS)
            ++filled;
    }
}

bool play_rewind_step(void)
{
    if (!ring || filled == 0)
        return false;
    head = (head + SLOTS - 1) % SLOTS;
    --filled;
    counter = 0;
    return host_unserialize(ring + (size_t)head * state_size, state_size);
}
