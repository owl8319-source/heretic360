// Heretic 360: the decoders for the music files (xenon/music_decode.c), for xenon/i_musicfiles.c.

#ifndef MUSIC_DECODE_H
#define MUSIC_DECODE_H

#include <stddef.h>
#include <stdint.h>

typedef struct music_decoder_s music_decoder_t;

// A decoder for an OGG Vorbis (is_mp3 0) or MP3 file in memory, which must stay there until it's closed; NULL if the
// file isn't one.
music_decoder_t *MusicFile_OpenDecoder(const void *data, size_t size, int is_mp3);
// The file's sample rate and channel count.
int MusicFile_Rate(const music_decoder_t *d);
int MusicFile_Channels(const music_decoder_t *d);
// Up to frames stereo frames, 16-bit, at the file's rate; 0 at its end.
int MusicFile_Decode(music_decoder_t *d, int16_t *stereo, int frames);
// Back to the start, for a looping track; 0 if it can't.
int MusicFile_Rewind(music_decoder_t *d);
void MusicFile_CloseDecoder(music_decoder_t *d);

#endif
