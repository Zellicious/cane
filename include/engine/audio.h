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

void audio_listener_set_position(float x, float y, float z);
void audio_listener_set_direction(float forward_x, float forward_y, float forward_z);
void audio_listener_set_velocity(float vel_x, float vel_y, float vel_z);

void audio_sound_set_position(Sound *sound, float x, float y, float z);
void audio_sound_set_velocity(Sound *sound, float vel_x, float vel_y, float vel_z);
void audio_sound_set_attenuation(Sound *sound, float min_dist, float max_dist);
void audio_sound_set_pan(Sound *sound, float pan);

#endif
