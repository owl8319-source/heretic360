// Heretic 360: the console's own on-screen keyboard, for naming saves, through xsdk's XAM calls (xsdk/xam.h). The
// keyboard is started in one call and asked once a tic whether it's done, as xsdk's docs/GUIDE.md asks ("Poll once a
// frame, with a timeout of 0"): XAM's dialogs only show while the title keeps presenting frames.

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#include "xsdk/xam.h"

#include "i_xam.h"

#define KEYBOARD_CHARS 32

static const uint16_t keyboard_title[] = u"Save Game";
static const uint16_t keyboard_description[] = u"Name this save.";

static XAM_OVERLAPPED overlapped;
static uint16_t default_wide[KEYBOARD_CHARS + 1];
static uint16_t typed_wide[KEYBOARD_CHARS + 1];
static boolean pending = false;

// The signed-in user's controller, or the first controller with nobody signed in.
static uint32_t KeyboardUser(void)
{
    uint32_t user;
    char gamertag[XSDK_XAM_GAMERTAG_BYTES];

    if (xsdk_xam_signed_in_user(&user, gamertag) != ERROR_SUCCESS || user == XSDK_XAM_USER_NONE)
    {
        return 0;
    }

    return user;
}

boolean I_StartSaveNameKeyboard(const char *default_text, int max_chars)
{
    int i;

    if (pending)
    {
        return false;
    }

    if (max_chars > KEYBOARD_CHARS)
    {
        max_chars = KEYBOARD_CHARS;
    }

    for (i = 0; i < max_chars && default_text[i] != '\0'; i++)
    {
        default_wide[i] = (unsigned char) default_text[i];
    }
    default_wide[i] = 0;
    typed_wide[0] = 0;

    xsdk_xam_prepare(&overlapped);
    const uint32_t returned = xsdk_xam_returned(
        &overlapped, xsdk_xam_keyboard(KeyboardUser(), keyboard_title, keyboard_description, default_wide, typed_wide,
                                       (uint32_t) max_chars + 1, &overlapped));

    if (returned != ERROR_IO_PENDING)
    {
        printf("heretic360: the keyboard didn't open: 0x%08X\n", (unsigned) returned);
        return false;
    }

    pending = true;
    return true;
}

int I_PollSaveNameKeyboard(char *text, int text_bytes)
{
    int i;

    if (!pending)
    {
        return -1;
    }

    const uint32_t result = xsdk_xam_wait(&overlapped, 0, NULL);

    if (result == ERROR_IO_INCOMPLETE)
    {
        return 0;
    }

    pending = false;

    if (result != ERROR_SUCCESS)
    {
        printf("heretic360: the keyboard closed with 0x%08X\n", (unsigned) result);
        return -1;
    }

    for (i = 0; i < text_bytes - 1 && typed_wide[i] != 0; i++)
    {
        const uint16_t c = typed_wide[i];

        text[i] = (c >= ' ' && c < 0x7f) ? (char) toupper((int) c) : ' ';
    }
    text[i] = '\0';

    return 1;
}
