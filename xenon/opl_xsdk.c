// Heretic 360: the OPL (AdLib) driver for Chocolate Doom's music, in place of opl/opl_sdl.c, which needs SDL2_mixer.
//
// Chocolate Doom's i_oplmusic.c plays Heretic's MUS music by writing an OPL chip's registers, on timer callbacks it
// schedules with OPL_SetCallback. Here the chip is Nuked OPL3 (opl/opl3.c), and it runs inside the sound mixer
// (xenon/i_sound.c): each time SDL's audio thread asks for samples, OPL_XSDK_Generate fills them from the chip, and
// runs the callbacks whose time has come on the way. This is opl_sdl.c's own scheme, its music time counted in the
// samples the mixer has played.

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "opl3.h"

#include "opl.h"
#include "opl_internal.h"
#include "opl_queue.h"

// xenon/i_sound.c's mixer.
int I_SoundMixRate(void);
int I_SetMusicGenerator(void (*generator)(int32_t *mix, int frames));

typedef struct
{
    unsigned int rate;        // Number of times the timer is advanced per sec.
    unsigned int enabled;     // Non-zero if timer is enabled.
    unsigned int value;       // Last value that was set.
    uint64_t expire_time;     // Calculated time that timer will expire.
} opl_timer_t;

// The longest stretch generated at a time, in sample frames; the mixer asks in pieces smaller than this.
#define MAX_GENERATE_FRAMES 1024

// When the callback mutex is locked using OPL_Lock, callback functions are not invoked.
static SDL_Mutex *callback_mutex = NULL;

// Queue of callbacks waiting to be invoked, and the mutex over it.
static opl_callback_queue_t *callback_queue;
static SDL_Mutex *callback_queue_mutex = NULL;

// Current time, in us since startup.
static uint64_t current_time;

// If non-zero, playback is currently paused, and the time callbacks have been paused for.
static int opl_xsdk_paused;
static uint64_t pause_offset;

// OPL software emulator structure.
static opl3_chip opl_chip;
static int opl_opl3mode;

static int mixing_freq;
static int driver_running = 0;

// Register number that was written.
static int register_num = 0;

// Timers; the emulator does not do timer stuff itself.
static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };

// Advance time by the specified number of samples, invoking any callback functions as appropriate.
static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t us;

    SDL_LockMutex(callback_queue_mutex);

    us = ((uint64_t) nsamples * OPL_SECOND) / mixing_freq;
    current_time += us;

    if (opl_xsdk_paused)
    {
        pause_offset += us;
    }

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        // The callback runs holding callback_mutex (so OPL_Lock keeps callbacks off) and not callback_queue_mutex
        // (so it can schedule the next with OPL_SetCallback).
        SDL_UnlockMutex(callback_queue_mutex);

        SDL_LockMutex(callback_mutex);
        callback(callback_data);
        SDL_UnlockMutex(callback_mutex);

        SDL_LockMutex(callback_queue_mutex);
    }

    SDL_UnlockMutex(callback_queue_mutex);
}

// Adds the chip's next nsamples stereo frames into the mix.
static void AddChipOutput(int32_t *mix, unsigned int nsamples)
{
    static Bit16s chip_buffer[MAX_GENERATE_FRAMES * 2];

    OPL3_GenerateStream(&opl_chip, chip_buffer, nsamples);

    for (unsigned int i = 0; i < nsamples * 2; i++)
    {
        mix[i] += chip_buffer[i];
    }
}

// The mixer's music generator, on SDL's audio thread: frames stereo frames added into mix.
static void OPL_XSDK_Generate(int32_t *mix, int frames)
{
    unsigned int filled = 0;
    const unsigned int buffer_samples = (unsigned int) frames;

    while (filled < buffer_samples)
    {
        uint64_t next_callback_time;
        uint64_t nsamples;

        SDL_LockMutex(callback_queue_mutex);

        // Generate up to the next callback's time, so it changes the chip where it should.
        if (opl_xsdk_paused || OPL_Queue_IsEmpty(callback_queue))
        {
            nsamples = buffer_samples - filled;
        }
        else
        {
            next_callback_time = OPL_Queue_Peek(callback_queue) + pause_offset;

            nsamples = (next_callback_time - current_time) * mixing_freq;
            nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;

            if (nsamples > buffer_samples - filled)
            {
                nsamples = buffer_samples - filled;
            }
        }

        SDL_UnlockMutex(callback_queue_mutex);

        if (nsamples > MAX_GENERATE_FRAMES)
        {
            nsamples = MAX_GENERATE_FRAMES;
        }

        AddChipOutput(mix + filled * 2, (unsigned int) nsamples);
        filled += (unsigned int) nsamples;

        AdvanceTime((unsigned int) nsamples);
    }
}

static void OPL_XSDK_Shutdown(void)
{
    if (driver_running)
    {
        // Off the mixer first, so the audio thread no longer touches what's freed below.
        I_SetMusicGenerator(NULL);
        driver_running = 0;
    }

    if (callback_queue != NULL)
    {
        OPL_Queue_Destroy(callback_queue);
        callback_queue = NULL;
    }

    if (callback_mutex != NULL)
    {
        SDL_DestroyMutex(callback_mutex);
        callback_mutex = NULL;
    }

    if (callback_queue_mutex != NULL)
    {
        SDL_DestroyMutex(callback_queue_mutex);
        callback_queue_mutex = NULL;
    }
}

static int OPL_XSDK_Init(unsigned int port_base)
{
    (void) port_base;

    // OPL_Init's detection waits on callbacks the mixer runs, so without a running mixer it would wait forever.
    mixing_freq = I_SoundMixRate();
    if (mixing_freq <= 0)
    {
        printf("OPL_XSDK_Init: the sound mixer isn't running, so no music\n");
        return 0;
    }

    opl_xsdk_paused = 0;
    pause_offset = 0;
    current_time = 0;

    callback_queue = OPL_Queue_Create();
    callback_mutex = SDL_CreateMutex();
    callback_queue_mutex = SDL_CreateMutex();

    if (callback_queue == NULL || callback_mutex == NULL || callback_queue_mutex == NULL)
    {
        printf("OPL_XSDK_Init: couldn't create the callback queue: %s\n", SDL_GetError());
        OPL_XSDK_Shutdown();
        return 0;
    }

    OPL3_Reset(&opl_chip, (Bit32u) mixing_freq);
    opl_opl3mode = 0;

    if (!I_SetMusicGenerator(OPL_XSDK_Generate))
    {
        OPL_XSDK_Shutdown();
        return 0;
    }

    driver_running = 1;
    printf("OPL_XSDK_Init: emulated OPL3 at %i Hz\n", mixing_freq);

    return 1;
}

static unsigned int OPL_XSDK_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x40;   // Timer 1 has expired
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x20;   // Timer 2 has expired
    }

    return result;
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer2.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }

            break;

        case OPL_REG_NEW:
            opl_opl3mode = value & 0x01;
            // Falls through: the chip takes it too.

        default:
            OPL3_WriteRegBuffered(&opl_chip, (Bit16u) reg_num, (Bit8u) value);
            break;
    }
}

static void OPL_XSDK_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_REGISTER_PORT_OPL3)
    {
        register_num = value | 0x100;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_XSDK_SetCallback(uint64_t us, opl_callback_t callback, void *data)
{
    SDL_LockMutex(callback_queue_mutex);
    OPL_Queue_Push(callback_queue, callback, data, current_time - pause_offset + us);
    SDL_UnlockMutex(callback_queue_mutex);
}

static void OPL_XSDK_ClearCallbacks(void)
{
    SDL_LockMutex(callback_queue_mutex);
    OPL_Queue_Clear(callback_queue);
    SDL_UnlockMutex(callback_queue_mutex);
}

static void OPL_XSDK_Lock(void)
{
    SDL_LockMutex(callback_mutex);
}

static void OPL_XSDK_Unlock(void)
{
    SDL_UnlockMutex(callback_mutex);
}

static void OPL_XSDK_SetPaused(int paused)
{
    opl_xsdk_paused = paused;
}

static void OPL_XSDK_AdjustCallbacks(float factor)
{
    SDL_LockMutex(callback_queue_mutex);
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
    SDL_UnlockMutex(callback_queue_mutex);
}

opl_driver_t opl_xsdk_driver =
{
    "xsdk",
    OPL_XSDK_Init,
    OPL_XSDK_Shutdown,
    OPL_XSDK_PortRead,
    OPL_XSDK_PortWrite,
    OPL_XSDK_SetCallback,
    OPL_XSDK_ClearCallbacks,
    OPL_XSDK_Lock,
    OPL_XSDK_Unlock,
    OPL_XSDK_SetPaused,
    OPL_XSDK_AdjustCallbacks,
};
