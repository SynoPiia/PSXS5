/*
 * PSXS5 - the interface's colours, sizes and timings (1.1 redesign).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_THEME_H
#define PSXS5_THEME_H

/* colours, ARGB */
#define TH_BG 0xff0f1330u          /* screen background */
#define TH_BG_DEEP 0xff0a0d24u
#define TH_CARD 0xff151a3du        /* grouped settings, panels */
#define TH_CARD_SOFT 0xd8151a3du   /* panels over the game */
#define TH_ROW_SELECTED 0xff2a3370u
#define TH_FOCUS 0xff8fb0ffu       /* focus outline, accents */
#define TH_PILL 0xff1c2250u
#define TH_SWITCH_ON 0xff5b7cffu
#define TH_SWITCH_OFF 0xff3a4280u
#define TH_TEXT 0xffe8ebffu
#define TH_TEXT_DIM 0xff8f97c8u
#define TH_TEXT_SOFT 0xffcfd6ffu
#define TH_HINT 0xff9aa3d6u
#define TH_GOLD 0xfff0b429u
#define TH_DANGER 0xffff9c9cu
#define TH_GOOD 0xff5fd38au
#define TH_DIVIDER 0xff222a5cu

/* sizes, in 1920x1080 screen pixels */
#define TH_RADIUS 16.0f
#define TH_RADIUS_SMALL 12.0f
#define TH_MARGIN 64.0f
#define TH_HINT_Y 1012.0f

/* animation: how quickly highlights and panels catch up (per second) */
#define TH_SNAP 22.0f

#endif
