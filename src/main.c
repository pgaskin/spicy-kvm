/**
 * Copyright © 2024 Patrick Gaskin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <pipewire/pipewire.h>
#include <purespice.h>
#include <pthread.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spice/enums.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio.h"
#include "ddcci.h"
#include "input.h"

struct ddc_opts {
    bool enable;
    const char *drm;
    uint8_t output_self;
    uint8_t output_other;
};

static bool is_connection_ready = false;

static void on_connection_ready(void) {
    fprintf(stdout, "info: connection ready\n");
    is_connection_ready = true;
}

static bool should_exit = false;

static void sighandler(int sig) {
    if (should_exit) exit(1);
    fprintf(stdout, "info: will exit\n");
    should_exit = true;
}

static double audio_current_offset_ms;
static double audio_total_latency_ms;
static double audio_device_latency_ms;

static void update_title(void) {
    // TODO: make not racy
    fprintf(stdout, "\033]0;spicy-kvm%s [audio - %.2f offset - %.2f latency - %.2f device]\007",
        input_is_grabbed() ? " [grab]" : "",
        audio_current_offset_ms,
        audio_total_latency_ms,
        audio_device_latency_ms);
    fflush(stdout);
}

static void on_audio_latency(double current_offset_ms, double total_latency_ms, double device_latency_ms) {
    static int ctr = 0;
    if (ctr++ >= 8) {
        ctr = 0;
    } else {
        return;
    }
    audio_current_offset_ms = current_offset_ms;
    audio_total_latency_ms = total_latency_ms;
    audio_device_latency_ms = device_latency_ms;
    update_title();
}

// TODO: unify logging

int main(int argc, char **argv) {
    const struct PSConfig config = {
        .host = "10.33.0.137",
        .port = 5999,
        .password = "",
        .ready = on_connection_ready,
        .inputs = {
            .enable = true,
            .autoConnect = true,
        },
        .playback = {
            .enable = true,
            .autoConnect = true,
            .start = audio_playback_start,
            .volume = audio_playback_volume,
            .mute = audio_playback_mute,
            .stop = audio_playback_stop,
            .data = audio_playback_data,
        },
        .record = {
            .enable = true,
            .autoConnect = true,
            .start = audio_record_start,
            .volume = audio_record_volume,
            .mute = audio_record_mute,
            .stop = audio_record_stop,
        },
    };
    const struct audio_opts audio = {
        .period_size = 256,
        .buffer_latency = 12,
        .sink = NULL,
        .source = NULL,
        .latency_cb = on_audio_latency,
    };
    const struct input_opts input = {
        .grab_key = {
            [KEY_RIGHTCTRL] = true,
            [KEY_PAUSE] = true,
        },
        // TODO: option for temporary grab key (hold down to redirect input without changing grab or display state)
    };
    const struct ddc_opts ddc = {
        .enable = true,
        .drm = "card1-HDMI-A-1",
        .output_self = 0x11,
        .output_other = 0x12,
    };
    bool linger = true;

    // TODO: cli opts for host, port, password, input enable, playback enable, playback sink, record enable, record source, ddc enable, ddc outputs, ddc card, input grab keys, linger

    int rc;
    struct ddcci ddcci;
    bool ddcci_ok = false;

    if (config.playback.enable || config.record.enable) {
        fprintf(stdout, "info: initializing audio\n");
        if (!audio_init(&audio)) {
            fprintf(stderr, "fatal: failed to initialize audio\n");
            return 1;
        }
    }

    if (config.inputs.enable) {
        fprintf(stdout, "info: initializing input\n");
        if (!input_init(&input)) {
            fprintf(stderr, "fatal: failed to initialize input\n");
            return 1;
        }
    }

    if (ddc.enable) {
        fprintf(stdout, "info: initializing ddc\n");
        int i2c;
        if ((rc = ddcci_find_i2c(ddc.drm, &i2c))) {
            fprintf(stderr, "warning: failed to initialize ddc: no i2c device found for '%s': %s\n", ddc.drm, ddcci_strerror(rc));
        } else if ((rc = ddcci_open(&ddcci, 6))) {
            fprintf(stderr, "warning: failed to initialize ddc: i2c %d: %s\n", i2c, ddcci_strerror(rc));
        } else {
            fprintf(stdout, "info: using i2c %d for '%s'\n", i2c, ddc.drm);
            ddcci_ok = true;
        }
    }

    fprintf(stdout, "info: connecting to spice server\n");
    if (!purespice_connect(&config)) {
        fprintf(stderr, "fatal: failed to connect to spice server\n");
        return 1;
    }

    fprintf(stdout, "info: waiting for connection to finish\n");
    while (!is_connection_ready) {
        if (purespice_process(1) != PS_STATUS_RUN) {
            fprintf(stderr, "fatal: failed to finish connecting to spice server\n");
            return 1;
        }
    }

    if (config.inputs.enable) {
        fprintf(stdout, "info: using relative mouse motion\n");
        if (!purespice_mouseMode(true)) {
            fprintf(stderr, "fatal: failed to set mouse mode\n");
            return 1;
        }
    }

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGQUIT, sighandler);

    bool was_grabbed = false;
    while (!should_exit) {
        if (purespice_process(1) != PS_STATUS_RUN) {
            fprintf(stderr, "fatal: failed to run spice server connection\n");
            return 1;
        }
        bool is_grabbed = input_is_grabbed();
        if (is_grabbed != was_grabbed) {
            if (ddc.enable) {
                if (!ddcci_ok) {
                    fprintf(stdout, "info: initializing ddc\n");
                    int i2c;
                    if ((rc = ddcci_find_i2c(ddc.drm, &i2c))) {
                        fprintf(stderr, "warning: failed to initialize ddc: no i2c device found for '%s': %s\n", ddc.drm, ddcci_strerror(rc));
                    } else if ((rc = ddcci_open(&ddcci, 6))) {
                        fprintf(stderr, "warning: failed to initialize ddc: i2c %d: %s\n", i2c, ddcci_strerror(rc));
                    } else {
                        fprintf(stdout, "info: using i2c %d for '%s'\n", i2c, ddc.drm);
                        ddcci_ok = true;
                    }
                }
                if (ddcci_ok) {
                    if (is_grabbed) {
                        fprintf(stdout, "info: switching display outputs\n");
                        if ((rc = ddcci_vcp_set(&ddcci, 0x60, ddc.output_other))) {
                            fprintf(stderr, "warning: failed to switch display output: %s\n", ddcci_strerror(rc));
                            ddcci_close(&ddcci);
                            ddcci_ok = false;
                        }
                    } else {
                        fprintf(stdout, "info: switching display outputs\n");
                        if ((rc = ddcci_vcp_set(&ddcci, 0x60, ddc.output_self))) {
                            fprintf(stderr, "warning: failed to switch display output: %s\n", ddcci_strerror(rc));
                            ddcci_close(&ddcci);
                            ddcci_ok = false;
                        }
                    }
                    fprintf(stdout, "info: switched display outputs\n");
                }
            }
            if (was_grabbed && !is_grabbed) {
                if (linger) {
                    fprintf(stdout, "info: not exiting since linger is enabled\n");
                } else {
                    should_exit = true;
                }
            }
            was_grabbed = is_grabbed;
            update_title();
        }
    }

    fprintf(stdout, "info: cleaning up\n");
    if (ddcci_ok) {
        ddcci_close(&ddcci);
    }
    purespice_disconnect();
    audio_free();
    return 0;
}
