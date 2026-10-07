/*
 * PSXS5 - what PSXS5 does with the controller while a game runs: stick dead
 * zone and response, gas and brake on the triggers in racing games, the
 * DualSense as a light gun, and the adaptive triggers.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_CONTROLS_H
#define PSXS5_CONTROLS_H

#include "library.h"
#include "psxs5.h"

enum GameKind
{
    KIND_GUNCON = 1,
    KIND_JUSTIFIER = 2,
    KIND_RACING = 4,
};

/* KIND_* bits for a game, from assets/game-kinds.txt. */
int controls_kind(const Game *g);
/* The light gun a game gets with these settings: 0 none, 1 GunCon, 2 Justifier. */
int controls_gun_for(const Game *g, const Settings *s);
/* Before the game loads, and when it stops (the triggers go back to normal). */
void controls_start(const Game *g, const Settings *s);
void controls_stop(void);
/* Each frame, on the buttons the game will see. */
void controls_apply(PadState pads[PSXS5_MAX_PADS], const Settings *s, float dt);
/* The gun's crosshair, over the game. */
void controls_draw(void);
bool controls_gun_active(void);
void controls_recenter(void);

#endif
