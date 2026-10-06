/*
 * PSXS5 - the game shelf (home screen).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_COVERFLOW_H
#define PSXS5_COVERFLOW_H

#include "../library.h"

enum ShelfCategory
{
    CAT_ALL,
    CAT_RECENT,
    CAT_FAVORITES,
    CAT_MULTI_DISC,
    CAT_USA,
    CAT_EUROPE,
    CAT_JAPAN,
    CAT_COUNT
};

enum SortMode
{
    SORT_TITLE,
    SORT_RECENT,
    SORT_MOST_PLAYED,
    SORT_REGION,
    SORT_COUNT
};

/* The shelf's backdrop (also behind settings): a gradient tinted with the
 * selected game's cover colour. */
void shelf_backdrop(void);
/* Region name from a serial's prefix ("USA", "Europe", "Japan"), translated. */
const char *shelf_region_name(const char *serial);
const char *shelf_sort_name(int sort);

#endif
