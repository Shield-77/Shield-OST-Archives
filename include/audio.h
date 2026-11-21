#ifndef SHIELD_OST_AUDIO_H
#define SHIELD_OST_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

bool audio_init(void);
void audio_exit(void);
bool audio_open(const char *path, uint32_t known_duration_ms);
void audio_stop(void);
void audio_toggle_pause(void);
void audio_update(void);
bool audio_playing(void);
bool audio_paused(void);
uint32_t audio_position_ms(void);
uint32_t audio_duration_ms(void);
const char *audio_error(void);

#endif
