// Heretic 360: Chocolate Doom's i_main.c for xsdk. xsdk opens the log (heretic.log beside the .xex) before main, and
// stdout and stderr go to it, so Chocolate Doom's startup messages land there too.

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "xsdk/log.h"

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"

void D_DoomMain(void);

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

    xsdk_log_write("heretic360: starting " PACKAGE_STRING);

    // Each line goes to the log as it's finished, so a crash leaves the startup messages before it.
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    setvbuf(stderr, NULL, _IONBF, 0);

    M_FindResponseFile();
    M_SetExeDir();

    D_DoomMain();

    return 0;
}
