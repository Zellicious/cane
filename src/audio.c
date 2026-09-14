#include "audio.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static ma_engine g_engine;
static bool g_audio_initialized = false;

struct Sound {
    ma_sound sound;
    ma_decoder decoder;
    unsigned char *data; // must outlive the decoder; freed in audio_sound_free
};

bool audio_init(void) {
    ma_result result = ma_engine_init(NULL, &g_engine);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "failed to initialize audio engine.\n");
        return false;
    }
    g_audio_initialized = true;
    return true;
}

void audio_cleanup(void) {
    if (g_audio_initialized) {
        ma_engine_uninit(&g_engine);
        g_audio_initialized = false;
    }
}

Sound* audio_sound_load(const char *path) {
    if (!g_audio_initialized) return NULL;

    size_t size;
    unsigned char *data = vfs_read_file(path, &size);
    if (!data) {
        fprintf(stderr, "failed to read sound file: %s\n", path);
        return NULL;
    }

    Sound *s = malloc(sizeof(Sound));
    if (!s) { vfs_free(data); return NULL; }

    if (ma_decoder_init_memory(data, size, NULL, &s->decoder) != MA_SUCCESS) {
        fprintf(stderr, "failed to decode sound file: %s\n", path);
        vfs_free(data);
        free(s);
        return NULL;
    }

    if (ma_sound_init_from_data_source(&g_engine, &s->decoder, 0, NULL, &s->sound) != MA_SUCCESS) {
        fprintf(stderr, "failed to init sound: %s\n", path);
        ma_decoder_uninit(&s->decoder);
        vfs_free(data);
        free(s);
        return NULL;
    }

    s->data = data;
    return s;
}

void audio_sound_free(Sound *sound) {
    if (!sound) return;
    ma_sound_uninit(&sound->sound);
    ma_decoder_uninit(&sound->decoder);
    vfs_free(sound->data);
    free(sound);
}

void audio_sound_play(Sound *sound) { if (sound) ma_sound_start(&sound->sound); }

void audio_sound_stop(Sound *sound) {
    if (sound) {
        ma_sound_stop(&sound->sound);
        ma_sound_seek_to_pcm_frame(&sound->sound, 0);
    }
}

void audio_sound_set_looping(Sound *sound, bool loop) {
    if (sound) ma_sound_set_looping(&sound->sound, loop ? MA_TRUE : MA_FALSE);
}

void audio_sound_set_volume(Sound *sound, float volume) {
    if (sound) ma_sound_set_volume(&sound->sound, volume);
}

void audio_sound_set_pitch(Sound *sound, float pitch) {
    if (sound) ma_sound_set_pitch(&sound->sound, pitch);
}

void audio_listener_set_position(float x, float y, float z) {
    if (g_audio_initialized) {
        ma_engine_listener_set_position(&g_engine, 0, x, y, z);
    }
}

void audio_listener_set_direction(float forward_x, float forward_y, float forward_z) {
    if (g_audio_initialized) {
        ma_engine_listener_set_direction(&g_engine, 0, forward_x, forward_y, forward_z);
    }
}

void audio_listener_set_velocity(float vel_x, float vel_y, float vel_z) {
    if (g_audio_initialized) {
        ma_engine_listener_set_velocity(&g_engine, 0, vel_x, vel_y, vel_z);
    }
}

void audio_sound_set_position(Sound *sound, float x, float y, float z) {
    if (sound) ma_sound_set_position(&sound->sound, x, y, z);
}

void audio_sound_set_velocity(Sound *sound, float vel_x, float vel_y, float vel_z) {
    if (sound) ma_sound_set_velocity(&sound->sound, vel_x, vel_y, vel_z);
}

void audio_sound_set_attenuation(Sound *sound, float min_dist, float max_dist) {
    if (sound) {
        ma_sound_set_attenuation_model(&sound->sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&sound->sound, min_dist);
        ma_sound_set_max_distance(&sound->sound, max_dist);
    }
}

void audio_sound_set_pan(Sound *sound, float pan) {
    if (sound) ma_sound_set_pan(&sound->sound, pan); // -1.0f (left) to 1.0f (right)
}
