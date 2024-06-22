/**
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

#include <purespice.h>
#include <stdbool.h>

struct audio_opts {
    int period_size;    // samples
    int buffer_latency; // milliseconds

    const char *sink; // optional
    const char *source; // optional

    void (*latency_cb)(double current_offset_ms, double total_latency_ms, double device_latency_ms);
};

void audio_playback_start(int channels, int sampleRate, PSAudioFormat format, uint32_t time);
void audio_playback_stop(void);
void audio_playback_volume(int channels, const uint16_t volume[]);
void audio_playback_mute(bool mute);
void audio_playback_data(uint8_t *data, size_t size);

void audio_record_start(int channels, int sampleRate, PSAudioFormat format);
void audio_record_stop(void);
void audio_record_volume(int channels, const uint16_t volume[]);
void audio_record_mute(bool mute);

int audio_pull(uint8_t *dst, int frames);
void audio_push(uint8_t *data, int frames);

bool audio_init(const struct audio_opts *opts);
void audio_free(void);
