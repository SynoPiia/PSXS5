/*
 * PSXS5 - interface sounds, synthesised at start-up (no sample files).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_SFX_H
#define PSXS5_SFX_H

#include "../psxs5.h"

typedef enum
{
    SFX_CLICK,  /* moving along the shelf: a jewel case tapping its neighbour */
    SFX_SELECT, /* opening a game */
    SFX_BACK,
    SFX_COUNT
} Sfx;

void sfx_init(int sample_rate);
void sfx_play(Sfx sound);
void sfx_set_enabled(bool enabled);

#endif
