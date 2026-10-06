/*
 * PSXS5 - cover art: local lookup, background download and decoding.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_COVERS_H
#define PSXS5_COVERS_H

#include "library.h"
#include "platform/platform.h"

/* (Re)binds the cover cache to a freshly scanned library. */
void covers_start(const Library *lib, const Paths *paths, const Settings *settings);
void covers_stop(void);

/* Main thread, once per frame: uploads finished images, requests the covers
 * around `center` and frees the ones far away. */
void covers_update(int center);

/* Texture for game `index`; a generated title card until the art is ready. */
PlatTexture *covers_get(int index);

/* Downloads still queued, for the "Downloading covers" indicator. */
int covers_downloading(void);

/* URL of a cover in xlenore/psx-covers (also used by tools/psxs5_sync.py). */
void covers_url(char *out, size_t size, int style, const char *serial);

#endif
