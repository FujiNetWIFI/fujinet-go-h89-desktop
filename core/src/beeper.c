/*
 * beeper -- the H19's speaker: the bell (a 1 kHz tone for 200 ms, ^G and the
 * end of a transmitted page) and the key click (6 ms of it), as MAME's TLB
 * device times them, synthesised from the events the terminal reports
 * (h89host_take_sounds) into the session's audio.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>

#include "host.h"
#include "session_internal.h"

#define TONE_HZ       1000
#define BELL_SAMPLES  (H89SESSION_AUDIO_RATE / 5)       /* 200 ms */
#define CLICK_SAMPLES (H89SESSION_AUDIO_RATE * 6 / 1000) /* 6 ms */

static int s_remaining;     /* audio thread only */
static unsigned s_phase;

int h89session_render_audio(h89session *s, float *out, int nframes)
{
    unsigned bells = 0, clicks = 0;
    float amp;

    h89host_take_sounds(&bells, &clicks);
    if (bells) {
        if (h89session_get_int(s, "sound.bell", 1)) {
            if (s_remaining < BELL_SAMPLES)
                s_remaining = BELL_SAMPLES;
        }
        s->visual_bells += bells;
    }
    if (clicks && h89session_get_int(s, "sound.click", 1) && s_remaining < CLICK_SAMPLES)
        s_remaining = CLICK_SAMPLES;

    amp = (s->volume < 0 ? 0 : s->volume > 100 ? 100 : s->volume) / 100.0f * 0.25f;
    if (h89session_get_int(s, "sound.mute", 0))
        amp = 0;

    for (int i = 0; i < nframes; i++) {
        float v = 0;
        if (s_remaining > 0) {
            /* a square wave: the H19's speaker is driven from a logic output */
            v = ((s_phase * 2 * TONE_HZ / H89SESSION_AUDIO_RATE) & 1) ? amp : -amp;
            s_phase++;
            s_remaining--;
        }
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    return nframes;
}

void h89session_set_volume(h89session *s, int percent)
{
    s->volume = percent;
    h89session_set_int(s, "sound.volume", percent);
}

int h89session_take_visual_bells(h89session *s)
{
    int n = (int)s->visual_bells;
    s->visual_bells = 0;
    return n;
}
