// PSXS5 - frame generation for the presenter (fg_bridge.h): PS5SX2's interpolator
// (third_party/framegen/ps5_framegen.cpp) behind a C interface, its Vulkan
// commands looked up from PSXS5's device.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../../src/platform/vk/fg_bridge.h"
#include "ps5_framegen.h"

#include <cstdio>
#include <new>
#include <string>

#define FG_VK_DEFINE(name) PFN_##name name;
FG_VK_INSTANCE_FUNCS(FG_VK_DEFINE)
FG_VK_DEVICE_FUNCS(FG_VK_DEFINE)
#undef FG_VK_DEFINE

struct FgInterpolator
{
    ps5::framegen::interpolator fg;
};

extern "C" FgInterpolator *psxs5_fg_create(PFN_vkVoidFunction (*get_instance_proc)(VkInstance, const char *),
                                           VkInstance instance, VkPhysicalDevice gpu, VkDevice device, uint32_t width,
                                           uint32_t height, VkFormat format, char *error, size_t error_size)
{
    bool loaded = true;
#define FG_VK_LOAD_INSTANCE(name)                                                                  \
    name = reinterpret_cast<PFN_##name>(get_instance_proc(instance, #name));                       \
    loaded = loaded && name;
    FG_VK_INSTANCE_FUNCS(FG_VK_LOAD_INSTANCE)
#undef FG_VK_LOAD_INSTANCE
#define FG_VK_LOAD_DEVICE(name)                                                                    \
    name = vkGetDeviceProcAddr ? reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)) : nullptr; \
    loaded = loaded && name;
    FG_VK_DEVICE_FUNCS(FG_VK_LOAD_DEVICE)
#undef FG_VK_LOAD_DEVICE
    if (!loaded)
    {
        std::snprintf(error, error_size, "a Vulkan command is missing");
        return nullptr;
    }
    FgInterpolator *fg = new (std::nothrow) FgInterpolator;
    if (!fg)
    {
        std::snprintf(error, error_size, "no memory");
        return nullptr;
    }
    std::string why;
    if (!fg->fg.create(gpu, device, width, height, why, format))
    {
        std::snprintf(error, error_size, "%s", why.c_str());
        delete fg;
        return nullptr;
    }
    return fg;
}

extern "C" void psxs5_fg_destroy(FgInterpolator *fg)
{
    if (fg)
        fg->fg.destroy();
    delete fg;
}

extern "C" void psxs5_fg_prepare(FgInterpolator *fg, VkCommandBuffer cmd)
{
    fg->fg.prepare(cmd);
}

extern "C" VkImage psxs5_fg_frame_image(FgInterpolator *fg)
{
    return fg->fg.frame_image();
}

extern "C" VkImageView psxs5_fg_frame_view(FgInterpolator *fg)
{
    return fg->fg.frame_view();
}

extern "C" bool psxs5_fg_record(FgInterpolator *fg, VkCommandBuffer cmd, bool reset)
{
    return fg->fg.record(cmd, reset);
}

extern "C" VkImageView psxs5_fg_output_view(FgInterpolator *fg)
{
    return fg->fg.output_view();
}

extern "C" void psxs5_fg_advance(FgInterpolator *fg)
{
    fg->fg.advance();
}
