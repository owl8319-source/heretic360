// Heretic 360: Crispy Doom's i_timer.c on SDL3's timer, which SDL-xenon takes from xsdk's POSIX clocks.

#include <SDL3/SDL.h>

#include "i_timer.h"
#include "doomtype.h"

static Uint64 basetime = 0;

static Uint64 TicksSinceStart(void)
{
    Uint64 ticks = SDL_GetTicks();

    if (basetime == 0)
    {
        basetime = ticks;
    }

    return ticks - basetime;
}

// Time in 1/35th second tics.

int I_GetTime(void)
{
    return (int) ((TicksSinceStart() * TICRATE) / 1000);
}

// The same in milliseconds.

int I_GetTimeMS(void)
{
    return (int) TicksSinceStart();
}

// [crispy] The same in microseconds, for the frame-rate limiter.

uint64_t I_GetTimeUS(void)
{
    static Uint64 basens = 0;
    Uint64 ns = SDL_GetTicksNS();

    if (basens == 0)
    {
        basens = ns;
    }

    return (uint64_t) ((ns - basens) / 1000);
}

// [crispy] How far into the current tic we are, for drawing between tics.

fixed_t I_GetFracRealTime(void)
{
    return (int64_t) I_GetTimeMS() * TICRATE % 1000 * FRACUNIT / 1000;
}

// Sleeps for about ms milliseconds; on the console a sleep can run up to a millisecond long.

void I_Sleep(int ms)
{
    SDL_Delay((Uint32) ms);
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
    // SDL3 needs no subsystem for its timer.
}
