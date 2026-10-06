/*
 * PSXS5 - minimal HTTPS download (libcurl).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PS5: PacBrew's libcurl plus the boilerplate's console_curl helpers (system
 * resolver, certificate list, non-blocking sockets). Desktop: system libcurl.
 */
#include "net.h"

#include <stdio.h>

#if defined(PSXS5_HAVE_CURL)
#include <curl/curl.h>
#if defined(__PROSPERO__)
#include "console_curl.h"
#endif

static size_t write_cb(void *data, size_t size, size_t count, void *user)
{
    return fwrite(data, size, count, (FILE *)user) * size;
}

bool net_available(void)
{
    static int state = -1;
    if (state < 0)
        state = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return state == 1;
}

NetResult net_download(const char *url, const char *dest)
{
    if (!net_available())
        return NET_UNAVAILABLE;
    char part[PSXS5_PATH_MAX];
    snprintf(part, sizeof(part), "%s.part", dest);
    FILE *f = fopen(part, "wb");
    if (!f)
        return NET_UNAVAILABLE;

    CURL *easy = curl_easy_init();
    if (!easy)
    {
        fclose(f);
        remove(part);
        return NET_UNAVAILABLE;
    }
#if defined(__PROSPERO__)
    console_curl_setup(easy);
#else
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
#endif
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, PSXS5_NAME "/" PSXS5_VERSION);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, f);

    CURLcode rc = curl_easy_perform(easy);
    long status = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(easy);
    bool written = fclose(f) == 0;

    if (rc == CURLE_OK && status == 200 && written && rename(part, dest) == 0)
        return NET_OK;
    remove(part);
    if (rc == CURLE_OK && status == 404)
        return NET_NOT_FOUND;
    psxs5_log("net: %s -> curl %d, http %ld", url, (int)rc, status);
    return rc == CURLE_OK ? NET_NOT_FOUND : NET_UNAVAILABLE;
}

#else

bool net_available(void)
{
    return false;
}

NetResult net_download(const char *url, const char *dest)
{
    (void)url;
    (void)dest;
    return NET_UNAVAILABLE;
}

#endif
