/*
 * PSXS5 - settings from a phone: a small web page on the local network.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_REMOTE_H
#define PSXS5_REMOTE_H

#include "psxs5.h"

#define REMOTE_PORT 8095

/* Starts or stops the server to match Settings > System > Settings from your phone. */
void remote_update(bool enabled);
/* Main thread, every frame: applies changes made on the phone. */
void remote_frame(void);
/* "http://192.168.1.20:8095/" once the server runs, else "". */
const char *remote_address(void);

#endif
