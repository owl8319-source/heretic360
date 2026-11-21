// Heretic 360: the console's own on-screen keyboard, for naming saves (heretic/mn_menu.c), through xsdk's XAM calls.

#ifndef __I_XAM__
#define __I_XAM__

#include "doomtype.h"

// Opens the keyboard with default_text in it, for up to max_chars characters. False if it can't open: the console's
// UI is already up, or the call failed; the menu then names the save as it would on a PC.
boolean I_StartSaveNameKeyboard(const char *default_text, int max_chars);

// Once a tic while the keyboard is open: 0 while it's up, 1 once a name was accepted (in text, up to text_bytes with
// its zero, upper case ASCII; characters outside that become spaces), -1 once it was cancelled or failed.
int I_PollSaveNameKeyboard(char *text, int text_bytes);

#endif
