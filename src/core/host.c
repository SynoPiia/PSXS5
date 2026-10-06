/*
 * PSXS5 - libretro host for the statically linked cores.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The cores are linked into PSXS5 as static archives: PCSX-ReARMed
 * (libpcsx_rearmed.a, plain retro_*) and, in v2 builds, Beetle PSX HW
 * (libbeetle_psx.a, its retro_* renamed beetle_retro_* by
 * tools/build-beetle.sh). A table of functions picks one per game. Only the
 * environment callbacks the cores rely on are implemented; everything else
 * answers "unsupported" as libretro allows.
 */
#include "host.h"

#include "libretro.h"
#include "../platform/platform.h"
#if defined(PSXS5_VULKAN)
#include "../platform/vk/vk_present.h"
#include "../platform/vk/vk_present_hw.h" /* before libretro_vulkan.h: no prototypes */
#include "libretro_vulkan.h"
#endif
#include "../platform/ps5_crash.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OPTIONS 200

/* ---------------------------------------------------------------- the cores */

typedef struct
{
    const char *name;
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*init)(void);
    void (*deinit)(void);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void (*get_system_av_info)(struct retro_system_av_info *);
    void (*set_controller_port_device)(unsigned, unsigned);
    void (*run)(void);
    void (*reset)(void);
    size_t (*serialize_size)(void);
    bool (*serialize)(void *, size_t);
    bool (*unserialize)(const void *, size_t);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
} CoreApi;

#define CORE_API(label, p)                                                                         \
    {                                                                                              \
        label, p##retro_set_environment, p##retro_set_video_refresh, p##retro_set_audio_sample,  \
            p##retro_set_audio_sample_batch, p##retro_set_input_poll, p##retro_set_input_state,  \
            p##retro_init, p##retro_deinit, p##retro_load_game, p##retro_unload_game,            \
            p##retro_get_system_av_info, p##retro_set_controller_port_device, p##retro_run,      \
            p##retro_reset, p##retro_serialize_size, p##retro_serialize, p##retro_unserialize,   \
            p##retro_get_memory_data, p##retro_get_memory_size                                  \
    }

static const CoreApi PCSX = CORE_API("PCSX-ReARMed", );

#if defined(PSXS5_VULKAN)
void beetle_retro_set_environment(retro_environment_t);
void beetle_retro_set_video_refresh(retro_video_refresh_t);
void beetle_retro_set_audio_sample(retro_audio_sample_t);
void beetle_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void beetle_retro_set_input_poll(retro_input_poll_t);
void beetle_retro_set_input_state(retro_input_state_t);
void beetle_retro_init(void);
void beetle_retro_deinit(void);
bool beetle_retro_load_game(const struct retro_game_info *);
void beetle_retro_unload_game(void);
void beetle_retro_get_system_av_info(struct retro_system_av_info *);
void beetle_retro_set_controller_port_device(unsigned, unsigned);
void beetle_retro_run(void);
void beetle_retro_reset(void);
size_t beetle_retro_serialize_size(void);
bool beetle_retro_serialize(void *, size_t);
bool beetle_retro_unserialize(const void *, size_t);
void *beetle_retro_get_memory_data(unsigned);
size_t beetle_retro_get_memory_size(unsigned);
static const CoreApi BEETLE = CORE_API("Beetle PSX HW", beetle_);
#endif

static const CoreApi *core = &PCSX;

typedef struct
{
    char key[64];
    char value[64];
} Option;

static Option options[MAX_OPTIONS];
static int option_count;
static bool options_dirty;

static const Paths *host_paths;
static PadState pad_state[PSXS5_MAX_PADS];
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static struct retro_system_av_info av_info;
static struct retro_disk_control_ext_callback disk;
static bool disk_available;
static bool loaded;

static char patches_dir[PSXS5_PATH_MAX];
static bool multitap;

void psxs5_set_patches_dir(const char *dir); /* core: tools/patches/pcsx_rearmed-patchesdir.patch */

static struct retro_memory_descriptor memory_descriptors[32];
static struct retro_memory_map memory_map;

static const void *frame_data;
static unsigned frame_w, frame_h;
static size_t frame_pitch;
static bool frame_fresh;

/* ---------------------------------------------------------------- options */

static Option *find_option(const char *key)
{
    for (int i = 0; i < option_count; ++i)
        if (strcmp(options[i].key, key) == 0)
            return &options[i];
    return NULL;
}

static void set_option(const char *key, const char *value)
{
    Option *opt = find_option(key);
    if (!opt)
    {
        if (option_count >= MAX_OPTIONS)
            return;
        opt = &options[option_count++];
        str_copy(opt->key, sizeof(opt->key), key);
    }
    if (strcmp(opt->value, value) != 0)
    {
        str_copy(opt->value, sizeof(opt->value), value);
        options_dirty = true;
    }
}

/* Legacy variables look like "Description; default|other|...". */
static void register_variables(const struct retro_variable *vars)
{
    for (; vars && vars->key; ++vars)
    {
        if (find_option(vars->key))
            continue; /* keep PSXS5's override */
        const char *semi = strchr(vars->value, ';');
        const char *start = semi ? semi + 1 : vars->value;
        while (*start == ' ')
            ++start;
        char value[64];
        size_t n = strcspn(start, "|");
        if (n >= sizeof(value))
            n = sizeof(value) - 1;
        memcpy(value, start, n);
        value[n] = '\0';
        set_option(vars->key, value);
    }
}

#if defined(PSXS5_VULKAN)
static void apply_beetle_options(const Settings *s)
{
    static const char *const regions[] = {"auto", "ntsc-u", "pal"};
    static const char *const scales[] = {"1x(native)", "2x", "4x", "8x", "16x"};
    int level = s->internal_res >= 1 && s->internal_res <= 5 ? s->internal_res : 1;
    bool gpu = vkp_describe()[0] != '\0'; /* the screen runs through Vulkan */
    if (!gpu && level > 2)
        level = 2; /* the software renderer: 4x and up would not fit in memory */
    set_option("beetle_psx_hw_renderer", gpu ? "hardware_vk" : "software");
    set_option("beetle_psx_hw_internal_resolution", scales[level - 1]);
    set_option("beetle_psx_hw_region", regions[s->region % REGION_COUNT]);
    set_option("beetle_psx_hw_dither_mode", s->dithering ? "1x(native)" : "disabled");
    /* read as it plays: "precache" loads every disc of a game into memory,
     * and two discs already pass PSXS5's 1 GB */
    set_option("beetle_psx_hw_cd_access_method", "async");
    set_option("beetle_psx_hw_cd_fastload", s->cd_fast ? "4x" : "2x(native)");
    set_option("beetle_psx_hw_skip_bios", "enabled");
    set_option("beetle_psx_hw_pgxp_mode", s->pgxp ? "memory only" : "disabled");
    set_option("beetle_psx_hw_pgxp_texture", s->pgxp ? "enabled" : "disabled");
    set_option("beetle_psx_hw_widescreen_hack", s->widescreen ? "enabled" : "disabled");
    set_option("beetle_psx_hw_widescreen_hack_aspect_ratio", "16:9");
    set_option("beetle_psx_hw_analog_toggle", "enabled");
    /* the core writes saves/<disc name>.0.mcr itself */
    set_option("beetle_psx_hw_use_mednafen_memcard0_method", "mednafen");
    set_option("beetle_psx_hw_frame_duping", "enabled");
}
#endif

static void apply_settings_to_options(const Settings *s)
{
#if defined(PSXS5_VULKAN)
    if (core == &BEETLE)
    {
        apply_beetle_options(s);
        return;
    }
#endif
    static const char *regions[] = {"auto", "NTSC", "PAL"};
    set_option("pcsx_rearmed_region", regions[s->region % REGION_COUNT]);
    set_option("pcsx_rearmed_bios", s->force_hle ? "HLE" : "auto");
    set_option("pcsx_rearmed_dithering", s->dithering ? "enabled" : "disabled");
    set_option("pcsx_rearmed_cd_turbo", s->cd_fast ? "enabled" : "disabled");
    set_option("pcsx_rearmed_rgb32_output", "enabled");
    set_option("pcsx_rearmed_memcard1", "serial");   /* one card per game, managed by the core */
    set_option("pcsx_rearmed_show_bios_bootlogo", "disabled");
    set_option("pcsx_rearmed_vibration", "enabled");
    set_option("pcsx_rearmed_display_fps_v2", "disabled");
    /* 2x internal resolution: the enhanced GPU renders the 3D scene at double size. */
    set_option("pcsx_rearmed_neon_enhancement_enable", s->internal_res >= 2 ? "enabled" : "disabled");
    set_option("pcsx_rearmed_neon_enhancement_no_main", "disabled");
    /* players 3 and 4 through a multitap in port 1 */
    set_option("pcsx_rearmed_multitap", s->multitap ? "port 1" : "disabled");
    /* the widescreen codes need the picture's sides drawn */
    set_option("pcsx_rearmed_show_overscan", s->widescreen ? "hack" : "disabled");
}

/* ---------------------------------------------------------------- Vulkan rendering */

#if defined(PSXS5_VULKAN)
/* Beetle PSX HW renders through Vulkan: it asks for a Vulkan context
 * (SET_HW_RENDER), creates the device itself through the negotiation
 * interface, and hands over a finished image each frame (set_image). The
 * screen (vk_present.c) moves onto that device and draws the image. */
static struct retro_hw_render_callback hw;
static bool hw_requested, hw_running;
static const struct retro_hw_render_context_negotiation_interface_vulkan *negotiation;
static struct retro_hw_render_interface_vulkan hw_interface;

static void hw_set_image(void *handle, const struct retro_vulkan_image *image, uint32_t num_semaphores,
                         const VkSemaphore *semaphores, uint32_t src_queue_family)
{
    (void)handle, (void)num_semaphores, (void)semaphores, (void)src_queue_family;
    vkp_set_game_image(image ? image->image_view : VK_NULL_HANDLE,
                       image ? image->image_layout : VK_IMAGE_LAYOUT_UNDEFINED);
}
static uint32_t hw_get_sync_index(void *handle)
{
    (void)handle;
    return vkp_sync_index();
}
static uint32_t hw_get_sync_index_mask(void *handle)
{
    (void)handle;
    return vkp_sync_index_mask();
}
static void hw_wait_sync_index(void *handle)
{
    (void)handle;
    vkp_wait_sync_index();
}
static void hw_set_command_buffers(void *handle, uint32_t num, const VkCommandBuffer *cmd)
{
    (void)handle, (void)num, (void)cmd; /* Beetle submits its own */
}
static void hw_queue_noop(void *handle)
{
    (void)handle; /* one thread submits: no lock needed */
}
static void hw_set_signal_semaphore(void *handle, VkSemaphore semaphore)
{
    (void)handle, (void)semaphore;
}

/* After retro_load_game: the core's device, the screen moved onto it, then
 * context_reset builds the renderer. */
static bool hw_start(char *error, size_t size)
{
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkSurfaceKHR surface;
    PFN_vkGetInstanceProcAddr gipa;
    vkp_hw_context(&instance, &gpu, &surface, &gipa);
    struct retro_vulkan_context context;
    memset(&context, 0, sizeof(context));
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (!negotiation || !negotiation->create_device ||
        !negotiation->create_device(&context, instance, gpu, surface, gipa, extensions, 1, NULL, 0, NULL))
    {
        snprintf(error, size, "Beetle could not create its Vulkan device.");
        return false;
    }
    char why[160];
    if (!vkp_adopt_device(context.device, context.queue, context.queue_family_index, why, sizeof(why)))
    {
        snprintf(error, size, "The screen could not move to Beetle's device: %s", why);
        return false;
    }
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    PFN_vkGetDeviceProcAddr gdpa;
    vkp_hw_device(&device, &queue, &family, &gdpa);
    memset(&hw_interface, 0, sizeof(hw_interface));
    hw_interface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    hw_interface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    hw_interface.instance = instance;
    hw_interface.gpu = context.gpu ? context.gpu : gpu;
    hw_interface.device = device;
    hw_interface.get_device_proc_addr = gdpa;
    hw_interface.get_instance_proc_addr = gipa;
    hw_interface.queue = queue;
    hw_interface.queue_index = family;
    hw_interface.set_image = hw_set_image;
    hw_interface.get_sync_index = hw_get_sync_index;
    hw_interface.get_sync_index_mask = hw_get_sync_index_mask;
    hw_interface.set_command_buffers = hw_set_command_buffers;
    hw_interface.wait_sync_index = hw_wait_sync_index;
    hw_interface.lock_queue = hw_queue_noop;
    hw_interface.unlock_queue = hw_queue_noop;
    hw_interface.set_signal_semaphore = hw_set_signal_semaphore;
    hw_running = true;
    if (hw.context_reset)
        hw.context_reset();
    psxs5_log("host: Beetle renders through Vulkan");
    return true;
}

static void hw_stop(void)
{
    if (hw_running && hw.context_destroy)
        hw.context_destroy();
    hw_running = false;
    hw_requested = false;
    negotiation = NULL;
    vkp_set_game_image(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED);
}
#endif

/* ---------------------------------------------------------------- callbacks */

static void RETRO_CALLCONV core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_INFO)
        return;
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    psxs5_log("core: %s", line);
}

static float rumble_scale = 1.0f; /* Settings > Controls > Vibration */

static bool RETRO_CALLCONV rumble_cb(unsigned port, enum retro_rumble_effect effect,
                                     uint16_t strength)
{
    static uint16_t strong[PSXS5_MAX_PADS], weak[PSXS5_MAX_PADS];
    if (port >= PSXS5_MAX_PADS)
        return false;
    if (effect == RETRO_RUMBLE_STRONG)
        strong[port] = strength;
    else
        weak[port] = strength;
    plat_rumble((int)port, (uint16_t)(strong[port] * rumble_scale),
                (uint16_t)(weak[port] * rumble_scale));
    return true;
}

static bool RETRO_CALLCONV environment(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 0; /* core falls back to SET_VARIABLES */
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        register_variables((const struct retro_variable *)data);
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        struct retro_variable *var = (struct retro_variable *)data;
        Option *opt = find_option(var->key);
        var->value = opt ? opt->value : NULL;
        return opt != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = options_dirty;
        options_dirty = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    {
        enum retro_pixel_format fmt = *(const enum retro_pixel_format *)data;
        if (fmt > RETRO_PIXEL_FORMAT_RGB565)
            return false;
        pixel_format = fmt;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = host_paths->bios;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = host_paths->saves;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        ((struct retro_rumble_interface *)data)->set_rumble_state = rumble_cb;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        *(unsigned *)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        memset(&disk, 0, sizeof(disk));
        memcpy(&disk, data, sizeof(struct retro_disk_control_callback));
        disk_available = true;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        memcpy(&disk, data, sizeof(disk));
        disk_available = true;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        av_info = *(const struct retro_system_av_info *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        av_info.geometry = *(const struct retro_game_geometry *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    {
        /* kept for RetroAchievements: where PS1 RAM and scratchpad live */
        const struct retro_memory_map *m = data;
        memory_map.num_descriptors = m->num_descriptors < 32 ? m->num_descriptors : 32;
        memcpy(memory_descriptors, m->descriptors,
               memory_map.num_descriptors * sizeof(struct retro_memory_descriptor));
        memory_map.descriptors = memory_descriptors;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        psxs5_log("core message: %s", ((const struct retro_message *)data)->msg);
        return true;
#if defined(PSXS5_VULKAN)
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(unsigned *)data = RETRO_HW_CONTEXT_VULKAN;
        return vkp_describe()[0] != '\0';
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
    {
        struct retro_hw_render_callback *cb = data;
        if (core != &BEETLE || cb->context_type != RETRO_HW_CONTEXT_VULKAN || !vkp_describe()[0])
            return false;
        hw = *cb;
        hw_requested = true;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
    {
        const struct retro_hw_render_context_negotiation_interface *i = data;
        if (i->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
            return false;
        negotiation = data;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT:
    {
        struct retro_hw_render_context_negotiation_interface *i = data;
        if (i->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
            return false;
        i->interface_version = 1; /* create_device, not create_device2 */
        return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
        if (!hw_running)
            return false;
        *(const struct retro_hw_render_interface **)data = (const struct retro_hw_render_interface *)&hw_interface;
        return true;
#endif
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        psxs5_log("core message: %s", ((const struct retro_message_ext *)data)->msg);
        return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *(unsigned *)data = 1;
        return true;
    default:
        return false;
    }
}

static void RETRO_CALLCONV video_cb(const void *data, unsigned width, unsigned height,
                                    size_t pitch)
{
    if (!data)
        return; /* duplicate frame: keep showing the previous one */
    if (data == RETRO_HW_FRAME_BUFFER_VALID)
    {
        /* rendered on the GPU: the image went through set_image */
        frame_data = NULL;
        frame_w = width;
        frame_h = height;
        frame_fresh = true;
        return;
    }
    frame_data = data;
    frame_w = width;
    frame_h = height;
    frame_pitch = pitch;
    frame_fresh = true;
}

static void RETRO_CALLCONV audio_cb(int16_t left, int16_t right)
{
    int16_t frame[2] = {left, right};
    plat_audio_push(frame, 1);
}

static size_t RETRO_CALLCONV audio_batch_cb(const int16_t *data, size_t frames)
{
    plat_audio_push(data, frames);
    return frames;
}

static void RETRO_CALLCONV input_poll_cb(void)
{
}

static int16_t RETRO_CALLCONV input_state_cb(unsigned port, unsigned device, unsigned index,
                                             unsigned id)
{
    if (port >= PSXS5_MAX_PADS || !pad_state[port].connected)
        return 0;
    const PadState *p = &pad_state[port];
    switch (device & RETRO_DEVICE_MASK)
    {
    case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return (int16_t)(p->buttons & 0xffff);
        return id < 16 ? (int16_t)((p->buttons >> id) & 1) : 0;
    case RETRO_DEVICE_ANALOG:
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? p->lx : p->ly;
        if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? p->rx : p->ry;
        return 0;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------- API */

static const CoreApi *choose_core(const Settings *settings)
{
#if defined(PSXS5_VULKAN)
    /* Automatic stays on PCSX-ReARMed until Beetle draws through Vulkan */
    if (settings->emulator == EMU_BEETLE)
        return &BEETLE;
#else
    (void)settings;
#endif
    return &PCSX;
}

const char *host_core_name(void)
{
    return core->name;
}

bool host_load(const char *game_path, const Paths *paths, const Settings *settings,
               char *error, size_t error_size)
{
    host_unload();
    core = choose_core(settings);
    psxs5_log("host: emulator %s", core->name);
    host_paths = paths;
    option_count = 0;
    disk_available = false;
    frame_data = NULL;
    pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
    rumble_scale = settings->rumble ? (settings->rumble_strength + 1) * 0.25f : 0.0f;
    apply_settings_to_options(settings);

#define STEP(s) (psxs5_log("host: %s", s), ps5_crash_step(s))
    STEP("retro_set_environment");
    core->set_environment(environment);
    core->set_video_refresh(video_cb);
    core->set_audio_sample(audio_cb);
    core->set_audio_sample_batch(audio_batch_cb);
    core->set_input_poll(input_poll_cb);
    core->set_input_state(input_state_cb);
    STEP("retro_init");
    core->init();
    if (core == &PCSX)
        psxs5_set_patches_dir(patches_dir);

    STEP("retro_load_game");
    struct retro_game_info info = {game_path, NULL, 0, NULL};
    if (!core->load_game(&info))
    {
        snprintf(error, error_size, "The core could not load this game.");
        core->deinit();
        return false;
    }
#if defined(PSXS5_VULKAN)
    if (hw_requested && !hw_start(error, error_size))
    {
        psxs5_log("%s", error);
        hw_stop();
        core->unload_game();
        core->deinit();
        return false;
    }
#endif
    STEP("retro_get_system_av_info");
    core->get_system_av_info(&av_info);
    /* DualShock starts in digital mode, so it is also safe for digital-only games. */
    unsigned device = settings->analog ? RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
                                       : RETRO_DEVICE_JOYPAD;
    multitap = settings->multitap;
    for (unsigned port = 0; port < (multitap ? 4u : 2u); ++port)
        core->set_controller_port_device(port, device);
    loaded = true;
    STEP("running");
#undef STEP
    psxs5_log("loaded %s: %.3f fps, %.0f Hz, base %ux%u", game_path, av_info.timing.fps,
              av_info.timing.sample_rate, av_info.geometry.base_width,
              av_info.geometry.base_height);
    return true;
}

void host_unload(void)
{
    if (!loaded)
        return;
    core->unload_game();
#if defined(PSXS5_VULKAN)
    hw_stop();
#endif
    core->deinit();
    loaded = false;
    frame_data = NULL;
    for (int i = 0; i < PSXS5_MAX_PADS; ++i)
        plat_rumble(i, 0, 0);
}

bool host_loaded(void)
{
    return loaded;
}

void *host_memory_data(unsigned id)
{
    return loaded ? core->get_memory_data(id) : NULL;
}

size_t host_memory_size(unsigned id)
{
    return loaded ? core->get_memory_size(id) : 0;
}

const struct retro_memory_map *host_memory_map(void)
{
    return loaded && memory_map.num_descriptors ? &memory_map : NULL;
}

/* The core's CD layer (libpcsxcore/cdrom-async.h): reads through whatever
 * image format is loaded (bin/cue, CHD, PBP...). */
int cdra_readTrack(const unsigned char *time);
void *cdra_getBuffer(void);

bool host_read_sector(uint32_t lba, uint8_t out[2048])
{
    if (!loaded || core != &PCSX)
        return false; /* RetroAchievements then hashes the disc image itself */
    unsigned abs = lba + 150; /* sector 0 is at 00:02:00 */
    /* minute, second, frame as plain numbers: the core's cdra_readTrack takes
     * them through msf2sec, not as the BCD the PS1's CD commands use */
    unsigned char time[3] = {(unsigned char)(abs / 75 / 60), (unsigned char)(abs / 75 % 60),
                             (unsigned char)(abs % 75)};
    if (cdra_readTrack(time) != 0)
        return false;
    const uint8_t *buf = cdra_getBuffer();
    if (!buf)
        return false;
    memcpy(out, buf + 12, 2048); /* skip MSF/mode + subheader: Mode 2 Form 1 data */
    return true;
}

int padGetMode(unsigned int index); /* core, added by tools/patches/pcsx_rearmed-padgetmode.patch */

bool host_pad_digital(int port)
{
    return loaded && core == &PCSX && padGetMode((unsigned)port) == 0;
}

void host_set_pads(const PadState pads[PSXS5_MAX_PADS])
{
    memcpy(pad_state, pads, sizeof(pad_state));
}

void host_run_frame(void)
{
    if (loaded)
        core->run();
}

void host_reset(void)
{
    if (loaded)
        core->reset();
}

void host_apply_settings(const Settings *settings)
{
    rumble_scale = settings->rumble ? (settings->rumble_strength + 1) * 0.25f : 0.0f;
    apply_settings_to_options(settings);
}

double host_fps(void)
{
    return av_info.timing.fps > 1.0 ? av_info.timing.fps : 59.94;
}

int host_sample_rate(void)
{
    return av_info.timing.sample_rate > 1000.0 ? (int)(av_info.timing.sample_rate + 0.5) : 44100;
}

float host_aspect(void)
{
    return av_info.geometry.aspect_ratio > 0.0f ? av_info.geometry.aspect_ratio : 4.0f / 3.0f;
}

const void *host_frame(int *width, int *height, size_t *pitch, int *format, bool *fresh)
{
    *width = (int)frame_w;
    *height = (int)frame_h;
    *pitch = frame_pitch;
    *format = (int)pixel_format;
    *fresh = frame_fresh;
    frame_fresh = false;
    return frame_data;
}

bool host_save_state(const char *path)
{
    size_t size = core->serialize_size();
    if (!loaded || size == 0)
        return false;
    void *buffer = malloc(size);
    if (!buffer)
        return false;
    bool ok = core->serialize(buffer, size);
    if (ok)
    {
        char temp[PSXS5_PATH_MAX];
        snprintf(temp, sizeof(temp), "%s.tmp", path);
        FILE *f = fopen(temp, "wb");
        ok = f && fwrite(buffer, 1, size, f) == size;
        if (f)
            ok = (fclose(f) == 0) && ok;
        ok = ok && rename(temp, path) == 0;
    }
    free(buffer);
    return ok;
}

void host_set_patches_dir(const char *dir)
{
    str_copy(patches_dir, sizeof(patches_dir), dir ? dir : "");
}

size_t host_state_size(void)
{
    return loaded ? core->serialize_size() : 0;
}

bool host_serialize(void *buffer, size_t size)
{
    return loaded && core->serialize(buffer, size);
}

bool host_unserialize(const void *buffer, size_t size)
{
    if (!loaded || !core->unserialize(buffer, size))
        return false;
    plat_audio_clear();
    return true;
}

bool host_capture(uint8_t *rgba, int w, int h)
{
    if (!loaded || !frame_data || frame_w == 0 || frame_h == 0)
        return false;
    for (int y = 0; y < h; ++y)
    {
        unsigned sy = (unsigned)((y * 2 + 1) * frame_h / (2 * (unsigned)h));
        const uint8_t *row = (const uint8_t *)frame_data + sy * frame_pitch;
        for (int x = 0; x < w; ++x)
        {
            unsigned sx = (unsigned)((x * 2 + 1) * frame_w / (2 * (unsigned)w));
            uint8_t *o = &rgba[((size_t)y * w + x) * 4];
            if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
            {
                uint32_t c = ((const uint32_t *)row)[sx];
                o[0] = (c >> 16) & 0xff, o[1] = (c >> 8) & 0xff, o[2] = c & 0xff;
            }
            else
            {
                uint16_t c = ((const uint16_t *)row)[sx];
                if (pixel_format == RETRO_PIXEL_FORMAT_RGB565)
                    o[0] = (c >> 11) << 3, o[1] = ((c >> 5) & 63) << 2, o[2] = (c & 31) << 3;
                else
                    o[0] = ((c >> 10) & 31) << 3, o[1] = ((c >> 5) & 31) << 3, o[2] = (c & 31) << 3;
            }
            o[3] = 255;
        }
    }
    return true;
}

bool host_load_state(const char *path)
{
    if (!loaded)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = false;
    void *buffer = size > 0 ? malloc((size_t)size) : NULL;
    if (buffer && fread(buffer, 1, (size_t)size, f) == (size_t)size)
        ok = core->unserialize(buffer, (size_t)size);
    free(buffer);
    fclose(f);
    if (ok)
        plat_audio_clear();
    return ok;
}

int host_disc_count(void)
{
    return disk_available && disk.get_num_images ? (int)disk.get_num_images() : 1;
}

int host_disc_index(void)
{
    return disk_available && disk.get_image_index ? (int)disk.get_image_index() : 0;
}

bool host_disc_select(int index)
{
    if (!disk_available || index < 0 || index >= host_disc_count())
        return false;
    /* Open the lid, swap, close it: the game sees a normal disc change. */
    if (!disk.set_eject_state(true))
        return false;
    bool ok = disk.set_image_index((unsigned)index);
    disk.set_eject_state(false);
    return ok;
}
