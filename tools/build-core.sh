#!/usr/bin/env bash
# PSXS5 - builds PCSX-ReARMed as a static libretro archive.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/build-core.sh ps5       -> build/core-ps5/libpcsx_rearmed.a
#   tools/build-core.sh desktop   -> build/core-desktop/libpcsx_rearmed.a
#
# The core builds in-tree, so switching targets cleans it first.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
target=${1:-ps5}
core="$root/third_party/pcsx_rearmed"
out="$root/build/core-$target"
stamp="$core/.psxs5-target"
jobs=${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}

[[ -f $core/Makefile.libretro ]] || {
    echo "core source missing: run 'git submodule update --init --recursive'" >&2
    exit 2
}

# First bring-up uses the interpreter and no worker threads: the fewest moving
# parts on a new platform. DYNAREC=lightrec and USE_ASYNC_*=1 are later steps.
args=(
    -f Makefile.libretro
    platform=unix
    STATIC_LINKING=1
    TARGET=libpcsx_rearmed.a
    DYNAREC="${PSXS5_DYNAREC:-none}"
    HAVE_PHYSICAL_CDROM=0
    USE_LIBRETRO_VFS=0
    USE_ASYNC_CDROM=0
    USE_ASYNC_GPU=0
    USE_ASYNC_SPU=0
    NDRC_THREAD=0
    HAVE_CHD=1
    # "neon" is the enhanced GPU; on x86 it uses its SSE2 path. It provides 2x internal resolution.
    BUILTIN_GPU=neon
    HAVE_NEON_ASM=0
    DEBUG="${PSXS5_CORE_DEBUG:-0}"
)

case $target in
ps5)
    sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
    [[ -d $sdk ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
    [[ -d $sdk ]] || { echo "PS5 payload SDK not found at $sdk" >&2; exit 2; }
    export PS5_PAYLOAD_SDK=$sdk USE_CCACHE=0
    llvm_ar=$(command -v llvm-ar-18 || command -v llvm-ar)
    args+=(
        CC="sh $root/tooling/prospero-clang18"
        CXX="sh $root/tooling/prospero-clang18"
        AR="$llvm_ar"
        ARCH=x86_64
    )
    # Through the environment, not the command line: the core's Makefile
    # appends its own -D flags with +=, which a command-line CFLAGS would erase.
    export CFLAGS="-O2 -fPIC -ffunction-sections -fdata-sections -march=znver2"
    ;;
desktop)
    args+=(
        CC="${CC:-gcc}"
        AR="${AR:-ar}"
    )
    export CFLAGS="-O2 -fPIC -g"
    ;;
*)
    echo "usage: tools/build-core.sh [ps5|desktop]" >&2
    exit 2
    ;;
esac

# PSXS5's small additions to the core (e.g. padGetMode) live as patches so
# the submodule itself stays pristine; each is applied once.
for patch in "$root"/tools/patches/pcsx_rearmed-*.patch; do
    [[ -f $patch ]] || continue
    if git -C "$core" apply --reverse --check "$patch" 2>/dev/null; then
        continue # already applied
    fi
    echo "==> [core] applying $(basename "$patch")"
    git -C "$core" apply "$patch"
done

if [[ -f $stamp && $(cat "$stamp") != "$target" ]]; then
    echo "==> [core] switching target, cleaning"
    make -C "$core" -f Makefile.libretro platform=unix STATIC_LINKING=1 \
        TARGET=libpcsx_rearmed.a clean >/dev/null
fi
echo "$target" >"$stamp"

echo "==> [core] building PCSX-ReARMed for $target ($jobs jobs)"
make -C "$core" -j"$jobs" "${args[@]}"
mkdir -p "$out"
cp "$core/libpcsx_rearmed.a" "$out/libpcsx_rearmed.a"
echo "==> [core] $out/libpcsx_rearmed.a"
