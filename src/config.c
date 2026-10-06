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
    s->upscale = 2;
    s->upscale_filter = UPSCALE_XBR;
    s->region = REGION_AUTO;
    s->dithering = true;
    s->analog = true;
    s->cover_style = COVER_FLAT;
    s->cover_download = true;
    s->ui_sound = 0;  /* Soft */
    s->ui_volume = 1; /* 50 % */
    s->rumble = true;
    s->rumble_strength = 3;
    for (int i = 0; i < 16; ++i)
        s->button_map[i] = (int8_t)i;
}

static bool as_bool(const char *v)
{
    return strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0;
}

/* Applies the keys in a file over what *s already holds. */
static bool config_apply(Settings *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
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
        else if (strcmp(key, "stick_dpad") == 0)
            s->stick_dpad = atoi(value) % STICK_DPAD_COUNT;
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
        else if (strcmp(key, "ui_sounds") == 0 && !as_bool(value))
            s->ui_sound = 5; /* older config: sounds off -> SFX_STYLE_OFF */
        else if (strcmp(key, "ui_sound") == 0)
            s->ui_sound = atoi(value) % 6;
        else if (strcmp(key, "language") == 0)
            s->language = atoi(value) % 5;
        else if (strcmp(key, "ui_volume") == 0)
            s->ui_volume = atoi(value) % 4;
        else if (strcmp(key, "rumble") == 0)
            s->rumble = as_bool(value);
        else if (strcmp(key, "rumble_strength") == 0)
            s->rumble_strength = atoi(value) % 4;
        else if (strcmp(key, "sort_mode") == 0)
            s->sort_mode = atoi(value) % 8;
        else if (strcmp(key, "shelf_category") == 0)
            s->shelf_category = atoi(value) % 16;
        else if (strcmp(key, "button_map") == 0)
        {
            char *p = value;
            for (int i = 0; i < 16 && *p; ++i)
            {
                int v = (int)strtol(p, &p, 10);
                s->button_map[i] = (int8_t)(v >= -1 && v < 16 ? v : i);
                if (*p == ',')
                    ++p;
            }
        }
    }
    fclose(f);
    return true;
}

void config_load(Settings *s, const char *path)
{
    config_defaults(s);
    config_apply(s, path);
}

bool config_load_game(Settings *out, const Settings *global, const char *path)
{
    *out = *global;
    if (!config_apply(out, path))
        return false;
    /* these always follow the console-wide settings */
    out->last_game = global->last_game;
    out->cover_style = global->cover_style;
    out->cover_download = global->cover_download;
    out->ui_sound = global->ui_sound;
    out->ui_volume = global->ui_volume;
    out->language = global->language;
    out->sort_mode = global->sort_mode;
    out->shelf_category = global->shelf_category;
    return true;
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
            "cover_style=%d\ncover_download=%d\nui_sound=%d\nui_volume=%d\n"
            "integer_scale=%d\ninternal_res=%d\nupscale=%d\nupscale_filter=%d\nstick_dpad=%d\nlanguage=%d\n"
            "rumble=%d\nrumble_strength=%d\nsort_mode=%d\nshelf_category=%d\n",
            s->aspect, s->smooth, s->show_fps, s->region, s->force_hle, s->dithering,
            s->cd_fast, s->analog, s->state_slot, s->last_game, s->cover_style,
            s->cover_download, s->ui_sound, s->ui_volume, s->integer_scale, s->internal_res, s->upscale,
            s->upscale_filter, s->stick_dpad, s->language, s->rumble, s->rumble_strength,
            s->sort_mode, s->shelf_category);
    fprintf(f, "button_map=");
    for (int i = 0; i < 16; ++i)
        fprintf(f, i ? ",%d" : "%d", s->button_map[i]);
    fprintf(f, "\n");
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
