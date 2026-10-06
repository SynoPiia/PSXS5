/*
 * PSXS5 interface preview - stand-ins for the emulator and RetroAchievements,
 * so the screens run on a PC with a fake game picture.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../../src/core/host.h"
#include "../../src/ra/achievements.h"

#include <stdlib.h>
#include <string.h>

/* ---- the core: a striped test picture instead of a game */
static bool loaded;
static uint32_t frame[320 * 240];

bool host_load(const char *game_path, const Paths *paths, const Settings *settings, char *error,
               size_t error_size)
{
    (void)game_path; (void)paths; (void)settings; (void)error; (void)error_size;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
        {
            uint32_t sky = 0xff203a8au + (uint32_t)(y / 4) * 0x010203u;
            bool ground = y > 160, block = ((x / 32) + (y / 32)) % 2 == 0;
            frame[y * 320 + x] = ground ? (block ? 0xff3f8a3fu : 0xff2f6a2fu) : sky;
            if ((x - 230) * (x - 230) + (y - 70) * (y - 70) < 500)
                frame[y * 320 + x] = 0xfff0d050u;
        }
    loaded = true;
    return true;
}
void host_unload(void) { loaded = false; }
bool host_loaded(void) { return loaded; }
void host_set_pads(const PadState pads[PSXS5_MAX_PADS]) { (void)pads; }
bool host_pad_digital(int port) { (void)port; return false; }
void host_run_frame(void) {}
void host_reset(void) {}
void host_apply_settings(const Settings *settings) { (void)settings; }
double host_fps(void) { return 59.94; }
int host_sample_rate(void) { return 44100; }
float host_aspect(void) { return 4.0f / 3.0f; }
const void *host_frame(int *width, int *height, size_t *pitch, int *pixel_format, bool *fresh)
{
    *width = 320;
    *height = 240;
    *pitch = 320 * 4;
    *pixel_format = 1;
    *fresh = true;
    return loaded ? frame : NULL;
}
bool host_save_state(const char *path) { (void)path; return true; }
bool host_load_state(const char *path) { (void)path; return true; }
const struct retro_memory_map *host_memory_map(void) { return NULL; }
bool host_read_sector(uint32_t lba, uint8_t out[2048]) { (void)lba; (void)out; return false; }
int host_disc_count(void) { return 2; }
int host_disc_index(void) { return 0; }
bool host_disc_select(int index) { (void)index; return true; }

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index; (void)enabled; (void)code;
}

/* ---- RetroAchievements: a signed-in account with a game in progress */
static bool hardcore;
void ra_init(const Paths *paths) { (void)paths; }
void ra_shutdown(void) {}
void ra_game_loaded(void) {}
void ra_game_unloaded(void) {}
void ra_frame(void) {}
void ra_idle(void) {}
void ra_reset(void) {}
bool ra_signed_in(void) { return true; }
const char *ra_user(void) { return "SynoPiia"; }
unsigned ra_user_score(void) { return 1240; }
bool ra_game_progress(int *unlocked, int *total)
{
    if (!loaded)
        return false;
    *unlocked = 3;
    *total = 12;
    return true;
}
bool ra_hardcore(void) { return hardcore; }
void ra_set_hardcore(bool on) { hardcore = on; }
void ra_game_summary(char *out, size_t size) { if (size) out[0] = '\0'; }
bool ra_next_message(char *title, size_t title_size, char *detail, size_t detail_size)
{
    static bool shown;
    const char *banner = getenv("PSXS5_BANNER");
    if (shown || !loaded || !banner)
        return false;
    shown = true;
    strncpy(title, "Wumpa Collector", title_size - 1);
    strncpy(detail, "Collect 100 Wumpa fruit in one level  (10 points)", detail_size - 1);
    return true;
}
