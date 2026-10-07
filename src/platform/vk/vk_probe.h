/*
 * PSXS5 v2 - Vulkan bring-up probe (logs the GPU RADV finds).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_VK_PROBE_H
#define PSXS5_VK_PROBE_H

/* Does nothing unless built with PSXS5_VULKAN. */
void vk_probe(const char *root);

#endif
