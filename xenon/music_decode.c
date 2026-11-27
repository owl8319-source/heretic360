// Heretic 360: the music files' decoders, OGG Vorbis (stb_vorbis) and MP3 (dr_mp3), both public domain, both reading a
// file already in memory (xenon/i_musicfiles.c). They're kept in this file of their own, away from the game's headers,
// as both define macros and names of their own.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "../third_party/stb_vorbis.c"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../third_party/dr_mp3.h"

#include "music_decode.h"

struct music_decoder_s
{
    int is_mp3;
    stb_vorbis *vorbis;
    drmp3 mp3;
    int rate;
    int channels;
    // The file's own frames, for MP3's, which come in its own channel count.
    int16_t *scratch;
    int scratch_frames;
};

music_decoder_t *MusicFile_OpenDecoder(const void *data, size_t size, int is_mp3)
{
    music_decoder_t *d = calloc(1, sizeof(*d));

    if (d == NULL)
    {
        return NULL;
    }

    d->is_mp3 = is_mp3;

    if (is_mp3)
    {
        if (!drmp3_init_memory(&d->mp3, data, size, NULL))
        {
            free(d);
            return NULL;
        }
        d->rate = (int) d->mp3.sampleRate;
        d->channels = (int) d->mp3.channels;
    }
    else
    {
        int error = 0;
        stb_vorbis_info info;

        d->vorbis = stb_vorbis_open_memory(data, (int) size, &error, NULL);
        if (d->vorbis == NULL)
        {
            free(d);
            return NULL;
        }
        info = stb_vorbis_get_info(d->vorbis);
        d->rate = (int) info.sample_rate;
        d->channels = info.channels;
    }

    if (d->rate <= 0 || d->channels <= 0)
    {
        MusicFile_CloseDecoder(d);
        return NULL;
    }

    return d;
}

int MusicFile_Rate(const music_decoder_t *d)
{
    return d->rate;
}

int MusicFile_Channels(const music_decoder_t *d)
{
    return d->channels;
}

int MusicFile_Decode(music_decoder_t *d, int16_t *stereo, int frames)
{
    if (!d->is_mp3)
    {
        // stb_vorbis makes stereo of any channel count: a mono file's one channel goes to both.
        return stb_vorbis_get_samples_short_interleaved(d->vorbis, 2, stereo, frames * 2);
    }

    if (d->channels == 2)
    {
        return (int) drmp3_read_pcm_frames_s16(&d->mp3, (drmp3_uint64) frames, stereo);
    }

    // Mono (or more than two channels): read the file's frames, then make them stereo.
    if (d->scratch_frames < frames)
    {
        int16_t *const bigger = realloc(d->scratch, sizeof(int16_t) * (size_t) frames * (size_t) d->channels);

        if (bigger == NULL)
        {
            return 0;
        }
        d->scratch = bigger;
        d->scratch_frames = frames;
    }

    const int got = (int) drmp3_read_pcm_frames_s16(&d->mp3, (drmp3_uint64) frames, d->scratch);

    for (int i = 0; i < got; i++)
    {
        const int16_t *const in = d->scratch + i * d->channels;

        stereo[i * 2] = in[0];
        stereo[i * 2 + 1] = d->channels > 1 ? in[1] : in[0];
    }

    return got;
}

int MusicFile_Rewind(music_decoder_t *d)
{
    if (d->is_mp3)
    {
        return drmp3_seek_to_pcm_frame(&d->mp3, 0) ? 1 : 0;
    }

    return stb_vorbis_seek_start(d->vorbis);
}

void MusicFile_CloseDecoder(music_decoder_t *d)
{
    if (d == NULL)
    {
        return;
    }

    if (d->is_mp3)
    {
        drmp3_uninit(&d->mp3);
    }
    else if (d->vorbis != NULL)
    {
        stb_vorbis_close(d->vorbis);
    }

    free(d->scratch);
    free(d);
}
