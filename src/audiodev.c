/**
 * Copyright © 2024 Patrick Gaskin
 *
 * Modified from:
 * Looking Glass
 * Copyright © 2017-2024 The Looking Glass Authors
 * https://looking-glass.io
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

#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>

#include "lg_common/debug.h"
#include "lg_common/util.h"

#include "audio.h"
#include "audiodev.h"

typedef enum {
    STREAM_STATE_INACTIVE,
    STREAM_STATE_ACTIVE,
    STREAM_STATE_DRAINING
} StreamState;

struct PipeWire {
    struct pw_loop *loop;
    struct pw_context *context;
    struct pw_thread_loop *thread;

    struct
    {
        struct pw_stream *stream;
        struct spa_io_rate_match *rateMatch;
        struct pw_time time;

        int channels;
        int sampleRate;
        int stride;
        int maxPeriodFrames;
        int startFrames;

        StreamState state;
    } playback;

    struct
    {
        struct pw_stream *stream;

        int channels;
        int sampleRate;
        int stride;

        bool active;
    } record;
};

static struct PipeWire pw = {0};

static void audiodev_on_playback_io_changed(void *userdata, uint32_t id, void *data, uint32_t size) {
    switch (id) {
    case SPA_IO_RateMatch:
        pw.playback.rateMatch = data;
        break;
    }
}

static void audiodev_on_playback_process(void *userdata) {
    struct pw_buffer *pbuf;

#if PW_CHECK_VERSION(0, 3, 50)
    if (pw_stream_get_time_n(pw.playback.stream, &pw.playback.time,
                             sizeof(pw.playback.time)) < 0)
#else
    if (pw_stream_get_time(pw.playback.stream, &pw.playback.time) < 0)
#endif
        DEBUG_ERROR("pw_stream_get_time failed");

    if (!(pbuf = pw_stream_dequeue_buffer(pw.playback.stream))) {
        DEBUG_WARN("out of buffers");
        return;
    }

    struct spa_buffer *sbuf = pbuf->buffer;
    uint8_t *dst;

    if (!(dst = sbuf->datas[0].data))
        return;

    int frames = sbuf->datas[0].maxsize / pw.playback.stride;
    if (pw.playback.rateMatch && pw.playback.rateMatch->size > 0)
        frames = min(frames, pw.playback.rateMatch->size);

    frames = audio_pull(dst, frames);
    if (!frames) {
        sbuf->datas[0].chunk->size = 0;
        pw_stream_queue_buffer(pw.playback.stream, pbuf);
        return;
    }

    pbuf->size = frames;
    sbuf->datas[0].chunk->offset = 0;
    sbuf->datas[0].chunk->stride = pw.playback.stride;
    sbuf->datas[0].chunk->size = frames * pw.playback.stride;

    pw_stream_queue_buffer(pw.playback.stream, pbuf);
}

static void audiodev_onPlaybackDrained(void *userdata) {
    pw_thread_loop_lock(pw.thread);
    pw_stream_set_active(pw.playback.stream, false);
    pw.playback.state = STREAM_STATE_INACTIVE;
    pw_thread_loop_unlock(pw.thread);
}

bool audiodev_init(void) {
    pw_init(NULL, NULL);

    pw.loop = pw_loop_new(NULL);
    pw.context = pw_context_new(
        pw.loop,
        pw_properties_new(
            // Request real-time priority on the PipeWire threads
            PW_KEY_CONFIG_NAME, "client-rt.conf",
            NULL),
        0);
    if (!pw.context) {
        DEBUG_ERROR("Failed to create a context");
        goto err;
    }

    /* this is just to test for PipeWire availabillity */
    struct pw_core *core = pw_context_connect(pw.context, NULL, 0);
    if (!core)
        goto err_context;

    /* PipeWire is available so create the loop thread and start it */
    pw.thread = pw_thread_loop_new_full(pw.loop, "PipeWire", NULL);
    if (!pw.thread) {
        DEBUG_ERROR("Failed to create the thread loop");
        goto err_context;
    }

    pw_thread_loop_start(pw.thread);
    return true;

err_context:
    pw_context_destroy(pw.context);

err:
    pw_loop_destroy(pw.loop);
    pw_deinit();
    return false;
}

static void audiodev_playback_stop_stream(void) {
    if (!pw.playback.stream)
        return;

    pw_thread_loop_lock(pw.thread);
    pw_stream_destroy(pw.playback.stream);
    pw.playback.stream = NULL;
    pw.playback.rateMatch = NULL;
    pw_thread_loop_unlock(pw.thread);
}

void audiodev_playback_setup(const char *sink, int channels, int sampleRate, int requestedPeriodFrames, int *maxPeriodFrames, int *startFrames) {
    const struct spa_pod *params[1];
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    static const struct pw_stream_events events =
        {
            .version = PW_VERSION_STREAM_EVENTS,
            .io_changed = audiodev_on_playback_io_changed,
            .process = audiodev_on_playback_process,
            .drained = audiodev_onPlaybackDrained};

    if (pw.playback.stream &&
        pw.playback.channels == channels &&
        pw.playback.sampleRate == sampleRate) {
        *maxPeriodFrames = pw.playback.maxPeriodFrames;
        *startFrames = pw.playback.startFrames;
        return;
    }

    audiodev_playback_stop_stream();

    char requestedNodeLatency[32];
    snprintf(requestedNodeLatency, sizeof(requestedNodeLatency), "%d/%d",
             requestedPeriodFrames, sampleRate);

    pw.playback.channels = channels;
    pw.playback.sampleRate = sampleRate;
    pw.playback.stride = sizeof(float) * channels;

    pw_thread_loop_lock(pw.thread);

    struct pw_properties *props =
        pw_properties_new(
            PW_KEY_NODE_NAME, "spicy-kvm",
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Game",
            PW_KEY_NODE_LATENCY, requestedNodeLatency,
            NULL);

    if (sink) {
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, sink);
    }

    pw.playback.stream = pw_stream_new_simple(
        pw.loop,
        "spicy-kvm",
        props,
        &events,
        NULL);

    // The user can override the default node latency with the audiodev_LATENCY
    // environment variable, so get the actual node latency value from the stream.
    // The actual quantum size may be lower than this value depending on what else
    // is using the audio device, but we can treat this value as a maximum
    const struct pw_properties *properties =
        pw_stream_get_properties(pw.playback.stream);
    const char *actualNodeLatency =
        pw_properties_get(properties, PW_KEY_NODE_LATENCY);
    DEBUG_ASSERT(actualNodeLatency != NULL);

    unsigned num, denom;
    if (sscanf(actualNodeLatency, "%u/%u", &num, &denom) != 2 ||
        denom != sampleRate) {
        DEBUG_WARN(
            "audiodev_LATENCY value '%s' is invalid or does not match stream sample "
            "rate; using %d/%d",
            actualNodeLatency, requestedPeriodFrames,
            sampleRate);

        struct spa_dict_item items[] = {
            {PW_KEY_NODE_LATENCY, requestedNodeLatency}};
        pw_stream_update_properties(pw.playback.stream,
                                    &SPA_DICT_INIT_ARRAY(items));

        pw.playback.maxPeriodFrames = requestedPeriodFrames;
    } else
        pw.playback.maxPeriodFrames = num;

    // If the previous quantum size was very small, PipeWire can request two full
    // periods almost immediately at the start of playback
    pw.playback.startFrames = pw.playback.maxPeriodFrames * 2;

    *maxPeriodFrames = pw.playback.maxPeriodFrames;
    *startFrames = pw.playback.startFrames;

    if (!pw.playback.stream) {
        pw_thread_loop_unlock(pw.thread);
        DEBUG_ERROR("Failed to create the stream");
        return;
    }

    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
                                           &SPA_AUDIO_INFO_RAW_INIT(
                                                   .format = SPA_AUDIO_FORMAT_F32,
                                                   .channels = channels,
                                                   .rate = sampleRate));

    pw_stream_connect(
        pw.playback.stream,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS |
            PW_STREAM_FLAG_INACTIVE,
        params, 1);

    pw_thread_loop_unlock(pw.thread);
}

void audiodev_playback_start(void) {
    if (!pw.playback.stream)
        return;

    if (pw.playback.state != STREAM_STATE_ACTIVE) {
        pw_thread_loop_lock(pw.thread);

        switch (pw.playback.state) {
        case STREAM_STATE_INACTIVE:
            pw_stream_set_active(pw.playback.stream, true);
            pw.playback.state = STREAM_STATE_ACTIVE;
            break;

        case STREAM_STATE_DRAINING:
            // We are in the middle of draining the PipeWire buffers; we need to
            // wait for this to complete before allowing the new playback to start
            break;

        default:
            DEBUG_UNREACHABLE();
        }

        pw_thread_loop_unlock(pw.thread);
    }
}

void audiodev_playback_stop(void) {
    if (pw.playback.state != STREAM_STATE_ACTIVE)
        return;

    pw_thread_loop_lock(pw.thread);
    pw_stream_flush(pw.playback.stream, true);
    pw.playback.state = STREAM_STATE_DRAINING;
    pw_thread_loop_unlock(pw.thread);
}

void audiodev_playback_volume(int channels, const uint16_t volume[]) {
    if (channels != pw.playback.channels)
        return;

    float param[channels];
    for (int i = 0; i < channels; ++i)
        param[i] = 9.3234e-7 * pow(1.000211902, volume[i]) - 0.000172787;

    pw_thread_loop_lock(pw.thread);
    pw_stream_set_control(pw.playback.stream, SPA_PROP_channelVolumes,
                          channels, param, 0);
    pw_thread_loop_unlock(pw.thread);
}

void audiodev_playback_mute(bool mute) {
    pw_thread_loop_lock(pw.thread);
    float val = mute ? 1.0f : 0.0f;
    pw_stream_set_control(pw.playback.stream, SPA_PROP_mute, 1, &val, 0);
    pw_thread_loop_unlock(pw.thread);
}

uint64_t audiodev_playback_latency(void) {
    if (pw.playback.time.rate.num == 0)
        return 0;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    // diff in ns
    int64_t diff = SPA_TIMESPEC_TO_NSEC(&ts) - pw.playback.time.now;

    // elapsed frames
    int64_t elapsed =
        (pw.playback.time.rate.denom * diff) /
        (pw.playback.time.rate.num * SPA_NSEC_PER_SEC);

    const int64_t buffered = pw.playback.time.buffered + pw.playback.time.queued;
    int64_t latency = (buffered * 1000 / pw.playback.sampleRate) +
                      ((pw.playback.time.delay - elapsed) * 1000 *
                       pw.playback.time.rate.num / pw.playback.time.rate.denom);

    return max(0, -latency);
}

static void audiodev_record_stop_stream(void) {
    if (!pw.record.stream)
        return;

    pw_thread_loop_lock(pw.thread);
    pw_stream_destroy(pw.record.stream);
    pw.record.stream = NULL;
    pw_thread_loop_unlock(pw.thread);
}

static void audiodev_on_record_process(void *userdata) {
    struct pw_buffer *pbuf;

    if (!(pbuf = pw_stream_dequeue_buffer(pw.record.stream))) {
        DEBUG_WARN("out of buffers");
        return;
    }

    struct spa_buffer *sbuf = pbuf->buffer;
    uint8_t *dst;

    if (!(dst = sbuf->datas[0].data))
        return;

    dst += sbuf->datas[0].chunk->offset;
    audio_push(dst,
               min(
                   sbuf->datas[0].chunk->size,
                   sbuf->datas[0].maxsize - sbuf->datas[0].chunk->offset) /
                   pw.record.stride);

    pw_stream_queue_buffer(pw.record.stream, pbuf);
}

void audiodev_record_start(const char *source, int channels, int sampleRate) {
    const struct spa_pod *params[1];
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    static const struct pw_stream_events events =
        {
            .version = PW_VERSION_STREAM_EVENTS,
            .process = audiodev_on_record_process};

    if (pw.record.stream &&
        pw.record.channels == channels &&
        pw.record.sampleRate == sampleRate) {
        if (!pw.record.active) {
            pw_thread_loop_lock(pw.thread);
            pw_stream_set_active(pw.record.stream, true);
            pw.record.active = true;
            pw_thread_loop_unlock(pw.thread);
        }
        return;
    }

    audiodev_record_stop_stream();

    pw.record.channels = channels;
    pw.record.sampleRate = sampleRate;
    pw.record.stride = sizeof(uint16_t) * channels;

    struct pw_properties *props =
        pw_properties_new(
            PW_KEY_NODE_NAME, "spicy-kvm",
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Game",
            NULL);

    if (source) {
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, source);
    }

    pw_thread_loop_lock(pw.thread);
    pw.record.stream = pw_stream_new_simple(
        pw.loop,
        "spicy-kvm",
        props,
        &events,
        NULL);

    if (!pw.record.stream) {
        pw_thread_loop_unlock(pw.thread);
        DEBUG_ERROR("Failed to create the stream");
        return;
    }

    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
                                           &SPA_AUDIO_INFO_RAW_INIT(
                                                   .format = SPA_AUDIO_FORMAT_S16,
                                                   .channels = channels,
                                                   .rate = sampleRate));

    pw_stream_connect(
        pw.record.stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS,
        params, 1);

    pw_thread_loop_unlock(pw.thread);
    pw.record.active = true;
}

void audiodev_record_stop(void) {
    if (!pw.record.active)
        return;

    pw_thread_loop_lock(pw.thread);
    pw_stream_set_active(pw.record.stream, false);
    pw.record.active = false;
    pw_thread_loop_unlock(pw.thread);
}

void audiodev_record_volume(int channels, const uint16_t volume[]) {
    if (channels != pw.record.channels)
        return;

    float param[channels];
    for (int i = 0; i < channels; ++i)
        param[i] = 9.3234e-7 * pow(1.000211902, volume[i]) - 0.000172787;

    pw_thread_loop_lock(pw.thread);
    pw_stream_set_control(pw.record.stream, SPA_PROP_channelVolumes,
                          channels, param, 0);
    pw_thread_loop_unlock(pw.thread);
}

void audiodev_record_mute(bool mute) {
    pw_thread_loop_lock(pw.thread);
    float val = mute ? 1.0f : 0.0f;
    pw_stream_set_control(pw.record.stream, SPA_PROP_mute, 1, &val, 0);
    pw_thread_loop_unlock(pw.thread);
}

void audiodev_free(void) {
    audiodev_playback_stop_stream();
    audiodev_record_stop_stream();
    pw_thread_loop_stop(pw.thread);
    pw_thread_loop_destroy(pw.thread);
    pw_context_destroy(pw.context);
    pw_loop_destroy(pw.loop);

    pw.loop = NULL;
    pw.context = NULL;
    pw.thread = NULL;

    pw_deinit();
}