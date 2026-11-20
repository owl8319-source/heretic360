// Heretic 360: the part of Chocolate Doom's textscreen/txt_main.h that heretic/d_main.c uses, without SDL. The
// text-mode startup screen isn't shown on the console (stubs.c).

#ifndef TXT_MAIN_H
#define TXT_MAIN_H

typedef enum
{
    TXT_COLOR_BLACK,
    TXT_COLOR_BLUE,
    TXT_COLOR_GREEN,
    TXT_COLOR_CYAN,
    TXT_COLOR_RED,
    TXT_COLOR_MAGENTA,
    TXT_COLOR_BROWN,
    TXT_COLOR_GREY,
    TXT_COLOR_DARK_GREY,
    TXT_COLOR_BRIGHT_BLUE,
    TXT_COLOR_BRIGHT_GREEN,
    TXT_COLOR_BRIGHT_CYAN,
    TXT_COLOR_BRIGHT_RED,
    TXT_COLOR_BRIGHT_MAGENTA,
    TXT_COLOR_YELLOW,
    TXT_COLOR_BRIGHT_WHITE,
} txt_color_t;

int TXT_Init(void);
void TXT_Shutdown(void);
unsigned char *TXT_GetScreenData(void);
void TXT_UpdateScreen(void);
int TXT_GetChar(void);

#endif
