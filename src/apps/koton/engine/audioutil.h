//
// audioutil.h -- what Koton needs around AudioKit and the kit does not do: the soft limiter, the WAV file, the search
// for a SoundFont.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _koton_audioutil_h
#define _koton_audioutil_h

struct wav_file;

float soft_clip (float x);
void f32_to_s16 (const float *left, const float *right, short *out, int frames, float gain);
wav_file *wav_begin (const char *path, int rate, int channels, long long frames);
int wav_write (wav_file *w, const short *frames, int n);
int wav_end (wav_file *w);
int soundfont_find (const char *preferred, char *out, int cap);

#endif
