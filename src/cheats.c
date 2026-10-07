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

#include "core/host.h"
#include "net.h"
#include "platform/platform.h"

#include <SDL2/SDL.h>

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

/* A title as words: lower case, without the parts in brackets, roman numerals
 * as digits, and without the words release names add or drop ("the", "of",
 * "Disney-Pixar's"...). "Final Fantasy VII (USA) (Disc 1)" -> final fantasy 7 */
#define MAX_WORDS 24
typedef struct
{
    char w[MAX_WORDS][24];
    int n;
} Words;

static void title_words(const char *in, Words *out)
{
    static const char *const stop[] = {"the", "a", "an", "of", "and", "s", "disney", "pixar", NULL};
    static const char *const roman[][2] = {{"ii", "2"},  {"iii", "3"}, {"iv", "4"},  {"v", "5"}, {"vi", "6"},
                                           {"vii", "7"}, {"viii", "8"}, {"ix", "9"}, {"x", "10"}};
    out->n = 0;
    int depth = 0;
    char word[24];
    size_t len = 0;
    for (const char *p = in;; ++p)
    {
        char c = *p;
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth > 0)
            --depth;
        bool letter = c && depth == 0 && isalnum((unsigned char)c);
        if (letter && len + 1 < sizeof(word))
        {
            word[len++] = (char)tolower((unsigned char)c);
            continue;
        }
        if (letter)
            continue; /* overlong word: truncated */
        if (len)
        {
            word[len] = '\0';
            len = 0;
            bool skip = false;
            for (int i = 0; stop[i] && !skip; ++i)
                skip = strcmp(word, stop[i]) == 0;
            for (size_t i = 0; i < sizeof(roman) / sizeof(roman[0]); ++i)
                if (strcmp(word, roman[i][0]) == 0)
                    str_copy(word, sizeof(word), roman[i][1]);
            if (!skip && out->n < MAX_WORDS)
                str_copy(out->w[out->n++], sizeof(out->w[0]), word);
        }
        if (!c)
            break;
    }
}

static bool has_word(const Words *w, const char *word)
{
    for (int i = 0; i < w->n; ++i)
        if (strcmp(w->w[i], word) == 0)
            return true;
    return false;
}

/* 0, or up to 300 for the same words: every word of the shorter title is in
 * the longer, the numbers are the same (Crash 2 isn't Crash 3) and most words
 * are shared ("Legend of Dragoon" isn't "Legend"). */
static int title_match(const Words *a, const Words *b)
{
    if (!a->n || !b->n)
        return 0;
    char na[64] = "", nb[64] = "";
    for (int i = 0; i < a->n; ++i)
        if (isdigit((unsigned char)a->w[i][0]))
            snprintf(na + strlen(na), sizeof(na) - strlen(na), "%s.", a->w[i]);
    for (int i = 0; i < b->n; ++i)
        if (isdigit((unsigned char)b->w[i][0]))
            snprintf(nb + strlen(nb), sizeof(nb) - strlen(nb), "%s.", b->w[i]);
    if (strcmp(na, nb) != 0)
        return 0;
    const Words *small = a->n <= b->n ? a : b, *large = a->n <= b->n ? b : a;
    int shared = 0;
    for (int i = 0; i < small->n; ++i)
    {
        bool dup = false;
        for (int j = 0; j < i && !dup; ++j)
            dup = strcmp(small->w[j], small->w[i]) == 0;
        if (dup)
            continue;
        if (!has_word(large, small->w[i]))
            return 0;
        ++shared;
    }
    int unique_large = 0;
    for (int i = 0; i < large->n; ++i)
    {
        bool dup = false;
        for (int j = 0; j < i && !dup; ++j)
            dup = strcmp(large->w[j], large->w[i]) == 0;
        unique_large += !dup;
    }
    /* Jaccard: shared / union, union = the larger set here */
    int score = unique_large ? 300 * shared / unique_large : 0;
    return score >= 210 ? score : 0; /* at least 70 % */
}

static const char *region_of_serial(const char *serial)
{
    if (!serial[0])
        return NULL;
    if (strncmp(serial, "SLUS", 4) == 0 || strncmp(serial, "SCUS", 4) == 0)
        return "(USA";
    if (strncmp(serial, "SLES", 4) == 0 || strncmp(serial, "SCES", 4) == 0 ||
        strncmp(serial, "SCED", 4) == 0)
        return "Europe";
    return "Japan";
}

typedef struct
{
    Words title, disc;
} Wanted;

static void wanted_for(const Game *game, Wanted *w)
{
    title_words(game->title, &w->title);
    title_words(game->disc_name, &w->disc);
}

/* How well a cheat file name fits the game: 0 for another game. */
static int score_candidate(const char *file, const Game *game, const Wanted *want)
{
    char stem[256];
    str_copy(stem, sizeof(stem), file);
    char *dot = strrchr(stem, '.');
    if (dot)
        *dot = '\0';
    if (str_icmp(stem, game->disc_name) == 0)
        return 1000; /* exact No-Intro / Redump name */
    Words have;
    title_words(stem, &have);
    int a = title_match(&want->title, &have), b = title_match(&want->disc, &have);
    int score = a > b ? a : b;
    if (!score)
        return 0;
    /* The disc's region beats other releases; "World" is the next best. */
    const char *region = region_of_serial(game->serial);
    if (region && strstr(stem, region))
        score += 100;
    else if (strstr(stem, "(World)"))
        score += 90;
    else if (!region && strstr(stem, "(USA"))
        score += 20;
    /* GameShark sets are the full ones (files without a device are often a
     * code or two): one beats a plain file of the disc's own region. */
    if (strstr(stem, "(GameShark)"))
        score += 15;
    /* Codes are usually filed under disc 1 or under no disc number. */
    if (strstr(stem, "(Disc 1)") || !strstr(stem, "(Disc"))
        score += 10;
    /* demos and previews have other addresses: only for a demo disc */
    static const char *const trial[] = {"(Demo)", "(Preview)", "(Beta)", "(Proto", "(Sample)"};
    for (size_t i = 0; i < sizeof(trial) / sizeof(trial[0]); ++i)
        if (strstr(stem, trial[i]) && !strstr(game->disc_name, trial[i]) && !strstr(game->title, trial[i]))
            return 0;
    return score;
}

static bool find_in_dir(const char *dir, const Game *game, char *best_path, size_t size,
                        int *best_score)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    Wanted want;
    wanted_for(game, &want);
    bool found = false;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (str_icmp(path_ext(e->d_name), "cht") != 0)
            continue;
        int score = score_candidate(e->d_name, game, &want);
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

/* ---------------------------------------------------------------- the built-in index */

/* assets/cheats-index.txt: every PlayStation cheat file of libretro-database
 * (tools/make-cheat-index.py). The best name for the game, without listing
 * any folder. */
static bool best_from_index(const Game *game, char *name, size_t size)
{
    char path[PSXS5_PATH_MAX];
    plat_asset_path(path, sizeof(path), "cheats-index.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    Wanted want;
    wanted_for(game, &want);
    char line[256];
    int best = 0;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        int score = score_candidate(line, game, &want);
        if (score > best)
        {
            best = score;
            str_copy(name, size, line);
        }
    }
    fclose(f);
    return best > 0;
}

/* One file from libretro-database, on a thread: the game starts meanwhile and
 * its cheats appear as soon as the file is in (cheats_fetch_finished). */
static SDL_atomic_t fetch_state; /* 0 idle, 1 downloading, 2 done */
static char fetch_url[600], fetch_dest[PSXS5_PATH_MAX];

static int fetch_main(void *unused)
{
    (void)unused;
    char temp[PSXS5_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.part", fetch_dest);
    NetResult r = net_download(fetch_url, temp);
    bool ok = r == NET_OK && rename(temp, fetch_dest) == 0;
    if (!ok)
        remove(temp);
    psxs5_log("cheats: download %s: %s", fetch_dest, ok ? "ok" : r == NET_NOT_FOUND ? "not found" : "failed");
    SDL_AtomicSet(&fetch_state, ok ? 2 : 0);
    return 0;
}

static void fetch_start(const char *name, const char *dest)
{
    if (!net_available() || !SDL_AtomicCAS(&fetch_state, 0, 1))
        return;
    static const char base[] =
        "https://raw.githubusercontent.com/libretro/libretro-database/master/cht/Sony%20-%20PlayStation/";
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
    SDL_Thread *t = SDL_CreateThread(fetch_main, "cheat-download", NULL);
    if (t)
        SDL_DetachThread(t);
    else
        SDL_AtomicSet(&fetch_state, 0);
}

bool cheats_fetch_finished(void)
{
    return SDL_AtomicCAS(&fetch_state, 2, 0);
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

static bool file_opens(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
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
    /* 3. The best name in the built-in index of libretro-database: the file
     *    if it's here, else downloaded for next time (needs no listing). */
    if (!found)
    {
        char name[256];
        if (best_from_index(game, name, sizeof(name)))
        {
            path_join(path, sizeof(path), cheats_dir, name);
            found = file_opens(path);
            if (!found)
            {
                make_dirs(cheats_dir);
                fetch_start(name, path);
            }
        }
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
    host_cheat_reset(); /* to the emulator running the game */
    unsigned index = 0;
    for (int i = 0; i < list->count; ++i)
        if (list->items[i].enabled)
            host_cheat_set(index++, list->items[i].code);
}
