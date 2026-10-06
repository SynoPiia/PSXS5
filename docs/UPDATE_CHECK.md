# Update check

An app listed on [homebrew.page](https://homebrew.page) can tell its user that
a newer release exists. This optional example is that check: four files you
copy into your project, and a small title that exercises them on a console.

It asks the catalog one question, over HTTPS, about the running app only. It
downloads nothing else and installs nothing. The request goes through
**libcurl** with OpenSSL and the console's certificate list, which works the
same in the normal app sandbox and in an elevated app.

## Use it in your app

Copy the four files from `examples/update-check/` into your sources:

| File | What it is |
| --- | --- |
| `update_check.h`, `update_check.c` | The check: address, request, JSON reader, decision |
| `console_curl.h`, `console_curl.c` | What PacBrew's libcurl needs to run in a native title (see [The transport](#the-transport-libcurl-on-the-console)) |

In an app built from this template every C and C++ file under `src/` is
compiled automatically, so that is one command, plus two lines in the
`Makefile` (or in your `.env`):

```bash
cp examples/update-check/{update_check,console_curl}.{h,c} src/
```

```make
PACBREW_PACKAGES += libcurl
APP_WRAP_SYMBOLS += fcntl
```

The first line links PacBrew's libcurl 8.18.0 with OpenSSL 3.5.2, zlib, zstd
and libpsl (downloaded and verified by the build). The second routes every
`fcntl` call to `console_curl.c`. The copies build with `make` and pass
`make lint` unchanged. They compile as C11 or as C++.

Shipping an app with libcurl linked in means shipping those libraries'
notices: see [Licences](#licences).

```cpp
#include "update_check.h"

// On a worker thread, once per launch:
update_check_result result;
update_check_run_self(&result);
if (result.state == UPDATE_CHECK_AVAILABLE)
    show_notice("Update available: %s", result.version);
```

`update_check_run_self` reads the app's own title ID and `contentVersion` from
`/app0/sce_sys/param.json`, so there is nothing to configure and nothing to
keep in sync with a release. `update_check_run(title_id, installed, &result)`
does the same for values you pass.

| `result.state` | Meaning | What to do |
| --- | --- | --- |
| `UPDATE_CHECK_AVAILABLE` | The catalog lists a higher content version | Tell the user. `result.version` is the release's name, `result.page` its page on homebrew.page. |
| `UPDATE_CHECK_UP_TO_DATE` | The catalog has nothing newer | Nothing. |
| `UPDATE_CHECK_UNKNOWN` | No answer, or one that can't be used | Nothing. `result.reason` says why, for your log. |

The check is for apps that are **listed in the catalog**. Before your app is
listed, its title ID isn't known there and every check answers "not listed",
which is `UPDATE_CHECK_UNKNOWN`. To get listed, see the catalog's
[submission guide](https://github.com/blackbearreloaded/ps5-homebrew-catalog/blob/main/docs/submitting.md).

Rules that keep it harmless:

- **Never on the main thread.** The call blocks for up to five seconds per
  network phase. Run it on a worker and let the app start without it.
- **Once per launch** is enough; the catalog changes a few times a day.
- **Unknown means silence.** No network, an app that isn't listed, a catalog
  that doesn't know the app's content version: all give
  `UPDATE_CHECK_UNKNOWN`, and none is an error worth showing.
- **Only notify.** The check never downloads or installs. Point the user to
  ProsperoStore or to the app's page. To let the app update itself instead,
  use the [self-update](SELF_UPDATE.md) example, which builds on this check.
- **One check at a time.** The function isn't reentrant.

## What makes an update visible

The catalog compares **content versions**: the `contentVersion` in your
`sce_sys/param.json`, in the PlayStation format `NN.NNN.NNN` (`01.000.070`). An
update exists when the catalog's value for your newest release is higher than
the one in the running app. So:

- raise `contentVersion` in every release, and build from that commit;
- keep `sce_sys/param.json` in your repository, so the catalog can read it at
  your release tag.

The catalog's
[App versions](https://github.com/blackbearreloaded/ps5-homebrew-catalog/blob/main/docs/versioning.md)
page has the full rules, and its
[Store API](https://github.com/blackbearreloaded/ps5-homebrew-catalog/blob/main/docs/api.md)
page specifies the file this check reads
(`https://homebrew.page/api/v1/apps/<TITLEID>.json`).

## How it works

1. Build the address from the title ID. Anything that isn't four capital
   letters and five digits is refused before a request is made.
2. `GET` it with libcurl over HTTPS only, verifying the server's certificate
   and name against the console's own certificate list, with no redirects,
   HTTP/1.1, a 5-second connect limit (name lookup included) and 15 seconds in
   all, and a `User-Agent` naming the app:
   `homebrew-update-check/1 (<TITLEID>)`.
3. Refuse an answer larger than 64 KiB, a status other than 200, and anything
   that isn't one complete JSON object.
4. Read `status` and `content_version`. A reservation (`coming_soon`) and a
   `null` version are "unknown".
5. Compare the two content versions as three numbers.

Everything read from the network is treated as hostile: lengths are checked,
values are copied only into buffers that hold them, nesting is bounded, and an
answer cut short in transit is refused as a whole. The curl handle is created
for the request and cleaned up after it; libcurl's global state is set up once,
on the first check.

`update_check_run_with` takes a transport of your own, for an app that already
has an HTTP client, and is what the tests use.

## The transport: libcurl on the console

The full guide to libcurl on the console (large downloads, elevated apps,
adding other PacBrew libraries, troubleshooting) is [CURL.md](CURL.md). In
short:

PacBrew's archives are built for the payload SDK's libc, not for a native
title. They link, but four things stop them working on the console. Each is
answered in `console_curl.c`, and each was found on hardware by ProsperoRadio
(an elevated app) and ProsperoLichess (a sandboxed one):

| Problem | Symptom without the fix | What `console_curl.c` does |
| --- | --- | --- |
| `getaddrinfo` and friends come from `libScePosixForWebKit`, a module a native title doesn't load | Crash at address 0 on the first name lookup | Defines `getaddrinfo`, `freeaddrinfo`, `gai_strerror`, `getnameinfo`, `gethostbyname` and `fnmatch` on `sceNetResolver` (IPv4) |
| In the sandbox, the console's libc refuses `fcntl` on sockets with `EINVAL` | Every request fails: `curl 7: fcntl set CLOEXEC: Invalid argument`; sockets stay blocking | `__wrap_fcntl` (linked with `APP_WRAP_SYMBOLS += fcntl`): a refused close-on-exec request succeeds (a title never execs), and non-blocking mode goes through the console's own `SO_NBIO` socket option |
| libc functions the archives ask for are missing, or link from `libScePosixForWebKit` (`isatty`, `mkstemp`) | Link errors, or a crash at address 0 | Small stand-ins; `gmtime_r` is a full implementation, because OpenSSL checks certificate dates with it |
| No certificate store OpenSSL knows | `CURLE_SSL_CACERT_BADFILE` (77) | `console_curl_ca_file()`: the console's `CA_LIST.cer`, at `/system/common/cert/` when elevated or `/<sandbox word>/common/cert/` in the sandbox, passed as `CURLOPT_CAINFO` |
| curl's sockets stay blocking | The answer arrives, then the call doesn't return until the server closes the idle connection (400 s with homebrew.page); large downloads are slow | `console_curl_setup()` sets the console's `SO_NBIO` option on every socket through `CURLOPT_SOCKOPTFUNCTION` |

**Call `console_curl_setup(easy)` on every curl handle**, right after
`curl_easy_init()`. It sets `CURLOPT_NOSIGNAL`, `CURLOPT_CAINFO` and the socket
callback, and with the `fcntl` wrap that is all any other libcurl use in your
app needs. An app with a socket callback of its own calls
`console_curl_nonblocking(socket)` from it.

The last row was found here, on the console: the `fcntl` wrap alone left the
sockets blocking in this title, and each check took 400 seconds to return
after an answer that had arrived in 160 ms. Why the wrap wasn't enough wasn't
established; the example title now logs a socket probe (`sockets ...`) at
start for the next run.

The build keeps every symbol of the app internal (`tooling/native/app-symbols.map`).
Without that, the linker would publish the functions above as exports, because
they replace system stubs, and the converter refuses exports.

A failed request sets `result.platform_error` to `-(10000 + CURLcode)`:
`-10007` is "couldn't connect", `-10028` a timeout, `-10060` a certificate the
list doesn't vouch for. `curl_easy_strerror(-error - 10000)` gives the text; the
example title logs it.

### Without libcurl: `UPDATE_CHECK_USE_SCEHTTP`

Built with `UPDATE_CHECK_USE_SCEHTTP` defined, the check uses the console's own
`sceHttp` and `sceSsl` instead, needs no PacBrew package, no `fcntl` wrap and no
`console_curl.c`, and adds nothing to the app's size. That transport has also
run on hardware (see the first run below), with one limit: once an app is
elevated, `sceSsl` rejects every public site with `0x8095f00c`. Use it only in an
app that never elevates. `UPDATE_CHECK_NO_NETWORK` leaves out the transport
altogether (the host tests use it).

### Licences

libcurl and its dependencies are linked statically. An app that ships with them
must carry each one's copyright line and licence text, for example in its
`THIRD_PARTY_NOTICES.md`:

| Component | Version in PacBrew v0.40.2 | Licence |
| --- | --- | --- |
| libcurl | 8.18.0 | curl licence (MIT/X derivative) |
| OpenSSL | 3.5.2 | Apache License 2.0 |
| zlib | 1.3.2 | zlib licence |
| zstd | 1.5.6 | BSD-3-Clause (dual-licensed with GPL-2.0) |
| libpsl | 0.21.5 | MIT; its built-in Public Suffix List data is MPL-2.0 |

The PacBrew prefix doesn't include these texts: take them from each project's
release of the version above.

## The example title

`make update-check-example` builds `dist/PPSA99780/`, a title with no
interface that checks itself and the titles in
[`examples/update-check/assets/targets.txt`](../examples/update-check/assets/targets.txt),
then reports each answer three ways:

- a line in the kernel log, starting `UPDATE-CHECK:`;
- the same line in `/download0/update-check.txt`;
- one notification with the number of requests answered.

```text
UPDATE-CHECK: PPSA99002 installed=01.000.000 state=update-available reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://homebrew.page/app/PPSA99002/ ms=412
```

The example's own title ID isn't in the catalog, so its self check answers
`not-listed`: that is the expected result, and it shows the 404 path working.
Edit `targets.txt` to check other titles.

Two build definitions are for scripted console runs:

| Definition | Effect |
| --- | --- |
| `UPDATE_CHECK_RUN_TAG=<word>` | Printed in the first line, to tell runs apart |
| `UPDATE_CHECK_EXIT_AFTER=<seconds>` | The title ends itself that long after reporting, through `sceSystemServiceLoadExec("exit")`. Without it the title stays up until it is closed from the home screen. |
| `UPDATE_CHECK_CURL_TRACE` | libcurl's own account of each step (lookup, connect, TLS, certificate) goes to the kernel log as `UPDATE-CHECK-CURL:` lines with the time since the process started. Headers and bodies are left out. |

```bash
APP_DEFINITIONS="UPDATE_CHECK_RUN_TAG=run1 UPDATE_CHECK_EXIT_AFTER=30" make update-check-example
```

## Tests

```bash
make test-update-check
```

Builds the two files into a host test with the address and undefined-behaviour
sanitizers and checks version parsing and ordering, the address builder, the
JSON reader (escapes, nesting, wrong types, oversized and malformed input), the
decision for every kind of answer, the mapping of HTTP statuses and transport
failures, and every truncation and thousands of single-byte mutations of a real
API answer. Host tests can't establish that the console's HTTPS reaches the
catalog; the example title does that.

## Console validation

### libcurl

Run on a PS5 on 2026-10-02 (the example title built with
`UPDATE_CHECK_RUN_TAG=curl3 UPDATE_CHECK_EXIT_AFTER=20 UPDATE_CHECK_CURL_TRACE`,
`eboot.bin` SHA-256
`789dc206b927dd6ca58ffd6e8517c3a6e906f8b718c622f2a4bae3bd785502c7`), installed
as a folder under `/data/homebrew` and registered by ShadowMountPlus, in the
title's own sandbox with no elevation. The console's firmware version wasn't
recorded.

```text
UPDATE-CHECK: start tag=curl3 host=homebrew.page transport=libcurl
UPDATE-CHECK: curl=8.18.0 ca=/FB7DAjsvgd/common/cert/CA_LIST.cer
UPDATE-CHECK: self installed=01.000.000 state=unknown reason=not-listed http=404 error=0x00000000 available=- version=- page=- ms=193
UPDATE-CHECK: PPSA99002 installed=01.000.000 state=update-available reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://homebrew.page/app/PPSA99002/ ms=159
UPDATE-CHECK: PPSA99002 installed=99.999.999 state=up-to-date reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://homebrew.page/app/PPSA99002/ ms=158
UPDATE-CHECK: PPSA99009 installed=01.000.000 state=unknown reason=not-available http=200 error=0x00000000 available=- version=- page=- ms=147
UPDATE-CHECK: PPSA00000 installed=01.000.000 state=unknown reason=not-listed http=404 error=0x00000000 available=- version=- page=- ms=180
UPDATE-CHECK: done requests=5 answered=5
```

| Checked | Result |
| --- | --- |
| The catalog is reachable with libcurl and the console's certificate list | Yes: five answers, 147 to 193 ms each, each with its own name lookup, TLS 1.3 handshake and handle |
| The certificate is verified | Yes: the trace shows `SSL certificate verified via OpenSSL` against `CA_LIST.cer` at the sandbox path, and the name matched |
| Each kind of answer is decided correctly | Update available, up to date, coming soon, and not listed, as in the first run |
| The title ends itself | Yes, through `sceSystemServiceLoadExec("exit")`: the kernel log shows `Kill for LoadExec ... => 0` |
| The console afterwards | Services answering, installed files unchanged two minutes later |

Two earlier runs the same day did not pass, and are why
`console_curl_setup()` exists. In the first, another title was started on the
console twelve seconds in, which closed this one before it had reported. In
the second, traced, every request was answered in about 160 ms and then
blocked for 400 seconds: the socket was still blocking (see
[The transport](#the-transport-libcurl-on-the-console)). That title had to be
closed from the home screen.

Not exercised on hardware: the libcurl transport in an elevated app (other
apps of ours use the same setup elevated), a failing network, an oversized
answer, and calling the check from a worker thread beside a running renderer.

### First run: the `sceHttp` transport

Run once on a PS5 on 2026-10-02, before the switch to libcurl (the example title built with
`UPDATE_CHECK_RUN_TAG=c2 UPDATE_CHECK_EXIT_AFTER=60`, `eboot.bin` SHA-256
`ab1dde8bec0fde046bcd26ec0a0dc02d2ecccbde6eaa8fe00c1017594ae6a1fe`), installed as a folder under `/data/homebrew` and registered by
ShadowMountPlus. The console's firmware version wasn't recorded in this run.

All five requests were answered over HTTPS with certificate verification on,
through the title's own sandbox with no elevation:

```text
UPDATE-CHECK: start tag=c2 host=homebrew.page
UPDATE-CHECK: self installed=01.000.000 state=unknown reason=not-listed http=404 error=0x00000000 available=- version=- page=- ms=154
UPDATE-CHECK: PPSA99002 installed=01.000.000 state=update-available reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://homebrew.page/app/PPSA99002/ ms=109
UPDATE-CHECK: PPSA99002 installed=99.999.999 state=up-to-date reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://homebrew.page/app/PPSA99002/ ms=94
UPDATE-CHECK: PPSA99009 installed=01.000.000 state=unknown reason=not-available http=200 error=0x00000000 available=- version=- page=- ms=71
UPDATE-CHECK: PPSA00000 installed=01.000.000 state=unknown reason=not-listed http=404 error=0x00000000 available=- version=- page=- ms=90
UPDATE-CHECK: done requests=5 answered=5
```

| Checked | Result |
| --- | --- |
| The catalog is reachable with the console's own HTTPS and certificate store | Yes: five answers, 71 to 154 ms each, including creating and destroying the `sceHttp` contexts |
| The title's own `param.json` is readable at `/app0/sce_sys/param.json` | Yes: the self check used its title ID and content version |
| Each kind of answer is decided correctly | Update available, up to date, coming soon, and not listed all matched what the catalog held |
| The report file in `/download0` | Written, and identical to the kernel log lines |
| The title ends itself | Yes, 60 seconds after reporting, through `sceSystemServiceLoadExec("exit")`: the kernel log shows `Kill for LoadExec ... => 0` and a normal unload, with no core dump and no forced kill |
| The console afterwards | Services answering, installed files unchanged two minutes later |

Not exercised on hardware: a failing network (the timeout and error paths are
covered by the host tests only), an oversized answer, and calling the check
from a worker thread beside a running renderer.
