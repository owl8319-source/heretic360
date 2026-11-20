// Heretic 360: Chocolate Doom's i_main.c for xsdk. xsdk opens the log (heretic.log beside the .xex) before main, and
// stdout and stderr go to it, so Chocolate Doom's startup messages land there too.
//
// The log writes to the hard drive as it's given lines, from the thread that gives them, and on the console a write
// can wait seconds for an idle drive to wake (xsdk docs/HARDWARE.md, "The log's file grows a unit at a time"). So
// stdout is buffered here, and a writer thread takes what's in it to the log once a second: the game's thread only
// copies its lines into memory. stderr stays unbuffered, so an error's message is in the log before the game ends;
// exit, which I_Quit and I_Error end with, writes out what's left of stdout.

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"

void D_DoomMain(void);

// How much the game can log between the writer's visits before a line has to go to the drive itself.
#define LOG_BUFFER_BYTES (64 * 1024)
#define LOG_WRITE_INTERVAL_MS 1000

static char log_buffer[LOG_BUFFER_BYTES];

static int SDLCALL LogWriter(void *data)
{
    (void) data;

    for (;;)
    {
        SDL_Delay(LOG_WRITE_INTERVAL_MS);
        fflush(stdout);
    }

    return 0;
}

static void StartLogWriter(void)
{
    SDL_Thread *thread;

    setvbuf(stdout, log_buffer, _IOFBF, sizeof(log_buffer));
    setvbuf(stderr, NULL, _IONBF, 0);

    thread = SDL_CreateThread(LogWriter, "heretic360 log", NULL);

    if (thread == NULL)
    {
        // Without the writer, lines go to the log as they're finished.
        setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
        printf("heretic360: no log writer thread (%s), so lines are logged as they come\n", SDL_GetError());
        return;
    }

    SDL_DetachThread(thread);
}

int main(int argc, char **argv)
{
    // The loader passes the .xex's name alone. Heretic looks for heretic.wad in the current directory, which xsdk
    // sets to game:\, the folder the .xex was launched from.
    myargc = argc;
    myargv = malloc(argc * sizeof(char *));
    assert(myargv != NULL);

    for (int i = 0; i < argc; i++)
    {
        myargv[i] = M_StringDuplicate(argv[i]);
    }

    StartLogWriter();
    printf("heretic360: starting " PACKAGE_STRING "\n");

    M_FindResponseFile();
    M_SetExeDir();

    D_DoomMain();

    return 0;
}
