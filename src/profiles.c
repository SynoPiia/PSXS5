/*
 * PSXS5 - profiles: several people on one console, each with their own
 * memory cards, save states, settings, play time and favourites, and
 * RetroAchievements sign-in. Games, BIOS, covers and cheats are shared.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The main profile is the data root itself (where everything lived before
 * profiles), so nothing moves. Others live in <root>/profiles/<name>/.
 * <root>/profiles.txt lists them (a sandboxed PSXS5 can't list folders) and
 * <root>/profile.txt says whose turn it is when not using PS5 auto-matching.
 * <root>/profiles-ps5.txt maps PS5 user ids to profile names.
 */
#include "profiles.h"

#include "app.h"
#include "config.h"
#include "i18n.h"
#include "platform/platform.h"
#include "ra/achievements.h"
#include "remote.h"
#include "stats.h"
#include "ui/coverflow.h"
#include "ui/sfx.h"
#include "ui/theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char names[PROFILES_MAX][PROFILE_NAME_LEN]; /* names[0] is "" (the main profile) */
static int count = 1;
static int current;

static void list_path(char *out, size_t size)
{
    path_join(out, size, app.paths.root, "profiles.txt");
}

static void current_path(char *out, size_t size)
{
    path_join(out, size, app.paths.root, "profile.txt");
}

static void load_list(void)
{
    count = 1;
    names[0][0] = '\0';
    char path[PSXS5_PATH_MAX];
    list_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[128];
    while (fgets(line, sizeof(line), f) && count < PROFILES_MAX)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && line[0] != '#')
            str_copy(names[count++], PROFILE_NAME_LEN, line);
    }
    fclose(f);
}

static void save_list(void)
{
    char path[PSXS5_PATH_MAX];
    list_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "# PSXS5 profiles (each has a folder in profiles/)\n");
    for (int i = 1; i < count; ++i)
        fprintf(f, "%s\n", names[i]);
    fclose(f);
}

/* The folder name: letters, digits, spaces, - and _ only. */
static void folder_of(const char *name, char *out, size_t size)
{
    size_t w = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p && w + 1 < size; ++p)
        out[w++] = (isalnum(*p) || *p == ' ' || *p == '-' || *p == '_') ? (char)*p : '_';
    out[w] = '\0';
}

/* app.paths for profile i: the per-person folders under its own directory. */
static void apply_paths(int i)
{
    char dir[PSXS5_PATH_MAX];
    if (i <= 0)
        str_copy(dir, sizeof(dir), app.paths.root);
    else
    {
        char sub[PROFILE_NAME_LEN + 16], folder[PROFILE_NAME_LEN];
        folder_of(names[i], folder, sizeof(folder));
        snprintf(sub, sizeof(sub), "profiles/%s", folder);
        path_join(dir, sizeof(dir), app.paths.root, sub);
    }
    config_user_paths(&app.paths, dir);
    make_dirs(app.paths.user);
    make_dirs(app.paths.saves);
    make_dirs(app.paths.states);
}

#if defined(__PROSPERO__)
static bool ps5_auto_enabled(void)
{
    return app.profile_ps5_auto;
}

#define PS5_MAP_USERS 8

static void ps5_map_path(char *out, size_t size)
{
    path_join(out, size, app.paths.root, "profiles-ps5.txt");
}

static void load_ps5_map(int *primary, int user_ids[PS5_MAP_USERS], char user_names[PS5_MAP_USERS][PROFILE_NAME_LEN],
                         int *user_count)
{
    *primary = -1;
    *user_count = 0;
    char path[PSXS5_PATH_MAX];
    ps5_map_path(path, sizeof(path));
    FILE *f = fopen(path, "r");

    if (!f)
        return;
    char line[256];

    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        char *eq = strchr(line, '=');

        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (!strcmp(key, "primary"))
        {
            *primary = (int)strtol(val, NULL, 0);
            continue;
        }

        if (*user_count >= PS5_MAP_USERS)
            continue;
        int uid = (int)strtol(key, NULL, 0);

        if (uid <= 0 || !val[0])
            continue;

        user_ids[*user_count] = uid;
        str_copy(user_names[*user_count], PROFILE_NAME_LEN, val);
        ++*user_count;
    }

    fclose(f);
}

static void save_ps5_map(int primary, const int user_ids[PS5_MAP_USERS],
                         const char user_names[PS5_MAP_USERS][PROFILE_NAME_LEN], int user_count)
{
    char path[PSXS5_PATH_MAX];
    ps5_map_path(path, sizeof(path));
    FILE *f = fopen(path, "w");

    if (!f)
        return;
    fprintf(f, "# PSXS5 PS5 user mapping\n");

    if (primary > 0)
        fprintf(f, "primary=%d\n", primary);
    for (int i = 0; i < user_count; ++i)
        if (user_ids[i] > 0 && user_names[i][0])
            fprintf(f, "%d=%s\n", user_ids[i], user_names[i]);
    fclose(f);
}

static bool ps5_map_lookup(int user_id, int primary, const int user_ids[PS5_MAP_USERS],
                           const char user_names[PS5_MAP_USERS][PROFILE_NAME_LEN], int user_count, char *out,
                           size_t out_size)
{
    for (int i = 0; i < user_count; ++i)
        if (user_ids[i] == user_id)
        {
            str_copy(out, out_size, user_names[i]);
            return true;
        }

    (void)primary;
    return false;
}

static void ps5_map_upsert(int user_id, const char *name, int *primary, int user_ids[PS5_MAP_USERS],
                           char user_names[PS5_MAP_USERS][PROFILE_NAME_LEN], int *user_count)
{
    for (int i = 0; i < *user_count; ++i)
    {
        if (user_ids[i] == user_id)
        {
            str_copy(user_names[i], PROFILE_NAME_LEN, name);
            return;
        }
    }
    if (*user_count < PS5_MAP_USERS)
    {
        user_ids[*user_count] = user_id;
        str_copy(user_names[*user_count], PROFILE_NAME_LEN, name);
        ++*user_count;
    }
    (void)primary;
}

static bool startup_ps5_auto(void)
{
    int uid;
    if (!plat_ps5_launch_user(&uid))
        return false;

    int primary = -1;
    int user_ids[PS5_MAP_USERS];
    char user_names[PS5_MAP_USERS][PROFILE_NAME_LEN];
    int user_count = 0;
    load_ps5_map(&primary, user_ids, user_names, &user_count);

    if (primary < 0 || uid == primary)
    {
        current = 0;
        if (primary < 0)
            primary = uid;
        save_ps5_map(primary, user_ids, user_names, user_count);
        apply_paths(0);
        psxs5_log("profile: PS5 user %d -> main profile (%s)", uid, app.paths.user);
        return true;
    }

    char name[PROFILE_NAME_LEN];
    if (!ps5_map_lookup(uid, primary, user_ids, user_names, user_count, name, sizeof(name)))
    {
        if (!plat_ps5_user_name(uid, name, sizeof(name)) || !name[0])
            snprintf(name, sizeof(name), "PS5-%08X", (unsigned)uid);
    }

    int i = profiles_find(name);

    if (i < 0)
        i = profiles_add(name);
    if (i < 0)
        return false;

    current = i;
    ps5_map_upsert(uid, name, &primary, user_ids, user_names, &user_count);
    save_ps5_map(primary, user_ids, user_names, user_count);
    apply_paths(i);
    psxs5_log("profile: PS5 user %d -> %s (%s)", uid, names[i], app.paths.user);
    
    return true;
}
#endif

static void startup_manual(void)
{
    current = 0;
    char path[PSXS5_PATH_MAX];
    current_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f)
    {
        char line[128] = "";
        if (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\r\n")] = '\0';
            for (int i = 1; i < count; ++i)
                if (!strcmp(names[i], line))
                    current = i;
        }
        fclose(f);
    }
    apply_paths(current);
    if (current)
        psxs5_log("profile: %s (%s)", names[current], app.paths.user);
}

void profiles_startup(void)
{
    load_list();
#if defined(__PROSPERO__)
    if (ps5_auto_enabled())
    {
        if (startup_ps5_auto())
            return;
        psxs5_log("profile: PS5 user detection failed, using saved selection");
    }
#endif
    startup_manual();
}

int profiles_count(void)
{
    return count;
}

int profiles_current(void)
{
    return current;
}

const char *profiles_name(int i)
{
    if (i < 0 || i >= count)
        return "";
    return i == 0 ? tr("Main profile") : names[i];
}

int profiles_find(const char *name)
{
    for (int i = 1; i < count; ++i)
        if (!strcmp(names[i], name))
            return i;
    return -1;
}

int profiles_add(const char *name)
{
    if (!name[0] || count >= PROFILES_MAX)
        return -1;
    int at = profiles_find(name);
    if (at >= 0)
        return at;
    str_copy(names[count], PROFILE_NAME_LEN, name);
    save_list();
    return count++;
}

bool profiles_remove(int i)
{
    if (i <= 0 || i >= count || i == current)
        return false;
    for (int k = i; k + 1 < count; ++k)
        memcpy(names[k], names[k + 1], PROFILE_NAME_LEN);
    --count;
    save_list(); /* its folder stays: nothing is deleted */
    return true;
}

bool profiles_switch(int i)
{
    if (i < 0 || i >= count || app.game)
        return false;
    if (i == current)
        return true;
    app_save_settings();
    stats_save();
    ra_shutdown();
    Settings before = app.global;
    current = i;
    apply_paths(i);
    FILE *f = fopen(app.paths.config, "r");
    if (f)
    {
        fclose(f);
        config_load(&app.global, app.paths.config);
    }
    else
    {
        /* a new profile starts with the settings in use */
        app.global = before;
        config_save(&app.global, app.paths.config);
    }
    app.settings = app.global;
    i18n_set(app.global.language);
    theme_apply(app.global.theme);
    sfx_configure(app.global.ui_sound, (app.global.ui_volume + 1) * 25);
    stats_load(app.paths.user);
    ra_init(&app.paths);
    char path[PSXS5_PATH_MAX];
    current_path(path, sizeof(path));
    f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "%s\n", i ? names[i] : "");
        fclose(f);
    }
    shelf_library_changed(); /* favourites and hidden games are the profile's */
    psxs5_log("profile: switched to %s", i ? names[i] : "the main profile");
    return true;
}
