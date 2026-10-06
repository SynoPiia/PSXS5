# libcurl in a PS5 native app

How to make HTTPS requests with libcurl and OpenSSL from an app built with
this template: what to add, why each piece is needed, how to handle large
downloads, and what was measured on a console.

Everything here was observed on our own consoles on 2026-10-02 with PacBrew
v0.40.2 (libcurl 8.18.0, OpenSSL 3.5.2), in three apps: ProsperoRadio (which
runs elevated), ProsperoLichess (sandboxed) and this repository's
[update check](UPDATE_CHECK.md) (sandboxed). Where something was seen but not
explained, it says so.

## Contents

1. [When to use libcurl](#1-when-to-use-libcurl)
2. [Quick start](#2-quick-start)
3. [What `console_curl.c` solves](#3-what-console_curlc-solves)
4. [After a PacBrew update: finding missing symbols](#4-after-a-pacbrew-update-finding-missing-symbols)
5. [Large downloads and streams](#5-large-downloads-and-streams)
6. [Elevated apps](#6-elevated-apps)
7. [Testing and diagnosing](#7-testing-and-diagnosing)
8. [What we learned on hardware](#8-what-we-learned-on-hardware)
9. [Troubleshooting](#9-troubleshooting)
10. [Licences](#10-licences)

| In this repository | What it is |
| --- | --- |
| [`examples/update-check/console_curl.h`, `console_curl.c`](../examples/update-check) | Everything libcurl needs to run on the console, and `console_curl_setup()` |
| [`examples/update-check/update_check.c`](../examples/update-check/update_check.c) | A complete GET with the easy interface (`update_check_fetch`) |
| [`examples/curl/multi_reader.cpp`](../examples/curl/multi_reader.cpp) | A blocking send/read/abort API on the multi interface, for large downloads |
| [`tools/find-missing-symbols.sh`](../tools/find-missing-symbols.sh) | Lists the libc symbols a PacBrew library needs that the console doesn't provide |

---

## 1. When to use libcurl

The console has its own HTTP library, `sceHttp` with `sceSsl`. It needs no
extra code and adds nothing to the app's size, and it works in a sandboxed
app: the update check's first console run used it.

It stops working when an app **elevates** (see
[Sandbox elevation](SANDBOX_ELEVATION.md)). With filesystem access outside the
sandbox, `sceHttp`/`sceSsl` rejected every public certificate with
`0x8095f00c`. Handing the system's certificates to `sceHttpsLoadCert` (which
returned 0) changed nothing, and the same build without elevation worked. We
found no way around it, so an elevated app needs a TLS stack of its own:
libcurl with OpenSSL, checking certificates against a file the app chooses.

| | `sceHttp` | libcurl |
| --- | --- | --- |
| Sandboxed app | Works | Works |
| Elevated app | Fails with `0x8095f00c` on every public site | Works |
| Added to the app | Nothing | About 7 MB (libcurl, OpenSSL, zlib, zstd, libpsl) and their [licence notices](#10-licences) |
| Extra code | None | `console_curl.c` and one link option |
| Redirects, resume, compression, HTTP semantics | Limited; reads return only when the buffer given is full | libcurl's |
| Same code on a PC | No | Yes |

Speed is similar. One 610-byte GET from the same site took 299 ms with libcurl
and 375 ms with `sceHttp`; a 70 MB catalogue of six files downloaded in about
64 s with either.

## 2. Quick start

1. Copy the support files into your sources:

   ```bash
   cp examples/update-check/console_curl.{h,c} src/
   ```

2. Link libcurl and wrap `fcntl`, in the `Makefile` or your `.env`:

   ```make
   PACBREW_PACKAGES += libcurl
   APP_WRAP_SYMBOLS += fcntl
   ```

   `PACBREW_PACKAGES` downloads the pinned PacBrew prefix (about 346 MB, once,
   checked by SHA-256, into the ignored `.deps/pacbrew/`) and resolves the
   package through pkg-config, which brings in OpenSSL, zlib, zstd and libpsl.
   `make pacbrew-list` shows the other package names. `APP_WRAP_SYMBOLS`
   makes the linker send every call of the listed functions to your
   `__wrap_<name>`, with `__real_<name>` as the original.

3. Call `console_curl_setup()` on every handle:

   ```cpp
   #include "console_curl.h"

   CURL *easy = curl_easy_init();
   console_curl_setup(easy);   // no signals, the console's CA list, non-blocking sockets
   curl_easy_setopt(easy, CURLOPT_URL, "https://example.org/");
   curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
   curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);   // covers the name lookup too
   curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 15000L);
   curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, on_body);
   curl_easy_setopt(easy, CURLOPT_WRITEDATA, &sink);
   const CURLcode result = curl_easy_perform(easy);
   curl_easy_cleanup(easy);
   ```

   Call `curl_global_init(CURL_GLOBAL_DEFAULT)` once first, from one thread.
   Never make a request on the render thread.

For a complete, bounded GET see `update_check_fetch` in
[`update_check.c`](../examples/update-check/update_check.c). For anything
larger than a small file, use the multi interface as in
[section 5](#5-large-downloads-and-streams).

## 3. What `console_curl.c` solves

PacBrew's archives are built for the payload SDK's libc, not for a native
title. They link once a few functions exist, and then fail in five ways on the
console. Each row was found on hardware.

| Problem | Symptom without the fix | The fix |
| --- | --- | --- |
| The name-lookup functions resolve to a module a native title doesn't load | Crash at address 0 on the first request | [Name lookup](#name-lookup) |
| libc functions the archives ask for don't exist on the console | The link fails, one symbol at a time | [libc stand-ins](#libc-stand-ins) |
| In the sandbox, libc refuses `fcntl` on sockets | Every request fails: `curl 7: fcntl set CLOEXEC: Invalid argument` | [The `fcntl` wrapper](#the-fcntl-wrapper) |
| curl's sockets stay blocking | A finished request doesn't return until the server closes the connection; downloads are several times slower | [Non-blocking sockets](#non-blocking-sockets) |
| OpenSSL knows no certificate store | `CURLE_SSL_CACERT_BADFILE` (77) | [Certificates](#certificates) |

`console_curl_setup(easy)` applies the per-handle parts: `CURLOPT_NOSIGNAL`
(no signals in a native title), `CURLOPT_CAINFO` and the socket callback.

The build keeps every symbol of the app internal
(`tooling/native/app-symbols.map`). Without that the linker would publish the
functions below as exports, because they replace system stubs, and the
converter refuses exports.

### Name lookup

In the SDK's import stubs, `getaddrinfo`, `freeaddrinfo`, `gai_strerror`,
`gethostbyname`, `getnameinfo` and `fnmatch` come only from
`libScePosixForWebKit`. A native title doesn't load that module, so the
imports stay null and libcurl's first lookup calls address 0. The link gives no
warning. (An earlier attempt in another app failed with `CURLE_FAILED_INIT` and
a resolver thread reporting `ENOMEM`; that was most likely the same null
import, but it wasn't rerun to confirm.)

`console_curl.c` defines them, so the linker takes the app's instead of the
stubs. They are built on `sceNetResolverCreate` and
`sceNetResolverStartNtoa`:

- IPv4 only, one address per name;
- a 16 KiB net pool and a resolver per lookup, so it is safe from any thread;
- 5 seconds per try, 2 retries;
- `getaddrinfo` returns `EAI_FAMILY` for anything but `AF_UNSPEC` or
  `AF_INET` (curl then uses IPv4), honours `AI_NUMERICHOST` and `AI_PASSIVE`,
  and sets `sin_len` (BSD socket addresses carry a length);
- `getnameinfo` is numeric only; `gethostbyname` returns `NULL` (curl uses
  `getaddrinfo`); `fnmatch` handles `*` and `?`.

With these in place curl's own threaded resolver works as it is. The same
request made three ways (address resolved by the app and passed with
`CURLOPT_RESOLVE`, the same with another certificate bundle, and plain) gave
200 each time.

Because the lookup runs inside curl's connect phase,
`CURLOPT_CONNECTTIMEOUT_MS` covers it. Allow for a slow lookup plus a connect.

### libc stand-ins

Each is the smallest stand-in that keeps the libraries working.

| Symbol | Asked for by | What the app provides | Why that is safe |
| --- | --- | --- | --- |
| `getpwuid_r` | libcurl (home folder, `.netrc`) | `*result = NULL`, `ENOENT` | No user database; curl treats it as "no home" |
| `_setjmp`, `_longjmp` | the archives | An assembly `jmp` to `setjmp`/`longjmp` | A plain jump keeps the caller's frame; a C wrapper would break `setjmp` |
| `openlog`, `closelog` | the archives | Empty | Nothing logs to syslog |
| `dladdr` | OpenSSL | Returns 0 (not found) | Informational only |
| `if_nametoindex` | libcurl | Returns 0 | Only for IPv6 scope IDs |
| `pipe2` | libcurl | `pipe` plus `fcntl` for the flags | Same behaviour |
| `recvmmsg`, `sendmmsg` | libcurl (HTTP/3) | `ENOSYS` | curl falls back |
| `ZSTD_trace_*` | zstd | Begin returns 0 ("not traced") | Optional tracing hooks |
| `gmtime_r` | OpenSSL | A full implementation | **Must be correct:** certificate dates are checked with it |
| `popen`, `pclose`, `isatty`, `mkstemp` | the archives | `ENOSYS` / "not a terminal" | Never needed by an HTTP client |

The last row is defensive. With this repository's pinned SDK those four
resolve to modules a title does load. ProsperoLichess, on its SDK, found
`isatty` and `mkstemp` resolving to `libScePosixForWebKit` (a crash at address
0), so the definitions are kept.

The trampolines, verbatim:

```c
__asm__(".text\n"
        ".globl _setjmp\n"
        "_setjmp:\n"
        "    jmp *setjmp@GOTPCREL(%rip)\n"
        ".globl _longjmp\n"
        "_longjmp:\n"
        "    jmp *longjmp@GOTPCREL(%rip)\n");
```

### The `fcntl` wrapper

Right after creating a socket, curl calls `fcntl(sock, F_SETFD, FD_CLOEXEC)`
and gives up with `CURLE_COULDNT_CONNECT` when that fails. In a sandboxed
title the console's libc refuses it on sockets with `EINVAL`, so every request
fails at once:

```text
Could not connect to server (curl 7: fcntl set CLOEXEC: Invalid argument)
```

The elevated ProsperoRadio never showed this; why the elevated state differs
wasn't checked.

`APP_WRAP_SYMBOLS += fcntl` links with `--wrap=fcntl`, so every `fcntl` call in
the app and in the archives goes to `__wrap_fcntl` in `console_curl.c`. It
calls the real `fcntl` first and answers only when that fails with `EINVAL`:
close-on-exec is accepted (a title never execs another program), and
`O_NONBLOCK` is turned into the console's own socket option. Files and pipes
never reach that path.

### Non-blocking sockets

curl makes its sockets non-blocking with `fcntl(O_NONBLOCK)`. On the console
that doesn't take effect, and curl carries on with a blocking socket. Two
things follow:

- **A finished request can hang.** In this repository's update check, every
  answer arrived in about 160 ms, and then `curl_easy_perform` or
  `curl_easy_cleanup` did not return for 400 seconds, until the server closed
  the idle connection. This happened with the `fcntl` wrapper in place.
- **Downloads are slow** (measured in [section 5](#5-large-downloads-and-streams)).

The fix is the console's own option, `SO_NBIO` (`0x1200`, not in the SDK
headers), set from `CURLOPT_SOCKOPTFUNCTION`, which curl calls for every socket
before connecting. `console_curl_setup()` installs that callback; an app with
a socket callback of its own calls `console_curl_nonblocking(socket)` from it.

```c
int console_curl_nonblocking(int socket)
{
    const int on = 1;
    return setsockopt(socket, SOL_SOCKET, 0x1200 /* SO_NBIO */, &on, sizeof(on));
}
```

Why the wrapper alone left the update check's sockets blocking wasn't
established (the real `fcntl` may accept `F_SETFL` on a socket without acting
on it). The update-check example logs a `sockets ...` probe line at start that
reports what each call returns. Use both the wrapper and the callback.

**Sockets from `sceNetSocket` are not needed.** ProsperoLichess also ran curl
on sockets it opened with `sceNetSocket` (through
`CURLOPT_OPENSOCKETFUNCTION`/`CLOSESOCKETFUNCTION`, and `--wrap` on `connect
send recv setsockopt getsockopt getsockname getpeername shutdown poll fcntl`
to route those sockets to the `sceNet*` functions). It worked, and was slower:
881 ms against 299 ms on libc sockets for the same 610-byte GET, as the first
request of a run.

### Certificates

The console keeps its certificate authorities in `CA_LIST.cer`: plain PEM with
comment lines between entries (90 certificates on our console, ISRG Root X1
among them). OpenSSL reads it as it is, so it goes straight into
`CURLOPT_CAINFO`.

| App state | Path |
| --- | --- |
| Elevated | `/system/common/cert/CA_LIST.cer` |
| Sandboxed | `/<sceKernelGetFsSandboxRandomWord()>/common/cert/CA_LIST.cer` |

`console_curl_ca_file()` returns the one that exists and checks again on every
call, so it follows an app that elevates after starting.

If the file can't be opened, curl fails with `CURLE_SSL_CACERT_BADFILE` (77). A
certificate that doesn't verify is `CURLE_PEER_FAILED_VERIFICATION` (60):
ProsperoLichess confirmed that expired, wrong-host and self-signed test sites
are refused that way. Let's Encrypt's newer chain (cross-signed by ISRG Root
X1) and Google Trust Services (homebrew.page) both verify from the console's
list without extra configuration.

Never switch verification off to make a request work.

## 4. After a PacBrew update: finding missing symbols

The linker reports missing symbols one at a time, which is slow. This lists
them all at once, for libcurl and what it links or for any PacBrew archives you
name:

```bash
make update-check-example          # or your own build: fills build/obj
tools/find-missing-symbols.sh      # libcurl libssl libcrypto libz libzstd libpsl
tools/find-missing-symbols.sh libsqlite3.a
```

It computes:

1. symbols the archives leave undefined,
2. minus symbols the archives define themselves,
3. minus symbols the SDK's import stubs export,
4. minus symbols the app's objects define.

and prints two lists:

- **provided by nothing**: the link fails until the app defines them;
- **provided only by `libScePosixForWebKit`**: the quiet trap. The link
  succeeds and the call jumps to address 0 at run time.

With the app's objects built, both lists are empty for libcurl. Run against an
empty object folder (`APP_OBJECTS=/nonexistent`), it prints exactly the
functions `console_curl.c` defines:

```text
== provided by nothing (the link fails; write a stand-in):
ZSTD_trace_compress_begin  ZSTD_trace_compress_end  ZSTD_trace_decompress_begin
ZSTD_trace_decompress_end  _longjmp  _setjmp  closelog  dladdr  getpwuid_r
gmtime_r  if_nametoindex  openlog  pipe2  recvmmsg  sendmmsg

== provided only by libScePosixForWebKit (links, then jumps to 0; define it in the app):
fnmatch  freeaddrinfo  gai_strerror  getaddrinfo  gethostbyname  getnameinfo
```

Run it when the pinned PacBrew version changes, and when you add a library.

## 5. Large downloads and streams

`curl_easy_perform` is fine for a small file. For a release archive, a large
catalogue or a live stream, an app needs progress, backpressure, a way to
cancel from another thread, and a connection kept open between requests. That
is the multi interface.
[`examples/curl/multi_reader.cpp`](../examples/curl/multi_reader.cpp) is a
complete version; the points that matter on the console are below.

### The shape

- **Connection** owns one `CURLM`. Requests made on it share its connection
  cache, so the socket and the TLS session are reused.
- **Request** is one easy handle on that multi.
- `send()` returns once the headers of the final response are in, `read()`
  blocks until it has at least one byte and returns 0 at the end, and `abort()`
  may be called from another thread.
- There is no curl worker thread: whichever thread calls `send()` or `read()`
  drives the transfer.

"Final response" is decided in the header callback: on the blank line that ends
a header block it reads `CURLINFO_RESPONSE_CODE` and ignores 1xx and any 3xx
(except 304) that curl will follow. The first body byte also counts as "headers
done".

### Wait only when idle

```cpp
void drive(int idle_wait_ms)
{
    const std::size_t before = body_.size();
    const bool had_headers = headers_done_;
    int running = 0;
    curl_multi_perform(multi, &running);
    // ... curl_multi_info_read: CURLMSG_DONE -> done, result, status
    const bool progressed = body_.size() != before || headers_done_ != had_headers || done_;
    if (!progressed && idle_wait_ms > 0)
        curl_multi_poll(multi, nullptr, 0, idle_wait_ms, &ready);
}
```

On the console `curl_multi_poll` doesn't always return early when the socket
becomes readable. Waiting after every pass held a download to one buffer per
wait. So: wait only when a pass made no progress, and keep the wait short
(10 ms).

What that and the socket option are worth, measured on one 11.8 MB file, one
change at a time:

| Build | Sockets | Waits in the read loop | Time |
| --- | --- | --- | --- |
| First | Blocking | `curl_multi_poll` 50 ms after every pass | about 2 min |
| Second | Blocking | 10 ms, only after a pass with no progress | about 43 s |
| Third | `SO_NBIO` | The same | about 10 s |
| A PC, same code | | | about 1 s |

### Backpressure

The body callback stores what arrives. When 1 MiB is waiting unread it returns
`CURL_WRITEFUNC_PAUSE` (curl keeps that chunk and offers it again). `read()`
resumes with `curl_easy_pause(CURLPAUSE_CONT)` once the unread part is below
512 KiB. curl may deliver the held chunk from inside that call, which is fine:
it is the same thread. The buffer is compacted when fully read, or when the
read position passes 1 MiB.

### Cancelling from another thread

```cpp
void abort()
{
    aborted_.store(true);          // std::atomic<bool>
    curl_multi_wakeup(multi);      // ends a curl_multi_poll early
}
```

`curl_multi_wakeup` is the one multi call documented as safe from another
thread. `send()` and `read()` check the flag on every loop. Make sure the
request can't be destroyed while another thread is aborting it.

### Options for large transfers

| Option | Value | Why |
| --- | --- | --- |
| `console_curl_setup()` | | `CURLOPT_NOSIGNAL`, `CURLOPT_CAINFO`, `SO_NBIO` |
| `CURLOPT_HTTP_VERSION` | 1.1 | What was tested |
| `CURLOPT_FOLLOWLOCATION`, `CURLOPT_MAXREDIRS` | 1, 8 | Release files redirect |
| `CURLOPT_PROTOCOLS_STR`, `CURLOPT_REDIR_PROTOCOLS_STR` | `https` | Nothing else. Check the host of a redirect yourself if it matters |
| `CURLOPT_BUFFERSIZE` | 256 KiB | Fewer passes per large file |
| `CURLOPT_CONNECTTIMEOUT_MS` | Twice your connect limit | It covers the name lookup too |
| `CURLOPT_ERRORBUFFER` | Per request | Log it with the address on failure |

### Error codes

The update check and the example report a failed transfer as
**`-(10000 + CURLcode)`**:

| Value | CURLcode | Meaning |
| --- | --- | --- |
| `-10006` | `CURLE_COULDNT_RESOLVE_HOST` | Name lookup failed |
| `-10007` | `CURLE_COULDNT_CONNECT` | Connect failed |
| `-10028` | `CURLE_OPERATION_TIMEDOUT` | The app's deadline or curl's |
| `-10042` | `CURLE_ABORTED_BY_CALLBACK` | `abort()` was called |
| `-10060` | `CURLE_PEER_FAILED_VERIFICATION` | Certificate not trusted |
| `-10077` | `CURLE_SSL_CACERT_BADFILE` | `CA_LIST.cer` not found or unreadable |

### One line per large transfer

Log one line when a request that received a megabyte or more ends: bytes,
milliseconds, perform passes, waits, and how many waits the socket woke. That
line showed both problems in the table above. It also showed, when a download
looked stuck, that the transfer had finished and the time was going elsewhere
(see the last rows of [Troubleshooting](#9-troubleshooting)).

## 6. Elevated apps

An app that uses the [elevation protocol](SANDBOX_ELEVATION.md) changes three
things for networking:

1. **`sceHttp` and `sceSsl` stop working** for public sites (`0x8095f00c`,
   [section 1](#1-when-to-use-libcurl)). Use libcurl for every request made
   after elevating.
2. **The certificate list moves** to `/system/common/cert/CA_LIST.cer`.
   `console_curl_ca_file()` handles it.
3. **System modules asked for later aren't found.** They are looked up by a
   path inside the sandbox, and after elevation the app's root is the
   console's. Load the modules the app needs before elevating and keep them,
   by wrapping the four load and unload calls:

   ```make
   APP_WRAP_SYMBOLS += sceSysmoduleLoadModule sceSysmoduleUnloadModule
   APP_WRAP_SYMBOLS += sceSysmoduleLoadModuleInternal sceSysmoduleUnloadModuleInternal
   ```

   ```c
   static struct { uint32_t id; int internal; int kept; } g_modules[] = {
       {0x0096, 0, 0},     /* keyboard dialog */
       {0x0088, 0, 0},     /* MP3 and AAC decoders */
       {0x80000069, 1, 0}, /* Opus decoder */
   };

   /* First thing in main, before the elevation request. */
   int app_preload_modules(void)
   {
       int loaded = 0;
       for (unsigned i = 0; i < sizeof(g_modules) / sizeof(g_modules[0]); ++i)
       {
           const int result = g_modules[i].internal
                                  ? __real_sceSysmoduleLoadModuleInternal(g_modules[i].id)
                                  : __real_sceSysmoduleLoadModule((uint16_t)g_modules[i].id);
           g_modules[i].kept = result >= 0;
           loaded += g_modules[i].kept;
       }
       return loaded;
   }

   /* A kept module is neither loaded again nor unloaded. */
   int __wrap_sceSysmoduleLoadModule(uint16_t id)
   {
       return module_kept(id, 0) ? 0 : __real_sceSysmoduleLoadModule(id);
   }
   /* ... the same for Unload, LoadModuleInternal and UnloadModuleInternal */
   ```

   The module list is ProsperoRadio's; use the IDs your app loads.

The libcurl setup itself ran elevated in ProsperoRadio. This repository's
update check has only been run sandboxed.

## 7. Testing and diagnosing

### On a PC

Code written against libcurl runs on Linux unchanged, which checks the logic
(headers, pause and resume, end of body, errors) without a console. Provide
the console functions it calls, and point `CURLOPT_CAINFO` at the PC's bundle:

```cpp
extern "C" const char *sceKernelGetFsSandboxRandomWord(void) { return "none"; }
extern "C" int sceKernelDebugOutText(int, const char *) { return 0; }
```

Pointing it at a copy of the console's `CA_LIST.cer` instead tests the
certificate list itself: that is how a site's chain can be checked before a
console run. A PC run doesn't cover the console-specific parts (sockets,
lookup, poll); `console_curl.c` doesn't build with a desktop compiler, and
isn't needed there.

### On the console

- **Trace first.** `CURLOPT_VERBOSE` with a `CURLOPT_DEBUGFUNCTION` that
  writes curl's `CURLINFO_TEXT` lines to the kernel log shows every step from
  the lookup to `SSL certificate verified via OpenSSL`. The update check has
  this built in as `UPDATE_CHECK_CURL_TRACE`, with the time of each line. It is
  what showed the 400-second wait coming *after* a complete answer. Turn it
  off once things work: it costs a log line per step.
- **Crashes show only in the kernel log** (`A user thread receives a fatal
  signal`, with the thread name and the fault address). Name your threads; a
  fault address of 0 right before a lookup is the null `getaddrinfo`.
- **Log before and after each request**, and after each batch of work between
  requests, so a stall can be placed.
- **Let the title end itself** in a scripted run
  (`sceSystemServiceLoadExec("exit")` after a delay), and give every network
  call a timeout, so a failed run doesn't leave a title that must be closed by
  hand.

## 8. What we learned on hardware

1. **Elevated apps can't use `sceSsl` for public sites.** `0x8095f00c` on
   every certificate, even after `sceHttpsLoadCert`. The same build without
   elevation worked. *(ProsperoRadio)*
2. **PacBrew's libcurl links into a native app** once the missing libc
   functions exist: libcurl 8.18.0, OpenSSL 3.5.2, zlib, zstd, libpsl.
3. **`getaddrinfo` is a null import.** It resolves to `libScePosixForWebKit`;
   the first call is a crash at address 0. The app's own functions on
   `sceNetResolver` fix it, and curl's threaded resolver then works.
   *(ProsperoRadio)*
4. **Sockets stay blocking unless `SO_NBIO` is set.** Slow downloads (43 s
   against 10 s for 11.8 MB) *(ProsperoRadio)*, and a finished request that
   doesn't return for 400 seconds *(update check)*.
5. **`curl_multi_poll` doesn't always wake early.** Wait only after a pass
   with no progress, for 10 ms. *(ProsperoRadio)*
6. **The console's `CA_LIST.cer` works as `CURLOPT_CAINFO`**, at
   `/system/common/cert/` elevated and `/<sandbox word>/common/cert/` in the
   sandbox. Bad certificates are refused with curl 60. *(all three)*
7. **In the sandbox, `fcntl` on a socket is refused.** curl fails with
   `fcntl set CLOEXEC: Invalid argument` until `fcntl` is wrapped.
   *(ProsperoLichess)*
8. **libc sockets beat `sceNetSocket` sockets** for curl: 299 ms against 881 ms.
   *(ProsperoLichess)*
9. **Results.** Five API requests to homebrew.page in 147 to 193 ms each, each
   with its own lookup and TLS handshake *(update check)*. A 70 MB catalogue in
   six files in about 64 s, the same as with `sceHttp`; audio streams playing
   within 2 s *(ProsperoRadio)*. A chunked live stream kept open
   *(ProsperoLichess)*.
10. **Not the network: small writes on `/data` are slow**, about 5 ms per
    database page with SQLite. A download that "stopped after a few files" was
    the import between requests. Build such a database in memory and write it
    once. *(ProsperoRadio)*

## 9. Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `sceHttp`/`sceSsl` fail with `0x8095f00c` on every HTTPS site, in an elevated app | The system TLS stack rejects public certificates once elevated | Use libcurl ([section 1](#1-when-to-use-libcurl)) |
| The link fails with one undefined libc symbol after another | PacBrew archives target the payload SDK's libc | `console_curl.c`; for other libraries, `tools/find-missing-symbols.sh` ([section 4](#4-after-a-pacbrew-update-finding-missing-symbols)) |
| The converter says `does not yet publish application exports` | A function the app defines in place of a system stub was exported | Keep `tooling/native/app-symbols.map` as shipped (`local: *`) |
| Crash at address 0 on the first request | `getaddrinfo` imported from `libScePosixForWebKit` | `console_curl.c` ([Name lookup](#name-lookup)) |
| `CURLE_FAILED_INIT`, resolver thread `ENOMEM` | Most likely the same null import | The same |
| Crash at address 0 in `isatty` or `mkstemp` | The same module, on some SDK versions | `console_curl.c` ([libc stand-ins](#libc-stand-ins)) |
| `-10007` in a sandboxed app; the error buffer says `fcntl set CLOEXEC: Invalid argument` | libc refuses `fcntl` on sockets in the sandbox | `APP_WRAP_SYMBOLS += fcntl` ([The `fcntl` wrapper](#the-fcntl-wrapper)) |
| The answer arrives, but the call returns minutes later (400 s with Cloudflare) | The socket is blocking; curl waits for the server to close the connection | `console_curl_setup()` on the handle ([Non-blocking sockets](#non-blocking-sockets)) |
| Downloads several times slower than on a PC | The same blocking socket | The same |
| Downloads still slow, many waits in the transfer line | Waiting in `curl_multi_poll` after passes that made progress | Wait only when idle, 10 ms ([section 5](#wait-only-when-idle)) |
| `-10006` | The lookup failed: no network, bad name, DNS down | Check the network and the name; it tries for 5 s, three times |
| `-10077` | `CA_LIST.cer` isn't at the chosen path (elevated against sandbox) | `console_curl_ca_file()` |
| `-10060` | The chain isn't in the console's list, or the clock or `gmtime_r` is wrong | Check the chain against a copy of `CA_LIST.cer` on a PC; check the console's clock |
| `-10028` | No headers within the connect and receive limits, or no body bytes for the receive limit | Raise the limits; check the server |
| A system module is missing after elevation | Modules are looked up inside the sandbox path | Load and keep them first ([section 6](#6-elevated-apps)) |
| A large download seems to stop after a few files, with no error | Usually not the network: something between requests | Log around each request and each batch of work; the transfer line shows whether the last request finished |
| Storing downloaded data takes minutes | Small writes on `/data`, about 5 ms per database page | Build in memory, write once |

## 10. Licences

libcurl and its dependencies are linked statically, so an app that ships with
them must carry each one's copyright line and licence text, for example in its
`THIRD_PARTY_NOTICES.md`:

| Component | Version in PacBrew v0.40.2 (from its headers) | Licence |
| --- | --- | --- |
| libcurl | 8.18.0 | curl licence (MIT/X derivative) |
| OpenSSL | 3.5.2 | Apache License 2.0 |
| zlib | 1.3.2 | zlib licence |
| zstd | 1.5.6 | BSD-3-Clause (dual-licensed with GPL-2.0; use the BSD terms) |
| libpsl | 0.21.5 | MIT; its built-in Public Suffix List data is MPL-2.0 |

OpenSSL 3 under Apache-2.0 is compatible with this template's
GPL-3.0-or-later. The PacBrew prefix doesn't include these texts (its
`share/licenses` holds another package's only), so take them from each
project's release of the version above, and check each licence against that
release before publishing.
