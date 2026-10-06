/*
 * PSXS5 - RetroArch .cht cheat files (GameShark / Action Replay codes).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Format (libretro-database):
 *   cheats = 2
 *   cheat0_desc = "Infinite HP"
 *   cheat0_code = "800A1234 03E7+800A1236 03E7"
 *   cheat0_enable = false
 */
#include "cheats.h"
#include "i18n.h"

#include "libretro.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- parsing */

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    return s;
}

static void unquote(char *dst, size_t size, char *value)
{
    value = trim(value);
    size_t n = strlen(value);
    if (n >= 2 && value[0] == '"' && value[n - 1] == '"')
    {
        value[n - 1] = '\0';
        ++value;
    }
    str_copy(dst, size, value);
}

static bool parse_cht(CheatList *list, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');
        if (!eq || strncmp(line, "cheat", 5) != 0 || !isdigit((unsigned char)line[5]))
            continue;
        *eq = '\0';
        char *key = trim(line);
        int index = atoi(key + 5);
        if (index < 0 || index >= CHEATS_MAX)
            continue;
        const char *field = strchr(key, '_');
        if (!field)
            continue;
        ++field;
        Cheat *c = &list->items[index];
        if (strcmp(field, "desc") == 0)
            unquote(c->desc, sizeof(c->desc), eq + 1);
        else if (strcmp(field, "code") == 0)
            unquote(c->code, sizeof(c->code), eq + 1);
        else if (strcmp(field, "enable") == 0)
            c->enabled = strstr(eq + 1, "true") != NULL;
        if (index + 1 > list->count)
            list->count = index + 1;
    }
    fclose(f);

    /* Drop holes and entries without a code. */
    int w = 0;
    for (int i = 0; i < list->count; ++i)
    {
        if (!list->items[i].code[0])
            continue;
        if (!list->items[i].desc[0])
            snprintf(list->items[i].desc, sizeof(list->items[i].desc), tr("Code %d"), i + 1);
        list->items[w++] = list->items[i];
    }
    list->count = w;
    return w > 0;
}

/* ---------------------------------------------------------------- matching */

/* "Final Fantasy VII (USA) (Disc 1)" -> "finalfantasyvii" */
static void normalize(const char *in, char *out, size_t size)
{
    size_t w = 0;
    int depth = 0;
    for (const char *p = in; *p && w + 1 < size; ++p)
    {
        if (*p == '(' || *p == '[')
            ++depth;
        else if ((*p == ')' || *p == ']') && depth > 0)
            --depth;
        else if (depth == 0 && isalnum((unsigned char)*p))
            out[w++] = (char)tolower((unsigned char)*p);
    }
    out[w] = '\0';
    /* ignore a leading article so "The Legend of Dragoon" == "Legend of Dragoon, The" */
    if (strncmp(out, "the", 3) == 0 && strlen(out) > 6)
        memmove(out, out + 3, strlen(out + 3) + 1);
    size_t len = strlen(out);
    if (len > 3 && strcmp(out + len - 3, "the") == 0)
        out[len - 3] = '\0';
}

static const char *region_of_serial(const char *serial)
{
    if (!serial[0])
        return NULL;
    if (strncmp(serial, "SLUS", 4) == 0 || strncmp(serial, "SCUS", 4) == 0)
        return "(USA)";
    if (strncmp(serial, "SLES", 4) == 0 || strncmp(serial, "SCES", 4) == 0 ||
        strncmp(serial, "SCED", 4) == 0)
        return "(Europe)";
    return "(Japan)";
}

static int score_candidate(const char *file, const Game *game, const char *want_title,
                           const char *want_disc)
{
    char stem[256];
    str_copy(stem, sizeof(stem), file);
    char *dot = strrchr(stem, '.');
    if (dot)
        *dot = '\0';

    if (str_icmp(stem, game->disc_name) == 0)
        return 1000; /* exact No-Intro / Redump name */

    char norm[256];
    normalize(stem, norm, sizeof(norm));
    int score = 0;
    if (strcmp(norm, want_disc) == 0 || strcmp(norm, want_title) == 0)
        score = 500;
    else
        return 0;

    /* Same region as the disc beats other releases. */
    static const char *regions[] = {"(USA)", "(Europe)", "(Japan)"};
    const char *region = region_of_serial(game->serial);
    bool same_region = region && strstr(stem, region);
    for (size_t r = 0; r < 3 && !same_region; ++r)
        same_region = strstr(game->disc_name, regions[r]) && strstr(stem, regions[r]);
    if (same_region)
        score += 100;
    else if (strstr(stem, "(World)"))
        score += 90;
    else if (!region && strstr(stem, "(USA)"))
        score += 20;
    /* GameShark sets use the plain code format; prefer them over other devices. */
    if (strstr(stem, "(GameShark)"))
        score += 5;
    /* Codes are usually filed under disc 1 or under no disc number. */
    if (strstr(stem, "(Disc 1)") || !strstr(stem, "(Disc"))
        score += 10;
    return score;
}

static bool find_in_dir(const char *dir, const Game *game, char *best_path, size_t size,
                        int *best_score)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    char want_title[256], want_disc[256];
    normalize(game->title, want_title, sizeof(want_title));
    normalize(game->disc_name, want_disc, sizeof(want_disc));
    bool found = false;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (str_icmp(path_ext(e->d_name), "cht") != 0)
            continue;
        int score = score_candidate(e->d_name, game, want_title, want_disc);
        if (score > *best_score)
        {
            *best_score = score;
            path_join(best_path, size, dir, e->d_name);
            found = true;
        }
    }
    closedir(d);
    return found;
}

static bool find_any_cht(const char *dir, char *out, size_t size)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)))
        if (str_icmp(path_ext(e->d_name), "cht") == 0)
        {
            path_join(out, size, dir, e->d_name);
            found = true;
        }
    closedir(d);
    return found;
}

/* ---------------------------------------------------------------- selection */

static void load_selection(CheatList *list)
{
    FILE *f = fopen(list->state_path, "r");
    if (!f)
        return;
    for (int i = 0; i < list->count; ++i)
        list->items[i].enabled = false;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char *desc = trim(line);
        for (int i = 0; i < list->count; ++i)
            if (strcmp(list->items[i].desc, desc) == 0)
                list->items[i].enabled = true;
    }
    fclose(f);
}

void cheats_save_selection(const CheatList *list)
{
    if (!list->state_path[0])
        return;
    FILE *f = fopen(list->state_path, "w");
    if (!f)
        return;
    for (int i = 0; i < list->count; ++i)
        if (list->items[i].enabled)
            fprintf(f, "%s\n", list->items[i].desc);
    fclose(f);
}

/* ---------------------------------------------------------------- API */

void cheats_clear(CheatList *list)
{
    list->count = 0;
    list->source[0] = '\0';
    list->state_path[0] = '\0';
}

bool cheats_load(CheatList *list, const Game *game, const char *cheats_dir)
{
    cheats_clear(list);
    memset(list->items, 0, sizeof(list->items));

    char path[PSXS5_PATH_MAX] = "";
    int score = 0;
    /* 1. A .cht next to the game always wins. "cheats.cht" (placed by the sync
     *    tool) opens without listing the folder, which a sandboxed PSXS5 can't. */
    bool found = false;
    if (game->folder[0])
    {
        path_join(path, sizeof(path), game->folder, "cheats.cht");
        found = path_exists(path);
    }
    if (!found)
        found = game->folder[0] && strcmp(game->folder, cheats_dir) != 0 &&
                find_any_cht(game->folder, path, sizeof(path));
    /* 2. Otherwise the best match from the cheat library. */
    if (!found)
        found = find_in_dir(cheats_dir, game, path, sizeof(path), &score);
    if (!found)
    {
        char sub[PSXS5_PATH_MAX];
        path_join(sub, sizeof(sub), cheats_dir, "Sony - PlayStation");
        found = find_in_dir(sub, game, path, sizeof(path), &score);
    }
    if (!found || !parse_cht(list, path))
        return false;

    str_copy(list->source, sizeof(list->source), path);
    char enabled_dir[PSXS5_PATH_MAX], file[96];
    path_join(enabled_dir, sizeof(enabled_dir), cheats_dir, "enabled");
    make_dirs(enabled_dir);
    snprintf(file, sizeof(file), "%.80s.txt", game->id);
    path_join(list->state_path, sizeof(list->state_path), enabled_dir, file);
    load_selection(list);
    psxs5_log("cheats: %d codes from %s", list->count, path);
    return true;
}

void cheats_apply(const CheatList *list)
{
    retro_cheat_reset();
    unsigned index = 0;
    for (int i = 0; i < list->count; ++i)
        if (list->items[i].enabled)
            retro_cheat_set(index++, true, list->items[i].code);
}
