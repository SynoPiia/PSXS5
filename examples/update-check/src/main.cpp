/*
 * ps5-native-app-boilerplate - Update-check example title.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs the update check for itself and for the titles listed in
 * /app0/assets/targets.txt, and reports each answer three ways: a line in the
 * kernel log, a line in /download0/update-check.txt, and one notification.
 *
 * A real app calls update_check_run_self() once, on a worker thread, and shows
 * its own notice. This title has no interface, so it checks on its main thread.
 */
#include "../update_check.h"

#ifndef UPDATE_CHECK_USE_SCEHTTP
#include "../console_curl.h"

#include <cerrno>
#include <curl/curl.h>
#include <sys/socket.h>
#include <unistd.h>
#define EXAMPLE_TRANSPORT "libcurl"
#else
#define EXAMPLE_TRANSPORT "sceHttp"
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>

#ifndef UPDATE_CHECK_RUN_TAG
#define UPDATE_CHECK_RUN_TAG local
#endif
#define EXAMPLE_STR2(x) #x
#define EXAMPLE_STR(x) EXAMPLE_STR2(x)

namespace
{
constexpr char targets_path[] = "/app0/assets/targets.txt";
constexpr char report_path[] = "/download0/update-check.txt";

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

extern "C"
{
    int sceKernelClose(int descriptor);
    int sceKernelOpen(const char *path, int flags, int mode);
    std::int64_t sceKernelRead(int descriptor, void *buffer, std::size_t length);
    std::int64_t sceKernelWrite(int descriptor, const void *buffer, std::size_t length);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelDebugOutText(int channel, const char *text);
    std::uint64_t sceKernelGetProcessTime(void);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceSystemServiceHideSplashScreen(void);
    int sceSystemServiceLoadExec(const char *path, const char **arguments);
}

int report_file = -1;
int answered = 0;
int requested = 0;

void emit(const char *line) noexcept
{
    std::array<char, 400> text{};
    const int length = std::snprintf(text.data(), text.size(), "UPDATE-CHECK: %s\n", line);
    if (length <= 0)
        return;
    (void)sceKernelDebugOutText(0, text.data());
    if (report_file >= 0)
        (void)sceKernelWrite(report_file, text.data(), std::strlen(text.data()));
}

const char *state_text(update_check_state state) noexcept
{
    switch (state)
    {
    case UPDATE_CHECK_UP_TO_DATE:
        return "up-to-date";
    case UPDATE_CHECK_AVAILABLE:
        return "update-available";
    case UPDATE_CHECK_UNKNOWN:
        break;
    }
    return "unknown";
}

void report(const char *label, const update_check_result &result,
            std::uint64_t microseconds) noexcept
{
    std::array<char, 360> line{};
    (void)std::snprintf(line.data(), line.size(),
                        "%s installed=%s state=%s reason=%s http=%d error=0x%08x available=%s "
                        "version=%s page=%s ms=%llu",
                        label, result.installed[0] != '\0' ? result.installed : "-",
                        state_text(result.state), update_check_reason_text(result.reason),
                        result.http_status, static_cast<unsigned>(result.platform_error),
                        result.available[0] != '\0' ? result.available : "-",
                        result.version[0] != '\0' ? result.version : "-",
                        result.page[0] != '\0' ? result.page : "-",
                        static_cast<unsigned long long>(microseconds / 1000u));
    emit(line.data());
#ifndef UPDATE_CHECK_USE_SCEHTTP
    if (result.platform_error <= -10000)
    {
        const auto code = static_cast<CURLcode>(-result.platform_error - 10000);
        (void)std::snprintf(line.data(), line.size(), "%s curl=%d (%s)", label,
                            static_cast<int>(code), curl_easy_strerror(code));
        emit(line.data());
    }
#endif
    ++requested;
    if (result.http_status != 0)
        ++answered;
}

void check(const char *title_id, const char *installed) noexcept
{
    update_check_result result{};
    const std::uint64_t start = sceKernelGetProcessTime();
    update_check_run(title_id, installed, &result);
    report(title_id, result, sceKernelGetProcessTime() - start);
}

// One target per line: a title ID, a space, the content version to pretend is installed.
// Anything else (blank lines, lines starting with #) is ignored.
void check_targets() noexcept
{
    static std::array<char, 4096> text{};
    const int descriptor = sceKernelOpen(targets_path, 0, 0);
    if (descriptor < 0)
    {
        emit("no targets file; only the self check ran");
        return;
    }
    std::size_t used = 0;
    while (used < text.size() - 1)
    {
        const std::int64_t got =
            sceKernelRead(descriptor, text.data() + used, text.size() - 1 - used);
        if (got <= 0)
            break;
        used += static_cast<std::size_t>(got);
    }
    (void)sceKernelClose(descriptor);
    text[used] = '\0';

    char *line = text.data();
    while (*line != '\0')
    {
        char *end = line;
        while (*end != '\0' && *end != '\n')
            ++end;
        const bool last = *end == '\0';
        *end = '\0';
        if (end > line && end[-1] == '\r')
            end[-1] = '\0';
        if (std::strlen(line) == 20 && line[9] == ' ')
        {
            line[9] = '\0';
            check(line, line + 10);
        }
        if (last)
            break;
        line = end + 1;
    }
}
} // namespace

int main()
{
    (void)sceSystemServiceHideSplashScreen();
    report_file = sceKernelOpen(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    emit("start tag=" EXAMPLE_STR(UPDATE_CHECK_RUN_TAG) " host=" UPDATE_CHECK_HOST
                                                        " transport=" EXAMPLE_TRANSPORT);
#ifndef UPDATE_CHECK_USE_SCEHTTP
    {
        std::array<char, 220> line{};
        (void)std::snprintf(line.data(), line.size(), "curl=%s ca=%s",
                            curl_version_info(CURLVERSION_NOW)->version, console_curl_ca_file());
        emit(line.data());

        // What the console answers to curl's way of making a socket non-blocking, and to its own.
        const int probe = socket(AF_INET, SOCK_STREAM, 0);
        errno = 0;
        const int set = fcntl(probe, F_SETFL, O_NONBLOCK);
        const int set_errno = errno;
        int on = 0;
        socklen_t length = sizeof(on);
        (void)getsockopt(probe, SOL_SOCKET, 0x1200, &on, &length);
        const int own = console_curl_nonblocking(probe);
        int after = 0;
        length = sizeof(after);
        (void)getsockopt(probe, SOL_SOCKET, 0x1200, &after, &length);
        (void)close(probe);
        (void)std::snprintf(line.data(), line.size(),
                            "sockets fcntl-nonblock=%d errno=%d nbio-after-fcntl=%d so-nbio=%d "
                            "nbio-after=%d",
                            set, set_errno, on, own, after);
        emit(line.data());
    }
#endif

    {
        update_check_result result{};
        const std::uint64_t start = sceKernelGetProcessTime();
        update_check_run_self(&result);
        report("self", result, sceKernelGetProcessTime() - start);
    }
    check_targets();

    std::array<char, 96> summary{};
    (void)std::snprintf(summary.data(), summary.size(), "done requests=%d answered=%d", requested,
                        answered);
    emit(summary.data());
    if (report_file >= 0)
        (void)sceKernelClose(report_file);

    NotificationRequest notification{};
    (void)std::snprintf(notification.message, sizeof(notification.message),
                        "Update check: %d of %d requests answered", answered, requested);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);

#ifdef UPDATE_CHECK_EXIT_AFTER
    // For scripted console runs: leave time to read the report, then end the title the way the
    // system expects. A native title must not call exit() itself.
    (void)sceKernelUsleep(static_cast<std::uint32_t>(UPDATE_CHECK_EXIT_AFTER) * 1000000u);
    (void)sceSystemServiceLoadExec("exit", nullptr);
#endif
    // Keep the title available for normal shell-mediated closure.
    for (;;)
        (void)sceKernelUsleep(2000000);
}
