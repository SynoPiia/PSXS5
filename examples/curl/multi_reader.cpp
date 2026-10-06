/*
 * ps5-native-app-boilerplate - A blocking send/read/abort API on curl multi.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * For large downloads and live streams (docs/CURL.md, "Large downloads and
 * streams"). send() returns when the headers of the final response are in,
 * read() blocks until it has at least one byte and returns 0 at the end of the
 * body, and abort() may come from another thread. There is no curl worker
 * thread: the thread that calls send() or read() drives the transfer.
 *
 *   - A Connection owns one CURLM, so requests made on it reuse the socket and
 *     the TLS session.
 *   - A Request is one easy handle on that multi.
 *   - Body bytes nobody has read are held in a buffer; above 1 MiB the write
 *     callback pauses the transfer, below 512 KiB read() resumes it.
 *   - Failures are -(10000 + CURLcode), as in the update check.
 *
 * Not part of any build target: copy it into your sources beside
 * console_curl.h. Lint compiles it; the code it was simplified from ran on a
 * console in ProsperoRadio.
 */
#include "../update-check/console_curl.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <time.h>
#include <utility>

namespace
{
constexpr std::size_t high_water = 1024 * 1024;
constexpr int error_base = 10000;
constexpr int wait_ms = 10;

int failure(CURLcode code) noexcept
{
    return -(error_base + static_cast<int>(code));
}

std::int64_t now_ms() noexcept
{
    timespec now{};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}
} // namespace

struct Connection
{
    Connection() = default;
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    ~Connection()
    {
        (void)curl_multi_cleanup(multi);
    }

    CURLM *multi = curl_multi_init();
};

class Request
{
  public:
    Request(Connection &connection, std::string url) : connection_(connection), url_(std::move(url))
    {
    }
    Request(const Request &) = delete;
    Request &operator=(const Request &) = delete;

    ~Request()
    {
        if (easy_ != nullptr)
        {
            (void)curl_multi_remove_handle(connection_.multi, easy_);
            curl_easy_cleanup(easy_);
        }
    }

    // Starts the transfer and waits for the final response's headers.
    int send(long connect_ms, long receive_ms)
    {
        receive_ms_ = receive_ms;
        easy_ = curl_easy_init();
        if (easy_ == nullptr)
            return failure(CURLE_FAILED_INIT);
        console_curl_setup(easy_); // no signals, the console's CA list, non-blocking sockets
        (void)curl_easy_setopt(easy_, CURLOPT_URL, url_.c_str());
        (void)curl_easy_setopt(easy_, CURLOPT_HTTP_VERSION,
                               static_cast<long>(CURL_HTTP_VERSION_1_1));
        (void)curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 1L);
        (void)curl_easy_setopt(easy_, CURLOPT_MAXREDIRS, 8L);
        (void)curl_easy_setopt(easy_, CURLOPT_BUFFERSIZE, 256L * 1024L);
        (void)curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "https");
        (void)curl_easy_setopt(easy_, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        // The connect limit covers the name lookup too.
        (void)curl_easy_setopt(easy_, CURLOPT_CONNECTTIMEOUT_MS, connect_ms);
        (void)curl_easy_setopt(easy_, CURLOPT_HEADERFUNCTION, &Request::on_header);
        (void)curl_easy_setopt(easy_, CURLOPT_HEADERDATA, this);
        (void)curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &Request::on_body);
        (void)curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);
        if (curl_multi_add_handle(connection_.multi, easy_) != CURLM_OK)
            return failure(CURLE_FAILED_INIT);

        const std::int64_t deadline = now_ms() + connect_ms + receive_ms;
        while (!headers_done_ && !done_)
        {
            if (aborted_.load())
                return failure(CURLE_ABORTED_BY_CALLBACK);
            if (now_ms() > deadline)
                return failure(CURLE_OPERATION_TIMEDOUT);
            drive(wait_ms);
        }
        return done_ && result_ != CURLE_OK ? failure(result_) : 0;
    }

    long status() const noexcept
    {
        return status_;
    }

    // >0 bytes copied, 0 at the end of the body, <0 a failure.
    int read(void *data, std::size_t size)
    {
        if (size == 0)
            return 0;
        const std::int64_t quiet_since = now_ms();
        for (;;)
        {
            if (aborted_.load())
                return failure(CURLE_ABORTED_BY_CALLBACK);
            const std::size_t available = body_.size() - taken_;
            if (available != 0)
            {
                const std::size_t count = available < size ? available : size;
                std::memcpy(data, body_.data() + taken_, count);
                taken_ += count;
                compact();
                // Resume a paused transfer once the reader has caught up. libcurl may call
                // on_body from inside curl_easy_pause; that is this same thread.
                if (paused_ && body_.size() - taken_ < high_water / 2)
                {
                    paused_ = false;
                    (void)curl_easy_pause(easy_, CURLPAUSE_CONT);
                }
                return static_cast<int>(count);
            }
            if (done_)
                return result_ == CURLE_OK ? 0 : failure(result_);
            if (now_ms() - quiet_since > receive_ms_)
                return failure(CURLE_OPERATION_TIMEDOUT);
            drive(wait_ms);
        }
    }

    // Safe from any thread while the request exists: curl_multi_wakeup is the one multi call
    // documented as safe from another thread.
    void abort()
    {
        aborted_.store(true);
        (void)curl_multi_wakeup(connection_.multi);
    }

  private:
    // One perform pass, and a short wait only when the pass brought nothing. On the console
    // curl_multi_poll does not always return early when the socket becomes readable, so
    // waiting after every pass holds a download to one buffer per wait.
    void drive(int idle_wait_ms)
    {
        const std::size_t before = body_.size();
        const bool had_headers = headers_done_;
        int running = 0;
        (void)curl_multi_perform(connection_.multi, &running);
        int queued = 0;
        while (const CURLMsg *message = curl_multi_info_read(connection_.multi, &queued))
        {
            if (message->msg == CURLMSG_DONE && message->easy_handle == easy_)
            {
                done_ = true;
                result_ = message->data.result;
                if (status_ == 0)
                    (void)curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status_);
            }
        }
        const bool progressed = body_.size() != before || headers_done_ != had_headers || done_;
        if (!progressed && idle_wait_ms > 0)
        {
            int ready = 0;
            (void)curl_multi_poll(connection_.multi, nullptr, 0, idle_wait_ms, &ready);
        }
    }

    void compact()
    {
        if (taken_ == body_.size())
        {
            body_.clear();
            taken_ = 0;
        }
        else if (taken_ >= high_water)
        {
            body_.erase(0, taken_);
            taken_ = 0;
        }
    }

    static std::size_t on_header(char *data, std::size_t size, std::size_t count, void *user)
    {
        auto *self = static_cast<Request *>(user);
        const std::size_t total = size * count;
        (void)data;
        if (total <= 2) // the blank line that ends a header block
        {
            long status = 0;
            (void)curl_easy_getinfo(self->easy_, CURLINFO_RESPONSE_CODE, &status);
            // 1xx, and redirects libcurl will follow, are not the answer.
            const bool passing = status < 200 || (status >= 300 && status < 400 && status != 304);
            if (!passing)
            {
                self->status_ = status;
                self->headers_done_ = true;
            }
        }
        return total;
    }

    static std::size_t on_body(char *data, std::size_t size, std::size_t count, void *user)
    {
        auto *self = static_cast<Request *>(user);
        if (self->body_.size() - self->taken_ >= high_water)
        {
            self->paused_ = true;
            return CURL_WRITEFUNC_PAUSE; // libcurl keeps this chunk and offers it again
        }
        self->headers_done_ = true;
        self->body_.append(data, size * count);
        return size * count;
    }

    Connection &connection_;
    std::string url_;
    CURL *easy_ = nullptr;
    std::string body_; // received; unread from taken_ on
    std::size_t taken_ = 0;
    long status_ = 0;
    long receive_ms_ = 5000;
    CURLcode result_ = CURLE_OK;
    bool headers_done_ = false;
    bool done_ = false;
    bool paused_ = false;
    std::atomic<bool> aborted_{false};
};

// Usage: one connection, one request per file, read to the end.
int download(Connection &connection, const char *url, std::string &out)
{
    Request request(connection, url);
    int result = request.send(10000, 5000);
    if (result < 0)
        return result;
    if (request.status() != 200)
        return -1;
    static char chunk[64 * 1024];
    while ((result = request.read(chunk, sizeof(chunk))) > 0)
        out.append(chunk, static_cast<std::size_t>(result));
    return result; // 0 at the end, or a failure
}
