// Heretic 360: Chocolate Doom's sound interface (i_sound.h) on SDL3's audio, which SDL-xenon plays through xsdk's
// audio API at 48 kHz.
//
// Sound effects: the WAD's DMX sound lumps, 8-bit unsigned samples, mixed here into 16-bit stereo on SDL's audio
// thread, with Chocolate Doom's panning and Heretic's pitch variation.
//
// Music: Chocolate Doom's OPL (AdLib) player, i_oplmusic.c, as Heretic sounded on a Sound Blaster, its chip emulated
// by Nuked OPL3 and added into the same mix (xenon/opl_xsdk.c).

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "deh_str.h"
#include "doomtype.h"
#include "i_sound.h"
#include "i_musicfiles.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

enum
{
    NUM_CHANNELS = 16,
    MIX_RATE = 48000,
    MIX_CHANNELS = 2,
    // Frames mixed at a time on SDL's audio thread.
    MIX_CHUNK_FRAMES = 256,
};

// A sound effect ready to mix: its 8-bit unsigned samples, inside its WAD lump, which stays in memory.
typedef struct
{
    const byte *samples;
    uint32_t length;
    uint32_t rate;
} cached_sound_t;

typedef struct
{
    const cached_sound_t *sound;
    // Where the channel is in its samples, 16.16 fixed point, and how far it moves an output frame.
    uint64_t position;
    uint32_t step;
    int left;
    int right;
    boolean playing;
} mix_channel_t;

static mix_channel_t channels[NUM_CHANNELS];
static SDL_AudioStream *stream = NULL;
static boolean sound_initialized = false;

// The longest the game has waited for the mixer's lock since i_video.c last asked (I_TakeSoundLockWaitMS), for the
// slow frame report.
static Uint64 lock_wait_max_ns = 0;

static void LockStream(void)
{
    const Uint64 start = SDL_GetTicksNS();

    SDL_LockAudioStream(stream);

    const Uint64 waited = SDL_GetTicksNS() - start;

    if (waited > lock_wait_max_ns)
    {
        lock_wait_max_ns = waited;
    }
}

static void UnlockStream(void)
{
    SDL_UnlockAudioStream(stream);
}

int I_TakeSoundLockWaitMS(void)
{
    const int ms = (int) (lock_wait_max_ns / 1000000);

    lock_wait_max_ns = 0;
    return ms;
}

// Configuration variables, kept as Chocolate Doom keeps them.

int snd_samplerate = 44100;
int snd_cachesize = 64 * 1024 * 1024;
int snd_maxslicetime_ms = 28;
char *snd_musiccmd = "";
int snd_pitchshift = -1;
int snd_musicdevice = SNDDEVICE_SB;
int snd_sfxdevice = SNDDEVICE_SB;
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
char *music_pack_path = "";
char *timidity_cfg_path = "";

static int snd_sbport = 0;
static int snd_sbirq = 0;
static int snd_sbdma = 0;
static int snd_mport = 0;

// The mixer, on SDL's audio thread, which holds the stream's lock while it runs.

// The music, added into the mix after the sound effects: the OPL chip (xenon/opl_xsdk.c), when music is on, and the
// songs' recordings (xenon/i_musicfiles.c), when they're on. Only one of them plays at a time (heretic/s_sound.c).
static void (*music_generator)(int32_t *mix, int frames) = NULL;
static void (*file_music_generator)(int32_t *mix, int frames) = NULL;

static void MixChannels(int16_t *out, int frames)
{
    int32_t mix[MIX_CHUNK_FRAMES * MIX_CHANNELS];

    memset(mix, 0, sizeof(int32_t) * frames * MIX_CHANNELS);

    for (int c = 0; c < NUM_CHANNELS; c++)
    {
        mix_channel_t *const ch = &channels[c];

        if (!ch->playing)
        {
            continue;
        }

        const cached_sound_t *const snd = ch->sound;

        for (int f = 0; f < frames; f++)
        {
            const uint32_t index = (uint32_t) (ch->position >> 16);

            if (index >= snd->length)
            {
                ch->playing = false;
                break;
            }

            // Linear between this sample and the next, so a pitched or 11 kHz sound doesn't buzz.
            const int32_t frac = (int32_t) (ch->position & 0xffff);
            const int32_t a = ((int32_t) snd->samples[index] - 128) << 8;
            const int32_t b = index + 1 < snd->length ? ((int32_t) snd->samples[index + 1] - 128) << 8 : a;
            const int32_t sample = a + (((b - a) * frac) >> 16);

            mix[f * 2] += (sample * ch->left) >> 8;
            mix[f * 2 + 1] += (sample * ch->right) >> 8;

            ch->position += ch->step;
        }
    }

    if (music_generator != NULL)
    {
        music_generator(mix, frames);
    }

    if (file_music_generator != NULL)
    {
        file_music_generator(mix, frames);
    }

    for (int i = 0; i < frames * MIX_CHANNELS; i++)
    {
        int32_t v = mix[i];

        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;

        out[i] = (int16_t) v;
    }
}

static void SDLCALL FeedAudio(void *userdata, SDL_AudioStream *s, int additional_amount, int total_amount)
{
    int16_t buffer[MIX_CHUNK_FRAMES * MIX_CHANNELS];
    int frames = additional_amount / (int) (sizeof(int16_t) * MIX_CHANNELS);

    (void) userdata;
    (void) total_amount;

    while (frames > 0)
    {
        const int now = frames < MIX_CHUNK_FRAMES ? frames : MIX_CHUNK_FRAMES;

        MixChannels(buffer, now);

        if (!SDL_PutAudioStreamData(s, buffer, now * (int) (sizeof(int16_t) * MIX_CHANNELS)))
        {
            return;
        }

        frames -= now;
    }
}

// Sound effects.

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sound effects play the lump of the one they link to. Heretic's lumps have no DS prefix.
    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    M_StringCopy(buf, DEH_String(sfx->name), buf_len);
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    char namebuf[9];

    GetSfxLumpName(sfxinfo, namebuf, sizeof(namebuf));

    return W_GetNumForName(namebuf);
}

// Reads a DMX sound lump as Chocolate Doom's i_sdlsound.c does; NULL if it isn't one.
static cached_sound_t *CacheSound(sfxinfo_t *sfxinfo)
{
    char namebuf[9];
    int lumpnum;
    unsigned int lumplen;
    unsigned int length;
    byte *data;
    cached_sound_t *snd;

    if (sfxinfo->driver_data != NULL)
    {
        return sfxinfo->driver_data;
    }

    GetSfxLumpName(sfxinfo, namebuf, sizeof(namebuf));
    lumpnum = W_CheckNumForName(namebuf);

    if (lumpnum < 0)
    {
        return NULL;
    }

    data = W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = W_LumpLength(lumpnum);

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
    {
        return NULL;
    }

    length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

    // DMX ignores sounds of 48 samples or fewer, and longer than their lumps.
    if (length > lumplen - 8 || length <= 48)
    {
        return NULL;
    }

    snd = Z_Malloc(sizeof(*snd), PU_STATIC, NULL);
    snd->rate = (data[3] << 8) | data[2];
    // DMX skips the first and last 16 samples.
    snd->samples = data + 8 + 16;
    snd->length = length - 32;

    if (snd->rate == 0)
    {
        snd->rate = 11025;
    }

    sfxinfo->driver_data = snd;

    return snd;
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    if (!sound_initialized)
    {
        return;
    }

    printf("I_PrecacheSounds: caching %i sound effects\n", num_sounds);

    for (int i = 0; i < num_sounds; ++i)
    {
        CacheSound(&sounds[i]);
    }
}

static void SetPanning(mix_channel_t *ch, int vol, int sep)
{
    int left = ((254 - sep) * vol) / 127;
    int right = (sep * vol) / 127;

    if (left < 0) left = 0;
    else if (left > 255) left = 255;
    if (right < 0) right = 0;
    else if (right > 255) right = 255;

    ch->left = left;
    ch->right = right;
}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return;
    }

    LockStream();
    SetPanning(&channels[channel], vol, sep);
    UnlockStream();
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch)
{
    const cached_sound_t *snd;
    double speed = 1.0;

    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return -1;
    }

    snd = CacheSound(sfxinfo);
    if (snd == NULL)
    {
        return -1;
    }

    // Chocolate Doom's measure of vanilla's pitch shift: the sound plays 1 / (2 - pitch / 127) as fast.
    if (snd_pitchshift && pitch != NORM_PITCH && pitch > 0)
    {
        speed = 1.0 / (2.0 - (double) pitch / NORM_PITCH);
    }

    LockStream();
    mix_channel_t *const ch = &channels[channel];
    ch->sound = snd;
    ch->position = 0;
    ch->step = (uint32_t) (((double) snd->rate * 65536.0 * speed) / MIX_RATE);
    SetPanning(ch, vol, sep);
    ch->playing = true;
    UnlockStream();

    return channel;
}

void I_StopSound(int channel)
{
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return;
    }

    LockStream();
    channels[channel].playing = false;
    UnlockStream();
}

boolean I_SoundIsPlaying(int channel)
{
    boolean playing;

    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return false;
    }

    LockStream();
    playing = channels[channel].playing;
    UnlockStream();

    return playing;
}

void I_UpdateSound(void)
{
}

void I_InitSound(GameMission_t mission)
{
    const SDL_AudioSpec spec = { SDL_AUDIO_S16, MIX_CHANNELS, MIX_RATE };

    (void) mission;

    if (M_CheckParm("-nosound") > 0 || M_CheckParm("-nosfx") > 0 || snd_sfxdevice == SNDDEVICE_NONE)
    {
        printf("I_InitSound: sound effects off\n");
        return;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        printf("I_InitSound: SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return;
    }

    memset(channels, 0, sizeof(channels));

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedAudio, NULL);
    if (stream == NULL)
    {
        printf("I_InitSound: SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    if (!SDL_ResumeAudioStreamDevice(stream))
    {
        printf("I_InitSound: SDL_ResumeAudioStreamDevice failed: %s\n", SDL_GetError());
    }

    sound_initialized = true;
    printf("I_InitSound: sound effects at %i Hz\n", MIX_RATE);
}

void I_ShutdownSound(void)
{
    if (!sound_initialized)
    {
        return;
    }

    // The music plays in the mixer, so it stops first.
    I_ShutdownMusic();

    sound_initialized = false;
    SDL_DestroyAudioStream(stream);
    stream = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// Music: Chocolate Doom's OPL (AdLib) player, i_oplmusic.c, its chip emulated in the mixer (xenon/opl_xsdk.c).

static boolean music_initialized = false;

int I_SoundMixRate(void)
{
    return sound_initialized ? MIX_RATE : 0;
}

// Sets or clears the music generator the mixer runs; false if there's no mixer.
int I_SetMusicGenerator(void (*generator)(int32_t *mix, int frames))
{
    if (stream == NULL)
    {
        return generator == NULL;
    }

    LockStream();
    music_generator = generator;
    UnlockStream();

    return true;
}

// Sets or clears the music files' generator (xenon/i_musicfiles.c); false if there's no mixer.
int I_SetFileMusicGenerator(void (*generator)(int32_t *mix, int frames))
{
    if (stream == NULL)
    {
        return generator == NULL;
    }

    LockStream();
    file_music_generator = generator;
    UnlockStream();

    return true;
}

void I_InitMusic(void)
{
    if (M_CheckParm("-nomusic") > 0 || M_CheckParm("-nosound") > 0)
    {
        printf("I_InitMusic: music off\n");
        return;
    }

    // Every music device but none and the PC speaker plays on the OPL here, as the default, Sound Blaster, does.
    if (snd_musicdevice == SNDDEVICE_NONE || snd_musicdevice == SNDDEVICE_PCSPEAKER)
    {
        printf("I_InitMusic: music off (snd_musicdevice %i)\n", snd_musicdevice);
        return;
    }

    if (!sound_initialized)
    {
        printf("I_InitMusic: no sound, so no music\n");
        return;
    }

    music_initialized = music_opl_module.Init();

    if (music_initialized)
    {
        printf("I_InitMusic: OPL music\n");
    }
}

void I_ShutdownMusic(void)
{
    if (music_initialized)
    {
        music_opl_module.Shutdown();
        music_initialized = false;
    }
}

void I_SetMusicVolume(int volume)
{
    if (music_initialized)
    {
        music_opl_module.SetMusicVolume(volume);
    }
}

void I_PauseSong(void)
{
    if (music_initialized)
    {
        music_opl_module.PauseMusic();
    }
}

void I_ResumeSong(void)
{
    if (music_initialized)
    {
        music_opl_module.ResumeMusic();
    }
}

// Music calls that take longer than this go in the log, with how long they took.
#define SLOW_MUSIC_CALL_MS 50

static void ReportSlowMusicCall(const char *call, Uint64 started)
{
    const Uint64 ms = SDL_GetTicks() - started;

    if (ms > SLOW_MUSIC_CALL_MS)
    {
        printf("heretic360: %s took %u ms\n", call, (unsigned) ms);
    }
}

void *I_RegisterSong(void *data, int len)
{
    void *handle;

    if (!music_initialized)
    {
        return NULL;
    }

    const Uint64 started = SDL_GetTicks();
    handle = music_opl_module.RegisterSong(data, len);
    ReportSlowMusicCall("I_RegisterSong", started);

    return handle;
}

void I_UnRegisterSong(void *handle)
{
    if (music_initialized && handle != NULL)
    {
        const Uint64 started = SDL_GetTicks();
        music_opl_module.UnRegisterSong(handle);
        ReportSlowMusicCall("I_UnRegisterSong", started);
    }
}

void I_PlaySong(void *handle, boolean looping)
{
    if (music_initialized && handle != NULL)
    {
        const Uint64 started = SDL_GetTicks();
        music_opl_module.PlaySong(handle, looping);
        ReportSlowMusicCall("I_PlaySong", started);
    }
}

void I_StopSong(void)
{
    if (music_initialized)
    {
        const Uint64 started = SDL_GetTicks();
        music_opl_module.StopSong();
        ReportSlowMusicCall("I_StopSong", started);
    }
}

boolean I_MusicIsPlaying(void)
{
    if (!music_initialized)
    {
        return false;
    }

    return music_opl_module.MusicIsPlaying();
}

void I_InitTimidityConfig(void)
{
}

void I_BindSoundVariables(void)
{
    M_BindIntVariable("snd_musicdevice",         &snd_musicdevice);
    M_BindIntVariable("snd_sfxdevice",           &snd_sfxdevice);
    M_BindIntVariable("snd_sbport",              &snd_sbport);
    M_BindIntVariable("snd_sbirq",               &snd_sbirq);
    M_BindIntVariable("snd_sbdma",               &snd_sbdma);
    M_BindIntVariable("snd_mport",               &snd_mport);
    M_BindIntVariable("snd_maxslicetime_ms",     &snd_maxslicetime_ms);
    M_BindStringVariable("snd_musiccmd",         &snd_musiccmd);
    M_BindStringVariable("snd_dmxoption",        &snd_dmxoption);
    M_BindIntVariable("snd_samplerate",          &snd_samplerate);
    M_BindIntVariable("snd_cachesize",           &snd_cachesize);
    M_BindIntVariable("opl_io_port",             &opl_io_port);
    // The songs' recordings, as files, in place of the AdLib music (xenon/i_musicfiles.c).
    M_BindIntVariable("music_files",             &music_files);
    M_BindIntVariable("snd_pitchshift",          &snd_pitchshift);
    M_BindStringVariable("music_pack_path",      &music_pack_path);
    M_BindStringVariable("timidity_cfg_path",    &timidity_cfg_path);
    M_BindIntVariable("use_libsamplerate",       &use_libsamplerate);
    M_BindFloatVariable("libsamplerate_scale",   &libsamplerate_scale);
}

// [crispy] Whether a block of memory is a .mid file, or a .mus file (i_oplmusic.c asks).

boolean IsMid(byte *mem, int len)
{
    return len > 4 && !memcmp(mem, "MThd", 4);
}

boolean IsMus(byte *mem, int len)
{
    return len > 4 && !memcmp(mem, "MUS\x1a", 4);
}
