/* PSXS5 - frame generation (AMD FSR 3's frame interpolation, from PS5SX2: third_party/framegen),
 * for the presenter in C. From two of the game's frames, the one halfway between them.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FgInterpolator FgInterpolator;

/* frames of width x height in `format`; NULL (and `error`) when it can't be made */
FgInterpolator *psxs5_fg_create(PFN_vkVoidFunction (*get_instance_proc)(VkInstance, const char *), VkInstance instance,
                                VkPhysicalDevice gpu, VkDevice device, uint32_t width, uint32_t height, VkFormat format,
                                char *error, size_t error_size);
void psxs5_fg_destroy(FgInterpolator *fg);

/* each new frame of the game: prepare, copy the frame into frame_image (GENERAL layout), record;
 * record says whether output_view holds the frame halfway to the previous one */
void psxs5_fg_prepare(FgInterpolator *fg, VkCommandBuffer cmd);
VkImage psxs5_fg_frame_image(FgInterpolator *fg);
VkImageView psxs5_fg_frame_view(FgInterpolator *fg);
bool psxs5_fg_record(FgInterpolator *fg, VkCommandBuffer cmd, bool reset);
VkImageView psxs5_fg_output_view(FgInterpolator *fg);
/* once the frame was shown: it becomes the previous one */
void psxs5_fg_advance(FgInterpolator *fg);

#ifdef __cplusplus
}
#endif
