#!/usr/bin/env bash
# ps5-native-app-boilerplate - Incremental host GoogleTest compilation.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$root/tools/ninja-build.sh"
gtest=$(bash "$root/tools/setup-test-dependencies.sh")
cxx=$(command -v "${HOST_CXX:-clang++}")
build="$root/build/tests"
ninja_begin "$build/build.ninja"
read -r -a flags <<< "${HOST_TEST_CXXFLAGS:--std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -ffunction-sections -fdata-sections}"
read -r -a ldflags <<< "${HOST_TEST_LDFLAGS:--Wl,--gc-sections}"
objects=()
for source in "$gtest/googletest/src/gtest-all.cc" "$gtest/googletest/src/gtest_main.cc" \
    "$root/tests/test_demo_renderer.cpp" "$root/src/demo_renderer.cpp"; do
    object="$build/${source##*/}.o"
    args=(-std=c++20 -O2)
    [[ $source == "$gtest/"* ]] || args=("${flags[@]}")
    ninja_inputs=("$source" "$cxx")
    ninja_edge CXX "$object" "${compiler_cache[@]}" "$cxx" "${args[@]}" -pthread \
        -I"$root/src" -isystem "$gtest/googletest/include" -I"$gtest/googletest" \
        -MD -MF "$object.d" -c "$source" -o "$object"
    objects+=("$object")
done
ninja_inputs=("${objects[@]}" "$cxx")
ninja_edge LINK "$build/demo_renderer_tests" "$cxx" "${flags[@]}" -pthread \
    "${objects[@]}" "${ldflags[@]}" -o "$build/demo_renderer_tests"
ninja_run
