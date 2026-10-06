/*
 * PSXS5 - interface sounds, synthesised at start-up (no sample files).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The click is a short band-passed noise burst (the plastic "tick") over a
 * falling low sine (the body of the case), like sliding a jewel case back
 * between two others on a shelf.
 */
#include "sfx.h"

#include "../platform/platform.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int16_t *frames; /* interleaved stereo */
    size_t count;
} Sound;

static Sound sounds[SFX_COUNT];
static bool enabled = true;

static uint32_t rng = 0x12345678u;
static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(rng >> 8) / 8388608.0f - 1.0f;
}

/* Two-pole resonant band-pass. */
typedef struct
{
    float a1, a2, b0, y1, y2;
} Resonator;

static Resonator resonator(float freq, float q, int rate)
{
    float w = 2.0f * 3.14159265f * freq / rate, r = expf(-w / (2.0f * q));
    Resonator f = {2.0f * r * cosf(w), -r * r, (1.0f - r * r) * 0.5f, 0, 0};
    return f;
}

static float resonate(Resonator *f, float x)
{
    float y = f->b0 * x + f->a1 * f->y1 + f->a2 * f->y2;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

static Sound make(int rate, float seconds, float tick_freq, float tick_gain, float body_from,
                  float body_to, float body_gain, float decay)
{
    Sound s;
    s.count = (size_t)(rate * seconds);
    s.frames = calloc(s.count * 2, sizeof(int16_t));
    if (!s.frames)
    {
        s.count = 0;
        return s;
    }
    Resonator tick = resonator(tick_freq, 1.6f, rate);
    float phase = 0.0f;
    for (size_t i = 0; i < s.count; ++i)
    {
        float t = (float)i / rate;
        float env_tick = expf(-t * decay);
        float env_body = expf(-t * decay * 0.55f);
        float freq = body_from * powf(body_to / body_from, fminf(t / seconds, 1.0f));
        phase += 2.0f * 3.14159265f * freq / rate;
        float v = resonate(&tick, noise()) * 6.0f * tick_gain * env_tick +
                  sinf(phase) * body_gain * env_body;
        /* 2 ms fade-in avoids a DC click */
        float attack = fminf(t / 0.002f, 1.0f);
        int sample = (int)(v * attack * 32767.0f);
        if (sample > 32767)
            sample = 32767;
        if (sample < -32768)
            sample = -32768;
        s.frames[i * 2] = s.frames[i * 2 + 1] = (int16_t)sample;
    }
    return s;
}

void sfx_init(int rate)
{
    for (int i = 0; i < SFX_COUNT; ++i)
        free(sounds[i].frames);
    sounds[SFX_CLICK] = make(rate, 0.045f, 2600.0f, 0.45f, 190.0f, 70.0f, 0.28f, 120.0f);
    sounds[SFX_SELECT] = make(rate, 0.16f, 1800.0f, 0.30f, 220.0f, 440.0f, 0.30f, 30.0f);
    sounds[SFX_BACK] = make(rate, 0.09f, 1400.0f, 0.30f, 300.0f, 140.0f, 0.25f, 60.0f);
}

void sfx_set_enabled(bool on)
{
    enabled = on;
}

void sfx_play(Sfx id)
{
    if (!enabled || id >= SFX_COUNT || !sounds[id].frames)
        return;
    /* Fast scrolling must not build a backlog of clicks. */
    if (plat_audio_queued_frames() > sounds[SFX_CLICK].count)
        plat_audio_clear();
    plat_audio_push(sounds[id].frames, sounds[id].count);
}
