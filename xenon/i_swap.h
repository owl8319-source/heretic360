// Heretic 360: Chocolate Doom's i_swap.h, on SDL3's byte swapping. The Xbox 360's CPU is big-endian and WAD files are
// little-endian, so SHORT and LONG swap every value read from the WAD.

#ifndef __I_SWAP__
#define __I_SWAP__

#include <SDL3/SDL_endian.h>

// Deliberately cast to signed values; this is the behaviour of the macros in the original source and some code
// relies on it.
#define SHORT(x)  ((signed short) SDL_Swap16LE(x))
#define LONG(x)   ((signed int) SDL_Swap32LE(x))

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define SYS_BIG_ENDIAN
#endif

#endif
