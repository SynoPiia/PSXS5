/*
 * ps5-native-app-boilerplate - Self-update example title.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Checks the catalog for a newer release of itself, asks before updating,
 * shows the download and the unpacking with the time left, and closes so the
 * helper can put the new version in place.
 *
 * The screen is the template's CPU renderer: enough for a prompt and a
 * progress bar. An app with a real interface draws its own from the same
 * self_update_status values. Every step is also written to the kernel log and
 * to /download0/self-update.txt, each line starting "SELF-UPDATE:".
 */
#include "../../../src/demo_renderer.hpp"
#include "../self_update.h"
#include "update_check.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <string_view>

#ifndef SELF_UPDATE_RUN_TAG
#define SELF_UPDATE_RUN_TAG local
#endif
#define EXAMPLE_STR2(x) #x
#define EXAMPLE_STR(x) EXAMPLE_STR2(x)

namespace
{
using ps5::demo::Canvas;
using ps5::demo::Color;

constexpr char report_path[] = "/download0/self-update.txt";
constexpr std::uint32_t button_circle = 0x00002000;
constexpr std::uint32_t button_cross = 0x00004000;

// scePadRead sample layout (120 bytes).
struct PadSample
{
    std::uint32_t buttons;
    std::uint8_t sticks_and_triggers[6];
    std::uint8_t reserved0[66];
    std::int32_t connected;
    std::uint64_t timestamp_us;
    std::uint8_t reserved1[32];
};
static_assert(sizeof(PadSample) == 120);

extern "C"
{
    int sceKernelClose(int descriptor);
    int sceKernelOpen(const char *path, int flags, int mode);
    std::int64_t sceKernelRead(int descriptor, void *buffer, std::size_t length);
    std::int64_t sceKernelWrite(int descriptor, const void *buffer, std::size_t length);
    int sceKernelDebugOutText(int channel, const char *text);
    std::uint64_t sceKernelGetProcessTime(void);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceSystemServiceLoadExec(const char *path, const char **arguments);
    int sceUserServiceInitialize(const void *parameters);
    int sceUserServiceGetInitialUser(int *user);
    int scePadInit(void);
    int scePadOpen(int user, int type, int index, const void *parameters);
    int scePadRead(int handle, PadSample *samples, int count);
}

enum class Screen
{
    checking,
    nothing, // up to date, or nothing can be said
    offer,
    working,
    closing,
    failed
};

int report_file = -1;
int pad = -1;
bool pad_tried = false;
std::uint32_t held = 0;
Screen screen = Screen::checking;
std::atomic<int> check_done{0};
self_update_check_result check_result = SELF_UPDATE_UNKNOWN;
self_update_offer offer{};
self_update_job job{};
self_update_phase last_phase = SELF_UPDATE_IDLE;
std::uint64_t screen_since = 0;
std::uint64_t last_progress_line = 0;
std::array<char, 96> message{};

std::uint64_t now_ms() noexcept
{
    return sceKernelGetProcessTime() / 1000u;
}

void emit(const char *line) noexcept
{
    std::array<char, 400> text{};
    const int length = std::snprintf(text.data(), text.size(), "SELF-UPDATE: %s\n", line);
    if (length <= 0)
        return;
    (void)sceKernelDebugOutText(0, text.data());
    if (report_file >= 0)
        (void)sceKernelWrite(report_file, text.data(), std::strlen(text.data()));
}

void show(Screen next) noexcept
{
    screen = next;
    screen_since = now_ms();
}

[[noreturn]] void close_app() noexcept
{
    if (report_file >= 0)
        (void)sceKernelClose(report_file);
    // A native title ends through the system, never by returning or calling exit().
    (void)sceSystemServiceLoadExec("exit", nullptr);
    for (;;)
        (void)sceKernelUsleep(1000000);
}

// The buttons that went down since the last frame.
std::uint32_t pressed() noexcept
{
    if (!pad_tried)
    {
        int user = 0;
        pad_tried = true;
        (void)sceUserServiceInitialize(nullptr);
        if (sceUserServiceGetInitialUser(&user) == 0 && scePadInit() >= 0)
            pad = scePadOpen(user, 0, 0, nullptr);
        emit(pad >= 0 ? "controller ready" : "no controller");
    }
    if (pad < 0)
        return 0;
    static std::array<PadSample, 16> samples;
    const int count = scePadRead(pad, samples.data(), static_cast<int>(samples.size()));
    std::uint32_t down = 0;
    for (int i = 0; i < count; ++i)
    {
        const std::uint32_t buttons = samples[static_cast<std::size_t>(i)].buttons;
        down |= buttons & ~held;
        held = buttons;
    }
    return down;
}

// The renderer's font has capitals, digits and a few signs; everything else becomes a space.
void draw_text(Canvas &canvas, unsigned x, unsigned y, const char *text, unsigned scale,
               Color color) noexcept
{
    std::array<char, 96> upper{};
    std::size_t length = 0;
    for (; text[length] != '\0' && length + 1 < upper.size(); ++length)
    {
        const char c = text[length];
        upper[length] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    }
    canvas.text(x, y, std::string_view{upper.data(), length}, scale, color);
}

void megabytes(char *out, std::size_t size, std::uint64_t bytes) noexcept
{
    (void)std::snprintf(out, size, "%llu.%llu MB",
                        static_cast<unsigned long long>(bytes / 1000000u),
                        static_cast<unsigned long long>(bytes % 1000000u / 100000u));
}

#ifdef SELF_UPDATE_DEV_OFFER
// Development only: takes the offer from /app0/assets/offer.txt instead of the catalog, so the
// download and the helper can be tried with a title that isn't listed. It skips the catalog's
// signature, so never ship a build with it. Five lines: the new content version, the version's
// name, the release ZIP on GitHub, its SHA-256, its size in bytes. Any lines after those are the
// release notes.
bool dev_line(const char *&at, char *out, std::size_t size) noexcept
{
    std::size_t length = 0;
    while (*at != '\0' && *at != '\n' && *at != '\r' && length + 1 < size)
        out[length++] = *at++;
    out[length] = '\0';
    while (*at == '\n' || *at == '\r')
        ++at;
    return length != 0;
}

self_update_check_result dev_offer() noexcept
{
    static std::array<char, 2048> text{};
    std::array<char, 24> size{};
    const char *at = text.data();
    const int descriptor = sceKernelOpen("/app0/assets/offer.txt", O_RDONLY, 0);
    if (descriptor < 0)
        return SELF_UPDATE_UNKNOWN;
    const std::int64_t count = sceKernelRead(descriptor, text.data(), text.size() - 1);
    (void)sceKernelClose(descriptor);
    std::memset(&offer, 0, sizeof(offer));
    if (count <= 0 ||
        !update_check_read_param("/app0/sce_sys/param.json", offer.title, offer.installed) ||
        !dev_line(at, offer.available, sizeof(offer.available)) ||
        !dev_line(at, offer.version, sizeof(offer.version)) ||
        !dev_line(at, offer.artifact, sizeof(offer.artifact)) ||
        !dev_line(at, offer.sha256, sizeof(offer.sha256)) ||
        !dev_line(at, size.data(), size.size()))
        return SELF_UPDATE_UNKNOWN;
    (void)std::snprintf(offer.name, sizeof(offer.name), "Self-Update Example");
    offer.size = std::strtoull(size.data(), nullptr, 10);
    (void)std::snprintf(offer.notes, sizeof(offer.notes), "%s", at);
    return SELF_UPDATE_AVAILABLE;
}
#endif

void *check_thread(void *) noexcept
{
#ifdef SELF_UPDATE_DEV_OFFER
    emit("DEVELOPMENT OFFER: the catalog and its signature are skipped");
    check_result = dev_offer();
#else
    check_result = self_update_check_self(&offer);
#endif
    check_done.store(1);
    return nullptr;
}

void after_check() noexcept
{
    std::array<char, 380> line{};
    static const char *const names[] = {"available", "up-to-date", "unknown", "untrusted",
                                        "not-installable"};
    (void)std::snprintf(
        line.data(), line.size(),
        "check result=%s installed=%s available=%s version=%s size=%llu notes=%zu%s "
        "ms=%llu",
        names[check_result], offer.installed[0] != '\0' ? offer.installed : "-",
        offer.available[0] != '\0' ? offer.available : "-",
        offer.version[0] != '\0' ? offer.version : "-", static_cast<unsigned long long>(offer.size),
        std::strlen(offer.notes), offer.notes_truncated != 0 ? "+" : "",
        static_cast<unsigned long long>(now_ms()));
    emit(line.data());
    if (check_result == SELF_UPDATE_AVAILABLE)
    {
        show(Screen::offer);
        return;
    }
    (void)std::snprintf(message.data(), message.size(), "%s",
                        check_result == SELF_UPDATE_UP_TO_DATE ? "This is the newest version"
                        : check_result == SELF_UPDATE_UNTRUSTED
                            ? "The catalog could not be verified"
                        : check_result == SELF_UPDATE_NOT_INSTALLABLE
                            ? "A newer version exists but cannot be installed here"
                            : "No update information");
    show(Screen::nothing);
}

void start_update() noexcept
{
    emit("update accepted");
    if (self_update_start(&job, self_update_console(), &offer) != 1)
    {
        (void)std::snprintf(message.data(), message.size(), "The update could not start");
        emit("failed reason=The update could not start");
        show(Screen::failed);
        return;
    }
    last_phase = SELF_UPDATE_IDLE;
    show(Screen::working);
}

void draw_working(Canvas &canvas, const self_update_status &status) noexcept
{
    std::array<char, 96> line{};
    std::array<char, 24> done{};
    std::array<char, 24> total{};
    const bool unpacking = status.phase == SELF_UPDATE_UNPACKING;
    draw_text(canvas, 160, 300,
              status.phase == SELF_UPDATE_STARTING ? "Preparing"
              : unpacking                          ? "Unpacking"
              : status.phase == SELF_UPDATE_READY  ? "Finishing"
                                                   : "Downloading",
              9, Color::white);
    canvas.rectangle(160, 470, 1600, 46, Color::panel);
    if (status.total != 0)
    {
        const std::uint64_t filled =
            status.done >= status.total ? 1600u : status.done * 1600u / status.total;
        canvas.rectangle(160, 470, static_cast<unsigned>(filled), 46, Color::cyan);
        megabytes(done.data(), done.size(), status.done);
        megabytes(total.data(), total.size(), status.total);
        (void)std::snprintf(line.data(), line.size(), "%s of %s   %u%%", done.data(), total.data(),
                            static_cast<unsigned>(status.done >= status.total
                                                      ? 100u
                                                      : status.done * 100u / status.total));
        draw_text(canvas, 160, 560, line.data(), 5, Color::white);
    }
    draw_text(canvas, 160, 640, status.time_left, 5, Color::yellow);
    if (status.phase != SELF_UPDATE_READY)
        draw_text(canvas, 160, 900, "Circle: cancel", 5, Color::white);
}

void log_progress(const self_update_status &status) noexcept
{
    std::array<char, 200> line{};
    static const char *const names[] = {"idle",  "starting", "downloading", "unpacking",
                                        "ready", "applying", "cancelled",   "failed"};
    const std::uint64_t now = now_ms();
    if (status.phase == last_phase && now - last_progress_line < 1000u)
        return;
    last_phase = status.phase;
    last_progress_line = now;
    (void)std::snprintf(line.data(), line.size(), "phase=%s done=%llu total=%llu rate=%llu left=%s",
                        names[status.phase], static_cast<unsigned long long>(status.done),
                        static_cast<unsigned long long>(status.total),
                        static_cast<unsigned long long>(status.rate),
                        status.time_left[0] != '\0' ? status.time_left : "-");
    emit(line.data());
}

void draw_frame(Canvas &canvas) noexcept
{
    const std::uint32_t down = pressed();
    const std::uint64_t shown_ms = now_ms() - screen_since;
    std::array<char, 96> line{};

    canvas.clear(Color::background);
    draw_text(canvas, 160, 120, "Self-update example", 8, Color::white);
    canvas.rectangle(160, 210, 1600, 6, Color::white);

    switch (screen)
    {
    case Screen::checking:
        draw_text(canvas, 160, 300, "Checking for updates", 7, Color::white);
        if (check_done.load() != 0)
            after_check();
        break;
    case Screen::nothing:
        draw_text(canvas, 160, 300, message.data(), 6, Color::white);
        (void)std::snprintf(line.data(), line.size(), "Installed %s", offer.installed);
        if (offer.installed[0] != '\0')
            draw_text(canvas, 160, 400, line.data(), 5, Color::cyan);
#ifdef SELF_UPDATE_EXIT_AFTER
        if (shown_ms > static_cast<std::uint64_t>(SELF_UPDATE_EXIT_AFTER) * 1000u)
        {
            emit("closing: nothing to do");
            close_app();
        }
#endif
        break;
    case Screen::offer:
        draw_text(canvas, 160, 300, "Update available", 9, Color::yellow);
        (void)std::snprintf(line.data(), line.size(), "Version %s", offer.version);
        draw_text(canvas, 160, 440, line.data(), 6, Color::white);
        (void)std::snprintf(line.data(), line.size(), "Installed %s   New %s", offer.installed,
                            offer.available);
        draw_text(canvas, 160, 530, line.data(), 4, Color::cyan);
        if (offer.size != 0)
        {
            std::array<char, 24> size{};
            megabytes(size.data(), size.size(), offer.size);
            (void)std::snprintf(line.data(), line.size(), "Download %s", size.data());
            draw_text(canvas, 160, 600, line.data(), 4, Color::cyan);
        }
        draw_text(canvas, 160, 820, "Cross: update now", 6, Color::white);
        draw_text(canvas, 160, 900, "Circle: later", 6, Color::white);
        if ((down & button_cross) != 0)
            start_update();
        else if ((down & button_circle) != 0)
        {
            emit("update declined");
            (void)std::snprintf(message.data(), message.size(), "Update skipped");
            show(Screen::nothing);
        }
#ifdef SELF_UPDATE_AUTO_ACCEPT
        // For scripted console runs: accept as if Cross had been pressed.
        else if (shown_ms > static_cast<std::uint64_t>(SELF_UPDATE_AUTO_ACCEPT) * 1000u)
            start_update();
#endif
        break;
    case Screen::working:
    {
        self_update_status status;
        self_update_poll(&job, &status);
        log_progress(status);
        draw_working(canvas, status);
        if (status.phase == SELF_UPDATE_READY)
        {
            // The user already said yes: go ahead as soon as the update is staged.
            if (self_update_apply(&job) == 1)
            {
                emit("applying: the app closes now and the helper replaces its files");
                show(Screen::closing);
            }
        }
        else if (status.phase == SELF_UPDATE_FAILED || status.phase == SELF_UPDATE_CANCELLED)
        {
            self_update_finish(&job);
            (void)std::snprintf(message.data(), message.size(), "%s",
                                status.phase == SELF_UPDATE_CANCELLED ? "Update cancelled"
                                                                      : status.error);
            (void)std::snprintf(line.data(), line.size(), "%s reason=%s",
                                status.phase == SELF_UPDATE_CANCELLED ? "cancelled" : "failed",
                                message.data());
            emit(line.data());
            show(Screen::failed);
        }
        else if ((down & button_circle) != 0)
        {
            emit("cancel pressed");
            self_update_cancel(&job);
        }
        break;
    }
    case Screen::closing:
        draw_text(canvas, 160, 300, "Updating", 9, Color::yellow);
        draw_text(canvas, 160, 440, "The app closes now", 6, Color::white);
        draw_text(canvas, 160, 530, "Open it again when the notification says it is updated", 4,
                  Color::cyan);
        if (shown_ms > 2500u)
            close_app();
        break;
    case Screen::failed:
        draw_text(canvas, 160, 300, "Not updated", 9, Color::magenta);
        draw_text(canvas, 160, 440, message.data(), 4, Color::white);
        draw_text(canvas, 160, 530, "Nothing was changed", 4, Color::cyan);
#ifdef SELF_UPDATE_EXIT_AFTER
        if (shown_ms > static_cast<std::uint64_t>(SELF_UPDATE_EXIT_AFTER) * 1000u)
        {
            emit("closing: not updated");
            close_app();
        }
#endif
        break;
    }
    (void)shown_ms;
}

#ifdef SELF_UPDATE_WATCHDOG
// For scripted console runs: the title ends itself after this many seconds whatever else
// happens, so a run can never leave it open.
void *watchdog_thread(void *) noexcept
{
    (void)sceKernelUsleep(static_cast<std::uint32_t>(SELF_UPDATE_WATCHDOG) * 1000000u);
    emit("watchdog: closing");
    close_app();
}
#endif
} // namespace

int main()
{
    pthread_t checker{};
    report_file = sceKernelOpen(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    emit("start tag=" EXAMPLE_STR(SELF_UPDATE_RUN_TAG));
#ifdef SELF_UPDATE_WATCHDOG
    pthread_t watchdog{};
    if (pthread_create(&watchdog, nullptr, watchdog_thread, nullptr) == 0)
        (void)pthread_detach(watchdog);
#endif
    screen_since = now_ms();
    // The check blocks on the network, so it never runs on the thread that draws.
    if (pthread_create(&checker, nullptr, check_thread, nullptr) != 0)
        check_done.store(1);
    else
        (void)pthread_detach(checker);
    ps5::demo::run_frames(draw_frame, "Self-update example started");
}
