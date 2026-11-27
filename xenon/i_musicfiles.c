// Heretic 360: recordings of Heretic's songs in place of the AdLib music. Heretic never had a CD soundtrack (Hexen,
// which did, plays its tracks the same way in Hexen 360), so the files are named after the songs' lumps in the WAD, in
// a music folder beside the .xex:
//
//   music\e1m1.ogg ... music\e3m9.ogg, music\titl.ogg, music\intr.ogg, music\cptd.ogg   (or .mp3)
//
// "mus_e1m1.ogg" is found too, any case. Episodes 4 and 5 play songs of the first three, as the game does, unless
// there are files of their own (e4m1.ogg and on; heretic/s_sound.c). A song with no file plays as AdLib.
//
// The folder is listed once, the first time the setting is on, so a song without a file isn't looked for on the hard
// drive each time a level starts: on the console a file looked for and not found can wait on the drive. A song's file
// is read whole and decoded on a thread of this file's own, on another core than the game's, a little ahead of the
// mixer: the game never waits on the drive or the decoder, and the mixer, on SDL's audio thread (xenon/i_sound.c),
// only copies what's ready. The music is turned into the mixer's 48 kHz stereo from the file's rate as it's decoded.
// (The same player as Hexen 360's CD music.)

#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL3/SDL.h>

#include "xsdk/thread.h"

#include "doomtype.h"
#include "i_musicfiles.h"
#include "m_misc.h"
#include "music_decode.h"

int I_SoundMixRate(void);
int I_SetFileMusicGenerator(void (*generator)(int32_t *mix, int frames));

int music_files = 0;

#define MUSIC_FOLDER "music"
// Files the folder may hold for the game: Heretic has 30 songs, and Crispy looks for 30 more of the later episodes.
#define MAX_FILES 96
// How much decoded music waits for the mixer: about two thirds of a second at 48 kHz.
#define RING_FRAMES 32768
// How many of the file's frames the decoder takes at a time.
#define DECODE_FRAMES 2048
// The lowest file rate played (8 kHz), and so the most mixer frames a block can make: six to each of the file's.
#define MIN_FILE_RATE 8000
#define BLOCK_OUT_FRAMES (DECODE_FRAMES * 6 + 8)

typedef enum
{
    TRACK_FILE_NONE,
    TRACK_FILE_OGG,
    TRACK_FILE_MP3,
} track_file_t;

// What the folder listing found: each file's type, its name in the folder, and the song it's for (the lump's name
// without MUS_, lower case).
static track_file_t track_files[MAX_FILES];
static char *track_names[MAX_FILES];
static char track_songs[MAX_FILES][9];
static int file_count = 0;
static boolean listed = false;
static boolean started = false;

// The request the game makes (I_MusicFilesPlay) and the decoder thread takes, under request_lock.
static SDL_Mutex *request_lock;
static SDL_Condition *request_wake;
static int request_track;
static boolean request_loop;
static volatile unsigned int request_number;

// What the mixer reads, under ring_lock: decoded 48 kHz stereo, between ring_read and ring_write (counting frames,
// not wrapping; the ring's place is the count modulo RING_FRAMES).
static SDL_Mutex *ring_lock;
static int16_t ring[RING_FRAMES * 2];
static uint32_t ring_read;
static uint32_t ring_write;
// Whether the track is playing: false once one not looping is done and the mixer has had all of it, or stopped.
static volatile boolean playing = false;
// The game's pause (I_MusicFilesPause): the mixer takes nothing meanwhile, and the track waits where it is.
static volatile boolean paused = false;
// The music volume as a gain out of 256 for the mixer.
static volatile int gain = 192;

// The mixer's side, on SDL's audio thread: adds what's decoded into the mix, silence if there isn't enough yet.
static void FileMusic_Generate(int32_t *mix, int frames)
{
    if (paused)
    {
        return;
    }

    const int volume = gain;

    SDL_LockMutex(ring_lock);

    uint32_t available = ring_write - ring_read;
    const int count = (int) (available < (uint32_t) frames ? available : (uint32_t) frames);

    for (int f = 0; f < count; f++)
    {
        const int16_t *const frame = &ring[((ring_read + (uint32_t) f) % RING_FRAMES) * 2];

        mix[f * 2] += (frame[0] * volume) >> 8;
        mix[f * 2 + 1] += (frame[1] * volume) >> 8;
    }

    ring_read += (uint32_t) count;

    SDL_UnlockMutex(ring_lock);

    if (count > 0)
    {
        // The decoder may be waiting for room.
        SDL_SignalCondition(request_wake);
    }
}

// The decoder thread's side.

// The track's file, read whole, and its decoder.
static void *file_data = NULL;
static music_decoder_t *decoder = NULL;
static int playing_track = -1;
static boolean looping = false;
static volatile boolean decoder_done = false;
// The request decoder_done is about: a file the game has just asked for isn't over because the last one was.
static volatile unsigned int done_request = 0;

// From the file's rate to the mixer's, a straight line between each two of the file's frames: the step from one
// output frame to the next in the file's frames (16.16), where the next output frame is between in[0] and in[1], and
// in[0] the last frame of the block before.
static uint32_t resample_step;
static uint32_t resample_position;
static int16_t in_frames[(DECODE_FRAMES + 1) * 2];
static int16_t out_frames[BLOCK_OUT_FRAMES * 2];
static boolean have_previous;

static void CloseTrack(void)
{
    MusicFile_CloseDecoder(decoder);
    decoder = NULL;
    free(file_data);
    file_data = NULL;
    playing_track = -1;
}

// Reads the track's file whole and opens its decoder; false if it can't.
static boolean OpenTrack(int track)
{
    char path[300];
    FILE *file;
    long length;
    const Uint64 started_at = SDL_GetTicks();

    M_snprintf(path, sizeof(path), MUSIC_FOLDER "/%s", track_names[track]);

    file = fopen(path, "rb");
    if (file == NULL)
    {
        printf("musicfiles: can't open %s\n", path);
        return false;
    }

    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length <= 0 || (file_data = malloc((size_t) length)) == NULL)
    {
        printf("musicfiles: can't read %s (%ld bytes)\n", path, length);
        fclose(file);
        return false;
    }

    const size_t got = fread(file_data, 1, (size_t) length, file);
    fclose(file);

    if (got != (size_t) length)
    {
        printf("musicfiles: read %u of %s's %ld bytes\n", (unsigned) got, path, length);
        free(file_data);
        file_data = NULL;
        return false;
    }

    decoder = MusicFile_OpenDecoder(file_data, (size_t) length, track_files[track] == TRACK_FILE_MP3);
    if (decoder == NULL)
    {
        printf("musicfiles: %s isn't an %s file the game can play\n", path,
               track_files[track] == TRACK_FILE_MP3 ? "MP3" : "OGG Vorbis");
        free(file_data);
        file_data = NULL;
        return false;
    }

    if (MusicFile_Rate(decoder) < MIN_FILE_RATE || MusicFile_Rate(decoder) > 192000)
    {
        printf("musicfiles: %s is at %i Hz, which the game can't play\n", path, MusicFile_Rate(decoder));
        CloseTrack();
        return false;
    }

    resample_step = (uint32_t) (((uint64_t) MusicFile_Rate(decoder) << 16) / (uint64_t) I_SoundMixRate());
    resample_position = 0;
    have_previous = false;
    playing_track = track;

    printf("musicfiles: %s, %i Hz, %s, %ld KB, ready in %u ms\n", path, MusicFile_Rate(decoder),
           MusicFile_Channels(decoder) == 1 ? "mono" : "stereo", length / 1024,
           (unsigned) (SDL_GetTicks() - started_at));
    return true;
}

// Decodes a block and puts it in the ring at the mixer's rate; false when there's no more (a track not looping, at
// its end). Called with room in the ring for what a block can make. A block decoded as the game asks for another
// track is dropped (request taken is no longer the newest), so not a moment of the old one is heard.
static boolean DecodeBlock(unsigned int taken)
{
    int16_t *const out = out_frames;
    int got = MusicFile_Decode(decoder, in_frames + 2, DECODE_FRAMES);

    if (got <= 0 && looping)
    {
        // The end: from the start again, the line from the last frame to the first unbroken.
        if (MusicFile_Rewind(decoder))
        {
            got = MusicFile_Decode(decoder, in_frames + 2, DECODE_FRAMES);
        }
    }

    if (got <= 0)
    {
        return false;
    }

    if (!have_previous)
    {
        in_frames[0] = in_frames[2];
        in_frames[1] = in_frames[3];
        have_previous = true;
    }

    // in_frames[0 .. got] (got + 1 frames, the first the block before's last): each output frame between two of them.
    const uint32_t end = (uint32_t) got << 16;
    int count = 0;

    while (resample_position < end && count < BLOCK_OUT_FRAMES)
    {
        const uint32_t index = resample_position >> 16;
        const int32_t frac = (int32_t) (resample_position & 0xffff);
        const int16_t *const a = &in_frames[index * 2];
        const int16_t *const b = a + 2;

        out[count * 2] = (int16_t) (a[0] + (((b[0] - a[0]) * frac) >> 16));
        out[count * 2 + 1] = (int16_t) (a[1] + (((b[1] - a[1]) * frac) >> 16));
        count++;
        resample_position += resample_step;
    }

    resample_position -= end;
    in_frames[0] = in_frames[got * 2];
    in_frames[1] = in_frames[got * 2 + 1];

    SDL_LockMutex(ring_lock);
    if (taken != request_number)
    {
        count = 0;
    }
    for (int f = 0; f < count; f++)
    {
        int16_t *const frame = &ring[((ring_write + (uint32_t) f) % RING_FRAMES) * 2];

        frame[0] = out[f * 2];
        frame[1] = out[f * 2 + 1];
    }
    ring_write += (uint32_t) count;
    SDL_UnlockMutex(ring_lock);

    return true;
}

static uint32_t RingFree(void)
{
    SDL_LockMutex(ring_lock);
    const uint32_t used = ring_write - ring_read;
    SDL_UnlockMutex(ring_lock);
    return RING_FRAMES - used;
}

static void FlushRing(void)
{
    SDL_LockMutex(ring_lock);
    ring_read = ring_write;
    SDL_UnlockMutex(ring_lock);
}

static int SDLCALL DecoderThread(void *unused)
{
    unsigned int taken = 0;
    // What a block can make at most at the mixer's rate, from a file of any rate down to 8 kHz.
    const uint32_t block_room = BLOCK_OUT_FRAMES;

    (void) unused;

    // Off the game's hardware thread, onto another core's: the thread starts on its creator's (xsdk/thread.h).
    {
        const uint32_t here = xsdk_thread_hardware_thread();
        const uint32_t there = (here + 2) % XSDK_HARDWARE_THREADS;

        if (NT_SUCCESS(xsdk_thread_move(there)))
        {
            printf("musicfiles: decoding on hardware thread %u\n", (unsigned) there);
        }
    }

    for (;;)
    {
        int track;
        boolean loop;
        boolean new_request;

        SDL_LockMutex(request_lock);
        for (;;)
        {
            new_request = request_number != taken;

            if (new_request || (decoder != NULL && !decoder_done && RingFree() >= block_room))
            {
                break;
            }

            // Waits for a request, or the mixer making room; looks again now and then in any case.
            SDL_WaitConditionTimeout(request_wake, request_lock, 50);
        }
        track = request_track;
        loop = request_loop;
        taken = request_number;
        SDL_UnlockMutex(request_lock);

        if (new_request)
        {
            CloseTrack();
            FlushRing();
            decoder_done = false;
            looping = loop;

            if (track >= 0 && OpenTrack(track))
            {
                playing = true;
            }
            else
            {
                if (track >= 0)
                {
                    // A file the game can't play is taken as no file from now on: the sound code sees the song stop,
                    // and plays it as AdLib (heretic/s_sound.c, S_UpdateSounds).
                    track_files[track] = TRACK_FILE_NONE;
                    printf("musicfiles: %s plays as AdLib instead\n", track_songs[track]);
                }
                playing = false;
            }
            continue;
        }

        if (!DecodeBlock(taken))
        {
            done_request = taken;
            decoder_done = true;
            printf("musicfiles: %s ended\n", track_names[playing_track]);
        }
    }

    return 0;
}

// The song a file name is for: "e1m1.ogg", "MUS_E1M1.ogg" -> "e1m1"; false if it isn't an OGG or MP3 with a short
// enough name.
static boolean SongOfFile(const char *name, char song[9], track_file_t *type)
{
    const char *dot = strrchr(name, '.');
    const char *start = name;
    size_t length;

    if (dot == NULL)
    {
        return false;
    }

    if (!strcasecmp(dot, ".ogg"))
    {
        *type = TRACK_FILE_OGG;
    }
    else if (!strcasecmp(dot, ".mp3"))
    {
        *type = TRACK_FILE_MP3;
    }
    else
    {
        return false;
    }

    if (!strncasecmp(start, "mus_", 4))
    {
        start += 4;
    }

    length = (size_t) (dot - start);
    if (length == 0 || length > 8)
    {
        return false;
    }

    for (size_t i = 0; i < length; i++)
    {
        song[i] = (char) tolower((unsigned char) start[i]);
    }
    song[length] = '\0';
    return true;
}

// Lists the music folder: a file for each song, OGG where a song has both.
static void ListFiles(void)
{
    DIR *dir = opendir(MUSIC_FOLDER);
    struct dirent *entry;

    listed = true;

    if (dir == NULL)
    {
        printf("musicfiles: no %s folder beside the game, so the AdLib music plays\n", MUSIC_FOLDER);
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char song[9];
        track_file_t type;
        int i;

        if (!SongOfFile(entry->d_name, song, &type))
        {
            continue;
        }

        for (i = 0; i < file_count; i++)
        {
            if (!strcmp(track_songs[i], song))
            {
                break;
            }
        }

        if (i < file_count)
        {
            // OGG over MP3, for its clean loop.
            if (track_files[i] == TRACK_FILE_OGG || type == TRACK_FILE_MP3)
            {
                continue;
            }
        }
        else if (file_count == MAX_FILES)
        {
            continue;
        }
        else
        {
            file_count++;
        }

        track_files[i] = type;
        free(track_names[i]);
        track_names[i] = M_StringDuplicate(entry->d_name);
        M_StringCopy(track_songs[i], song, sizeof(track_songs[i]));
    }

    closedir(dir);

    if (file_count == 0)
    {
        printf("musicfiles: no song files (e1m1.ogg and so on, or .mp3) in %s, so the AdLib music plays\n",
               MUSIC_FOLDER);
    }
    else
    {
        printf("musicfiles: %i song files in %s\n", file_count, MUSIC_FOLDER);
    }
}

boolean I_MusicFilesInit(void)
{
    if (!listed)
    {
        ListFiles();
    }

    if (started)
    {
        return true;
    }

    if (I_SoundMixRate() <= 0)
    {
        printf("musicfiles: no sound, so no music files\n");
        return false;
    }

    request_lock = SDL_CreateMutex();
    request_wake = SDL_CreateCondition();
    ring_lock = SDL_CreateMutex();

    if (request_lock == NULL || request_wake == NULL || ring_lock == NULL
        || SDL_CreateThread(DecoderThread, "heretic360 music files", NULL) == NULL)
    {
        printf("musicfiles: can't start: %s\n", SDL_GetError());
        return false;
    }

    if (!I_SetFileMusicGenerator(FileMusic_Generate))
    {
        printf("musicfiles: no mixer\n");
        return false;
    }

    started = true;
    return true;
}

int I_MusicFilesFind(const char *lumpname)
{
    char song[9];
    track_file_t type;
    char name[16];

    if (!started || lumpname == NULL)
    {
        return -1;
    }

    M_snprintf(name, sizeof(name), "%s.ogg", lumpname);
    if (!SongOfFile(name, song, &type))
    {
        return -1;
    }

    for (int i = 0; i < file_count; i++)
    {
        if (track_files[i] != TRACK_FILE_NONE && !strcmp(track_songs[i], song))
        {
            return i;
        }
    }

    return -1;
}

boolean I_MusicFilesPlay(int file, boolean loop)
{
    if (!started)
    {
        return file < 0;
    }

    if (file >= file_count || (file >= 0 && track_files[file] == TRACK_FILE_NONE))
    {
        return false;
    }

    SDL_LockMutex(request_lock);
    request_track = file < 0 ? -1 : file;
    request_loop = loop;
    request_number++;
    // What's playing stops now, not when the decoder gets to the request.
    playing = file >= 0;
    paused = false;
    SDL_SignalCondition(request_wake);
    SDL_UnlockMutex(request_lock);

    FlushRing();
    return true;
}

boolean I_MusicFilesPlaying(void)
{
    if (!started || !playing)
    {
        return false;
    }

    if (decoder_done && done_request == request_number)
    {
        // A file not looping is over once the mixer has had all of it.
        SDL_LockMutex(ring_lock);
        const boolean left = ring_write != ring_read;
        SDL_UnlockMutex(ring_lock);
        if (!left)
        {
            playing = false;
        }
    }

    return playing;
}

void I_MusicFilesPause(boolean pause)
{
    paused = pause;
}

// volume is 0 to 15. Recordings are mastered loud, so the loudest gives three quarters of their level, to sit beside
// the sound effects as the AdLib music does (Hexen 360's CD music, the same).
void I_MusicFilesSetVolume(int volume)
{
    if (volume < 0)
    {
        volume = 0;
    }
    if (volume > 15)
    {
        volume = 15;
    }

    gain = volume * 192 / 15;
}
