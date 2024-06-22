#pragma once
#include <stdbool.h>
#include <stdint.h>

void audiodev_playback_setup(const char *sink, int channels, int sampleRate, int requestedPeriodFrames, int *maxPeriodFrames, int *startFrames);
void audiodev_playback_start(void);
void audiodev_playback_stop(void);
void audiodev_playback_volume(int channels, const uint16_t volume[]);
void audiodev_playback_mute(bool mute);
uint64_t audiodev_playback_latency(void);

void audiodev_record_start(const char *source, int channels, int sampleRate);
void audiodev_record_stop(void);
void audiodev_record_volume(int channels, const uint16_t volume[]);
void audiodev_record_mute(bool mute);

bool audiodev_init(void);
void audiodev_free(void);
