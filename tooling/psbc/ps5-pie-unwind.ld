/*
 * PS5 Vulkan compatibility probe - PIE layout for titles that link libunwind.
 * Copyright (C) 2026 Mihawk-99
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Milestone 5 Phase A1 (docs/M5_PHASE_A.md). The shader compiler's C++ code
 * (ACO) needs the SDK's libunwind, which finds unwind tables through these
 * boundary symbols rather than dl_iterate_phdr. Same definitions as
 * ps5-opengl's native-app/ps5-pie.ld. The layout itself is unchanged: link
 * with -L tooling/native so the base script is found.
 */

INCLUDE ps5-pie.ld

PROVIDE(__eh_frame_start = ADDR(.eh_frame));
PROVIDE(__eh_frame_end = ADDR(.eh_frame) + SIZEOF(.eh_frame));
PROVIDE(__eh_frame_hdr_start = ADDR(.eh_frame_hdr));
PROVIDE(__eh_frame_hdr_end = ADDR(.eh_frame_hdr) + SIZEOF(.eh_frame_hdr));
