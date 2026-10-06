/*
 * PSXS5 - small stand-ins for symbols the PS5 libraries don't export.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if defined(__PROSPERO__)
#include <stddef.h>

/* libiconv (pulled in by SDL2) expects FreeBSD's MB_CUR_MAX function and
 * nl_langinfo(CODESET). PSXS5 runs in the C locale with UTF-8 text. */
size_t ___mb_cur_max(void)
{
    return 1;
}

char *nl_langinfo(int item)
{
    (void)item;
    return (char *)"UTF-8";
}

/* __builtin_cpu_init/__builtin_cpu_supports normally come from compiler-rt.
 * PCSX-ReARMed only uses them to warn about a missing SSE2/AVX. The PS5's
 * Zen 2 CPU has CMOV, MMX, POPCNT, SSE..SSE4.2, AVX and AVX2: feature bits
 * 0-10 in compiler-rt's numbering. */
struct ProcessorModel
{
    unsigned int vendor, type, subtype;
    unsigned int features[1];
};

struct ProcessorModel __cpu_model = {2 /* AMD */, 0, 0, {(1u << 11) - 1}};

int __cpu_indicator_init(void)
{
    return 0;
}
#endif
