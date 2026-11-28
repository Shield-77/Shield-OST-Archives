#ifndef SHIELD_ONLINE_AUDIO_H
#define SHIELD_ONLINE_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

bool audio_init(void);
void audio_exit(void);
bool audio_open(const char *path, uint32_t known_duration_ms);
bool audio_open_stream(const char *path, uint32_t known_duration_ms);
void audio_set_streaming(bool incoming);
void audio_stop(void);
void audio_toggle_pause(void);
void audio_update(void);
bool audio_playing(void);
bool audio_paused(void);
bool audio_buffering(void);
uint32_t audio_position_ms(void);
uint32_t audio_duration_ms(void);
const char *audio_error(void);
unsigned audio_initial_peak(void);
unsigned audio_initial_frames(void);
unsigned audio_consumed_blocks(void);
unsigned audio_sample_rate(void);

#endif
