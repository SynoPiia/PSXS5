/*
 * PSXS5 - the coverflow game library.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_COVERFLOW_H
#define PSXS5_COVERFLOW_H

#include "../library.h"

typedef enum
{
    CF_NONE,
    CF_PLAY,     /* launch animation finished: start library->games[cursor] */
    CF_SETTINGS,
} CoverflowAction;

typedef struct
{
    int cursor;
    float pos;       /* animated shelf position, eases towards cursor */
    bool details;    /* Triangle: info panel */
    float launch_t;  /* > 0 while the launch animation runs */
    float details_t; /* panel slide 0..1 */
} Coverflow;

void coverflow_init(Coverflow *cf, int cursor);
/* Handles input and draws one frame (between plat_begin_frame/plat_end_frame). */
CoverflowAction coverflow_frame(Coverflow *cf, const Library *lib, uint32_t pressed, float dt,
                                const char *notice);
/* Shared backdrop for the other screens. */
void coverflow_backdrop(void);

#endif
