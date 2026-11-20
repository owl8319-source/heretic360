// Heretic 360: Chocolate Doom's i_timer.c on SDL3's timer, which SDL-xenon takes from xsdk's POSIX clocks.

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
