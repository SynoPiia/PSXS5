/*
 * PSXS5 - settings stored as key=value lines in <root>/psxs5.ini.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->aspect = ASPECT_AUTO;
    s->smooth = true;
    s->internal_res = 1;
    s->upscale = 1;
    s->upscale_filter = UPSCALE_SHARP;
    s->region = REGION_AUTO;
    s->dithering = true;
    s->analog = true;
    s->cover_style = COVER_FLAT;
    s->cover_download = true;
    s->ui_sounds = true;
}

static bool as_bool(const char *v)
{
    return strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0;
}

void config_load(Settings *s, const char *path)
{
    config_defaults(s);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#')
            continue;
        *eq = '\0';
        char *key = line, *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(key, "aspect") == 0)
            s->aspect = atoi(value) % ASPECT_COUNT;
        else if (strcmp(key, "smooth") == 0)
            s->smooth = as_bool(value);
        else if (strcmp(key, "show_fps") == 0)
            s->show_fps = as_bool(value);
        else if (strcmp(key, "region") == 0)
            s->region = atoi(value) % REGION_COUNT;
        else if (strcmp(key, "force_hle") == 0)
            s->force_hle = as_bool(value);
        else if (strcmp(key, "dithering") == 0)
            s->dithering = as_bool(value);
        else if (strcmp(key, "cd_fast") == 0)
            s->cd_fast = as_bool(value);
        else if (strcmp(key, "analog") == 0)
            s->analog = as_bool(value);
        else if (strcmp(key, "state_slot") == 0)
            s->state_slot = atoi(value) % 10;
        else if (strcmp(key, "last_game") == 0)
            s->last_game = atoi(value);
        else if (strcmp(key, "integer_scale") == 0)
            s->integer_scale = as_bool(value);
        else if (strcmp(key, "internal_res") == 0)
            s->internal_res = atoi(value) == 2 ? 2 : 1;
        else if (strcmp(key, "upscale") == 0)
            s->upscale = atoi(value) >= 1 && atoi(value) <= 4 ? atoi(value) : 1;
        else if (strcmp(key, "upscale_filter") == 0)
            s->upscale_filter = atoi(value) % UPSCALE_FILTER_COUNT;
        else if (strcmp(key, "cover_style") == 0)
            s->cover_style = atoi(value) % COVER_STYLE_COUNT;
        else if (strcmp(key, "cover_download") == 0)
            s->cover_download = as_bool(value);
        else if (strcmp(key, "ui_sounds") == 0)
            s->ui_sounds = as_bool(value);
    }
    fclose(f);
}

bool config_save(const Settings *s, const char *path)
{
    char temp[PSXS5_PATH_MAX];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *f = fopen(temp, "w");
    if (!f)
        return false;
    fprintf(f,
            "# PSXS5 settings\n"
            "aspect=%d\nsmooth=%d\nshow_fps=%d\nregion=%d\nforce_hle=%d\n"
            "dithering=%d\ncd_fast=%d\nanalog=%d\nstate_slot=%d\nlast_game=%d\n"
            "cover_style=%d\ncover_download=%d\nui_sounds=%d\n"
            "integer_scale=%d\ninternal_res=%d\nupscale=%d\nupscale_filter=%d\n",
            s->aspect, s->smooth, s->show_fps, s->region, s->force_hle, s->dithering,
            s->cd_fast, s->analog, s->state_slot, s->last_game, s->cover_style,
            s->cover_download, s->ui_sounds, s->integer_scale, s->internal_res, s->upscale,
            s->upscale_filter);
    bool ok = fclose(f) == 0;
    return ok && rename(temp, path) == 0;
}

void config_paths(Paths *p, const char *root)
{
    str_copy(p->root, sizeof(p->root), root);
    path_join(p->games, sizeof(p->games), root, "games");
    path_join(p->bios, sizeof(p->bios), root, "bios");
    path_join(p->saves, sizeof(p->saves), root, "saves");
    path_join(p->states, sizeof(p->states), root, "states");
    path_join(p->cheats, sizeof(p->cheats), root, "cheats");
    path_join(p->covers, sizeof(p->covers), root, "covers");
    path_join(p->logs, sizeof(p->logs), root, "logs");
    path_join(p->config, sizeof(p->config), root, "psxs5.ini");
}
