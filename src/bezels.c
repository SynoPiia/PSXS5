/*
 * PSXS5 - the game's own artwork around a 4:3 picture (Settings > Display >
 * Game artwork border), from The Bezel Project.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * assets/bezels-index.txt lists the project's PlayStation bezels (Redump
 * names, as the cheat files); the game's best match is downloaded once into
 * <root>/bezels/<id>.png, on a thread, the first time the game runs with the
 * setting on. Each is a 1920x1080 picture with a clear 4:3 window.
 */
#include "bezels.h"

#include "app.h"
#include "cheats.h"
#include "net.h"
#include "platform/platform.h"
#include "stb_image.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct
{
    const Game *game;     /* the bezel below is this game's */
    PlatTexture *texture;
    bool tried;           /* looked on disk (and maybe asked for a download) */
} B;

static SDL_atomic_t fetching; /* 0 idle, 1 downloading, 2 arrived */
static char fetch_url[700], fetch_dest[PSXS5_PATH_MAX];

static void bezel_path(const Game *g, char *out, size_t size)
{
    char dir[PSXS5_PATH_MAX], file[120];
    path_join(dir, sizeof(dir), app.paths.root, "bezels");
    snprintf(file, sizeof(file), "%.100s.png", g->id);
    path_join(out, size, dir, file);
}

static int fetch_main(void *unused)
{
    (void)unused;
    char temp[PSXS5_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.part", fetch_dest);
    NetResult r = net_download(fetch_url, temp);
    bool ok = r == NET_OK && rename(temp, fetch_dest) == 0;
    if (!ok)
        remove(temp);
    psxs5_log("bezels: download %s: %s", fetch_dest, ok ? "ok" : r == NET_NOT_FOUND ? "not found" : "failed");
    SDL_AtomicSet(&fetching, ok ? 2 : 0);
    return 0;
}

static void fetch_start(const char *name, const char *dest)
{
    if (!net_available() || !SDL_AtomicCAS(&fetching, 0, 1))
        return;
    static const char base[] = "https://raw.githubusercontent.com/thebezelproject/bezelproject-PSX/master/"
                               "retroarch/overlay/GameBezels/PSX/";
    str_copy(fetch_url, sizeof(fetch_url), base);
    size_t w = strlen(fetch_url);
    for (const unsigned char *p = (const unsigned char *)name; *p && w + 4 < sizeof(fetch_url); ++p)
    {
        if (isalnum(*p) || strchr("-._~", *p))
            fetch_url[w++] = (char)*p;
        else
            w += (size_t)snprintf(fetch_url + w, sizeof(fetch_url) - w, "%%%02X", *p);
    }
    fetch_url[w] = '\0';
    str_copy(fetch_dest, sizeof(fetch_dest), dest);
    SDL_Thread *t = SDL_CreateThread(fetch_main, "bezel-download", NULL);
    if (t)
        SDL_DetachThread(t);
    else
        SDL_AtomicSet(&fetching, 0);
}

static bool load(const char *path)
{
    int w, h, n;
    uint8_t *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba)
        return false;
    B.texture = plat_texture_create(rgba, w, h, true);
    stbi_image_free(rgba);
    return B.texture != NULL;
}

static void forget(void)
{
    plat_texture_free(B.texture);
    B.texture = NULL;
    B.tried = false;
    B.game = NULL;
}

/* The running game's bezel, loading or fetching it the first time. */
static PlatTexture *current(void)
{
    if (B.game != app.game)
    {
        forget();
        B.game = app.game;
    }
    if (!app.game)
        return NULL;
    if (SDL_AtomicCAS(&fetching, 2, 0))
        B.tried = false; /* a download arrived: look again */
    if (!B.tried)
    {
        B.tried = true;
        char path[PSXS5_PATH_MAX];
        bezel_path(app.game, path, sizeof(path));
        if (!load(path))
        {
            char name[256];
            if (cheats_best_in_index(app.game, "bezels-index.txt", name, sizeof(name)))
            {
                char dir[PSXS5_PATH_MAX];
                path_join(dir, sizeof(dir), app.paths.root, "bezels");
                make_dirs(dir);
                psxs5_log("bezels: %s -> %s", app.game->title, name);
                fetch_start(name, path);
            }
        }
    }
    return B.texture;
}

bool bezel_draw(const Settings *view, uint8_t dim)
{
    if (!view->bezel || view->aspect == ASPECT_STRETCH || view->aspect == ASPECT_16_9 ||
        view->aspect == ASPECT_16_10)
        return false;
    PlatTexture *t = current();
    if (!t)
        return false;
    plat_draw_texture(t, 0, 0, (float)plat_width(), (float)plat_height(),
                      0xff000000u | (uint32_t)dim << 16 | (uint32_t)dim << 8 | dim, true);
    return true;
}

bool bezel_shown(const Settings *view)
{
    return view->bezel && B.texture && B.game == app.game && view->aspect != ASPECT_STRETCH &&
           view->aspect != ASPECT_16_9 && view->aspect != ASPECT_16_10;
}
