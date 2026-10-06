/*
 * PSXS5 - minimal HTTPS download (libcurl).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_NET_H
#define PSXS5_NET_H

#include "psxs5.h"

typedef enum
{
    NET_OK,
    NET_NOT_FOUND,   /* the server answered 404: the file does not exist */
    NET_UNAVAILABLE, /* no network, DNS, TLS... worth giving up for this session */
} NetResult;

bool net_available(void); /* false when built without libcurl */
/* Downloads `url` to `dest` (written to dest.part, then renamed). Blocking. */
NetResult net_download(const char *url, const char *dest);

#endif
