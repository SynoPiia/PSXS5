/*
 * PSXS5 - settings from a phone: a small web page on the local network.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One thread answers HTTP on REMOTE_PORT:
 *   GET /               assets/remote.html
 *   GET /api/settings   every setting as JSON (a snapshot the main thread keeps fresh)
 *   POST /api/set?key=K&value=V
 * Changes are queued and applied by the main thread (remote_frame), so the
 * settings are never touched from two threads.
 */
#include "remote.h"

#include "app.h"
#include "i18n.h"
#include "platform/platform.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* provided by ui/settings_screen.c */
int settings_json(char *out, size_t size);
const char *settings_set_by_key(const char *key, int value);

#if defined(_WIN32)

/* the PC preview has no server */
void remote_update(bool enabled) { (void)enabled; }
void remote_frame(void) {}
const char *remote_address(void) { return ""; }

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define SNAPSHOT_SIZE (96 * 1024)
#define PENDING 16

static SDL_Thread *thread;
static SDL_mutex *lock;
static SDL_atomic_t quit;
static int listen_fd = -1;
static char address[64];
static char *snapshot;
static size_t snapshot_len;
static struct
{
    char key[16];
    int value;
} pending[PENDING];
static int pending_count;

static void send_all(int fd, const char *data, size_t len)
{
    while (len > 0)
    {
        ssize_t n = send(fd, data, len, 0);
        if (n <= 0)
            return;
        data += n;
        len -= (size_t)n;
    }
}

static void respond(int fd, const char *status, const char *type, const char *body, size_t len)
{
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, type, len);
    send_all(fd, head, (size_t)n);
    send_all(fd, body, len);
}

static char *read_asset(const char *name, size_t *len)
{
    char path[PSXS5_PATH_MAX];
    plat_asset_path(path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = size > 0 ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(f);
    *len = data ? (size_t)size : 0;
    return data;
}

static const char *query(const char *q, const char *name, char *out, size_t size)
{
    size_t n = strlen(name);
    for (const char *p = q; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL)
        if (!strncmp(p, name, n) && p[n] == '=')
        {
            size_t len = strcspn(p + n + 1, "& \r\n");
            if (len >= size)
                len = size - 1;
            memcpy(out, p + n + 1, len);
            out[len] = '\0';
            return out;
        }
    return NULL;
}

static void serve(int fd)
{
    char req[4096];
    ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0)
        return;
    req[n] = '\0';
    char method[8] = "", path[512] = "";
    if (sscanf(req, "%7s %511s", method, path) != 2)
        return;
    if (!strcmp(path, "/") || !strcmp(path, "/index.html"))
    {
        size_t len;
        char *page = read_asset("remote.html", &len);
        if (page)
            respond(fd, "200 OK", "text/html; charset=utf-8", page, len);
        else
            respond(fd, "404 Not Found", "text/plain", "missing page", 12);
        free(page);
    }
    else if (!strcmp(path, "/api/settings"))
    {
        SDL_LockMutex(lock);
        char *copy = snapshot_len ? malloc(snapshot_len) : NULL;
        size_t len = snapshot_len;
        if (copy)
            memcpy(copy, snapshot, len);
        SDL_UnlockMutex(lock);
        if (copy)
            respond(fd, "200 OK", "application/json", copy, len);
        else
            respond(fd, "503 Service Unavailable", "application/json", "{}", 2);
        free(copy);
    }
    else if (!strcmp(method, "POST") && !strncmp(path, "/api/set?", 9))
    {
        char key[16], value[16];
        if (query(path + 9, "key", key, sizeof(key)) && query(path + 9, "value", value, sizeof(value)))
        {
            SDL_LockMutex(lock);
            if (pending_count < PENDING)
            {
                str_copy(pending[pending_count].key, sizeof(pending[0].key), key);
                pending[pending_count++].value = atoi(value);
            }
            SDL_UnlockMutex(lock);
            respond(fd, "200 OK", "application/json", "{\"ok\":true}", 11);
        }
        else
            respond(fd, "400 Bad Request", "application/json", "{\"ok\":false}", 12);
    }
    else
        respond(fd, "404 Not Found", "text/plain", "not found", 9);
}

static int server(void *unused)
{
    (void)unused;
    while (!SDL_AtomicGet(&quit))
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(listen_fd, &set);
        struct timeval tv = {0, 300000};
        if (select(listen_fd + 1, &set, NULL, NULL, &tv) <= 0)
            continue;
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0)
            continue;
        struct timeval to = {2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
        serve(fd);
        close(fd);
    }
    return 0;
}

/* The console's address on the local network: the source address a UDP
 * socket would use towards the internet (nothing is sent). */
static void find_address(void)
{
    address[0] = '\0';
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return;
    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808u);
    struct sockaddr_in me = {0};
    socklen_t len = sizeof(me);
    if (connect(s, (struct sockaddr *)&to, sizeof(to)) == 0 &&
        getsockname(s, (struct sockaddr *)&me, &len) == 0)
    {
        uint32_t a = ntohl(me.sin_addr.s_addr);
        snprintf(address, sizeof(address), "http://%u.%u.%u.%u:%d/", a >> 24, (a >> 16) & 255,
                 (a >> 8) & 255, a & 255, REMOTE_PORT);
    }
    close(s);
}

void remote_update(bool enabled)
{
    if (enabled == (thread != NULL))
        return;
    if (!enabled)
    {
        SDL_AtomicSet(&quit, 1);
        SDL_WaitThread(thread, NULL);
        thread = NULL;
        close(listen_fd);
        listen_fd = -1;
        address[0] = '\0';
        return;
    }
    if (!lock)
        lock = SDL_CreateMutex();
    if (!snapshot)
        snapshot = malloc(SNAPSHOT_SIZE);
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0 || !lock || !snapshot)
    {
        psxs5_log("remote: no socket");
        return;
    }
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(REMOTE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listen_fd, 4) != 0)
    {
        psxs5_log("remote: port %d unavailable", REMOTE_PORT);
        close(listen_fd);
        listen_fd = -1;
        return;
    }
    SDL_AtomicSet(&quit, 0);
    find_address();
    thread = SDL_CreateThread(server, "remote", NULL);
    psxs5_log("remote: settings page at %s", address[0] ? address : "(no address)");
}

void remote_frame(void)
{
    if (!thread)
        return;
    static uint64_t refreshed;
    uint64_t now = plat_ticks_us();
    SDL_LockMutex(lock);
    int count = pending_count;
    char keys[PENDING][16];
    int values[PENDING];
    for (int i = 0; i < count; ++i)
    {
        str_copy(keys[i], sizeof(keys[i]), pending[i].key);
        values[i] = pending[i].value;
    }
    pending_count = 0;
    SDL_UnlockMutex(lock);
    for (int i = 0; i < count; ++i)
    {
        const char *name = settings_set_by_key(keys[i], values[i]);
        if (name)
        {
            char msg[160];
            snprintf(msg, sizeof(msg), tr("Changed from your phone: %s"), tr(name));
            app_toast(msg);
            app_save_settings();
        }
    }
    if (count || now - refreshed > 500000)
    {
        static char fresh[SNAPSHOT_SIZE];
        int len = settings_json(fresh, sizeof(fresh));
        SDL_LockMutex(lock);
        memcpy(snapshot, fresh, (size_t)len);
        snapshot_len = (size_t)len;
        SDL_UnlockMutex(lock);
        refreshed = now;
    }
}

const char *remote_address(void)
{
    return address;
}

#endif
