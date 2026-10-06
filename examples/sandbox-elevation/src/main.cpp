/*
 * ps5-native-app-boilerplate - Filesystem capability example.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../elevation.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace
{
constexpr char canary_path[] = "/data/hello-from-sandbox.txt";
constexpr char poc_path[] = "/data/PPSA99790-poc.txt";
#ifndef POC_RUN_TAG
#define POC_RUN_TAG poc_run
#endif
#define POC_STR2(x) #x
#define POC_STR(x) POC_STR2(x)

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

extern "C"
{
    int sceKernelClose(int descriptor);
    int sceKernelOpen(const char *path, int flags, mode_t mode);
    std::int64_t sceKernelRead(int descriptor, void *buffer, std::size_t length);
    std::int64_t sceKernelWrite(int descriptor, const void *buffer, std::size_t length);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelUsleep(std::uint32_t microseconds);
}

class KernelFile
{
  public:
    explicit KernelFile(int descriptor) noexcept : descriptor_{descriptor}
    {
    }
    ~KernelFile()
    {
        if (descriptor_ >= 0)
            (void)sceKernelClose(descriptor_);
    }
    KernelFile(const KernelFile &) = delete;
    KernelFile &operator=(const KernelFile &) = delete;
    [[nodiscard]] int get() const noexcept
    {
        return descriptor_;
    }

  private:
    int descriptor_;
};

void report(const char *message) noexcept
{
    NotificationRequest notification{};
    (void)std::snprintf(notification.message, sizeof(notification.message), "%s", message);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
}

bool verify_canary(int before_open) noexcept
{
    std::array<char, 128> expected{};
    std::array<char, 128> actual{};
    const int length = std::snprintf(expected.data(), expected.size(),
                                     "hello from the sandboxed app\n"
                                     "pid=%d before_open=%08x write_read=verified\n",
                                     getpid(), static_cast<std::uint32_t>(before_open));
    if (length <= 0 || static_cast<std::size_t>(length) >= expected.size())
        return false;
    {
        KernelFile output{sceKernelOpen(canary_path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        if (output.get() < 0 || sceKernelWrite(output.get(), expected.data(),
                                               static_cast<std::size_t>(length)) != length)
            return false;
    }
    KernelFile input{sceKernelOpen(canary_path, O_RDONLY, 0)};
    return input.get() >= 0 &&
           sceKernelRead(input.get(), actual.data(), static_cast<std::size_t>(length)) == length &&
           std::memcmp(actual.data(), expected.data(), static_cast<std::size_t>(length)) == 0;
}
// PoC on top of the example: write a distinctively-named /data file with a
// build-embedded run tag, then read it back and byte-compare. Independent of
// the canary so the run can be confirmed off-console over FTP afterwards.
bool write_poc_marker() noexcept
{
    std::array<char, 160> expected{};
    std::array<char, 160> actual{};
    const int length = std::snprintf(expected.data(), expected.size(),
                                     "PPSA99790 sandbox-elevation PoC\n"
                                     "tag=%s pid=%d\n",
                                     POC_STR(POC_RUN_TAG), getpid());
    if (length <= 0 || static_cast<std::size_t>(length) >= expected.size())
        return false;
    {
        KernelFile output{sceKernelOpen(poc_path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        if (output.get() < 0 || sceKernelWrite(output.get(), expected.data(),
                                               static_cast<std::size_t>(length)) != length)
            return false;
    }
    KernelFile input{sceKernelOpen(poc_path, O_RDONLY, 0)};
    return input.get() >= 0 &&
           sceKernelRead(input.get(), actual.data(), static_cast<std::size_t>(length)) == length &&
           std::memcmp(actual.data(), expected.data(), static_cast<std::size_t>(length)) == 0;
}
} // namespace

int main()
{
    int before_open = 0;
    {
        KernelFile directory{sceKernelOpen("/data", O_RDONLY, 0)};
        before_open = directory.get();
    }
    report(before_open < 0 ? "ELEVATION: /data is initially inaccessible"
                           : "ELEVATION: /data was already accessible");

    const auto result = elevation::request(elevation::Capability::filesystem);
    if (result == elevation::Status::ok)
    {
        const bool canary_ok = verify_canary(before_open);
        const bool poc_ok = write_poc_marker();
        std::array<char, 160> message{};
        (void)std::snprintf(message.data(), message.size(),
                            canary_ok && poc_ok
                                ? "ELEVATION: granted via %s; canary + PoC verified"
                                : "ELEVATION: granted via %s, but file verification failed",
                            elevation::path());
        report(message.data());
    }
    else
    {
        std::array<char, 128> message{};
        (void)std::snprintf(message.data(), message.size(),
                            "ELEVATION: request failed (status=%u); close this title",
                            static_cast<unsigned>(result));
        report(message.data());
    }

    // Keep the title available for normal shell-mediated closure, including errors.
    for (;;)
        (void)sceKernelUsleep(2000000);
}
