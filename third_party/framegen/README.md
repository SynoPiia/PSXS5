# Frame generation

`ps5_framegen.cpp`, `ps5_framegen.h` and `framegen/fg_shaders.inc` come from
[PS5SX2](https://github.com/Swordpdf/PS5SX2) (`ps5/coreorbis/orbis-shims/`, GPL-3.0-or-later),
itself from Swordpdf's RPCS3-PS5 / ps5-framegen: AMD FSR 3's optical flow and frame
interpolation (FidelityFX SDK 2.3.0, MIT; see `framegen/LICENSE.txt`) on plain Vulkan.

PSXS5's one change: `output_view()` in `ps5_framegen.h`, so the generated frame is drawn
straight to the screen. The Vulkan commands are PSXS5's own pointers
(`src/platform/vk/fg_bridge.cpp`); `tools/build-framegen.sh` builds the archive.
