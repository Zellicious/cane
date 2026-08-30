#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

typedef struct Sound Sound;

bool audio_init(void);
void audio_cleanup(void);

Sound* audio_sound_load(const char *path);
void audio_sound_free(Sound *sound);
void audio_sound_play(Sound *sound);
void audio_sound_stop(Sound *sound);
void audio_sound_set_looping(Sound *sound, bool loop);
void audio_sound_set_volume(Sound *sound, float volume);
void audio_sound_set_pitch(Sound *sound, float pitch);

#endif
