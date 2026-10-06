/*
 * PSXS5 - asking the HEN to let PSXS5 list /data.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A sandboxed title can open and write files under /data but can't list
 * folders. etaHEN (and OnionHEN) watch each sandbox for a jailbreak request
 * file: {"PID":<pid>} in /download0/etahen_jailbreak. On firmware 13.60 this
 * is answered ("jailbreak granted"), while the boilerplate's Lapy helper is
 * not. Porpoise was killed right after its grant; it asks from a process that
 * already runs threads, so PSXS5 asks first thing in main(), single-threaded,
 * after giving itself its own credential.
 *
 * A marker file guards against a crash loop: it is written before asking and
 * removed afterwards. If PSXS5 finds it at start-up, the previous request
 * killed the app, so this route is skipped and PSXS5 runs sandboxed.
 */
#if defined(__PROSPERO__)
#include "ps5_unlock.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int sceKernelUsleep(unsigned microseconds);

static const char *const REQUESTS[] = {
    "/download0/etahen_jailbreak",   /* etaHEN, OnionHEN, old Lapy */
    "/download0/onionhen_jailbreak", /* OnionHEN also takes this name */
};
#define REQUEST_COUNT (int)(sizeof(REQUESTS) / sizeof(REQUESTS[0]))
#define CRASH_MARKER "/download0/psxs5_unlock_attempt"

bool ps5_data_listable(void)
{
    DIR *d = opendir("/data");
    if (!d)
        return false;
    closedir(d);
    return true;
}

static bool publish(const char *path, int pid)
{
    char staged[64];
    snprintf(staged, sizeof(staged), "%s.tmp", path);
    unlink(staged);
    unlink(path);
    int fd = open(staged, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        return false;
    fchmod(fd, 0666);
    char body[32];
    int n = snprintf(body, sizeof(body), "{\"PID\":%d}\n", pid);
    bool ok = write(fd, body, (size_t)n) == n && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(staged, path) != 0)
    {
        unlink(staged);
        return false;
    }
    return true;
}

UnlockResult ps5_unlock_etahen(void)
{
    if (ps5_data_listable())
        return UNLOCK_ALREADY;
    if (access(CRASH_MARKER, F_OK) == 0)
    {
        /* The previous launch died while asking: skip this one launch (no crash
         * loop), then ask again on the next. */
        unlink(CRASH_MARKER);
        return UNLOCK_SKIPPED_AFTER_CRASH;
    }
    int marker = open(CRASH_MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (marker >= 0)
    {
        fsync(marker);
        close(marker);
    }

    seteuid(geteuid()); /* own credential before the HEN changes it */
    UnlockResult result = UNLOCK_NO_ANSWER;
    const int pid = (int)getpid();
    for (int round = 0; round < 3 && result != UNLOCK_OK; ++round)
    {
        int published = 0;
        for (int i = 0; i < REQUEST_COUNT; ++i)
            published += publish(REQUESTS[i], pid);
        if (published == 0)
        {
            result = UNLOCK_CANT_REQUEST;
            break;
        }
        /* ~1.5 s for the HEN to act and /data to open up */
        for (int poll = 0; poll < 90; ++poll)
        {
            sceKernelUsleep(16667);
            if (ps5_data_listable())
            {
                result = UNLOCK_OK;
                break;
            }
        }
        for (int i = 0; i < REQUEST_COUNT; ++i)
            unlink(REQUESTS[i]);
    }
    unlink(CRASH_MARKER); /* we survived the request either way */
    return result;
}

void ps5_unlock_reset_crash_marker(void)
{
    unlink(CRASH_MARKER);
}

const char *ps5_unlock_describe(UnlockResult r)
{
    switch (r)
    {
    case UNLOCK_OK: return "granted by etaHEN";
    case UNLOCK_ALREADY: return "already unlocked";
    case UNLOCK_NO_ANSWER: return "etaHEN didn't answer";
    case UNLOCK_CANT_REQUEST: return "couldn't write the request";
    case UNLOCK_SKIPPED_AFTER_CRASH: return "skipped once after the last request closed PSXS5 (start again to retry)";
    }
    return "?";
}
#endif
