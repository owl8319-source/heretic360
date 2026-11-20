// Heretic 360: the part of Chocolate Doom's textscreen/txt_io.h that heretic/d_main.c uses (stubs.c).

#ifndef TXT_IO_H
#define TXT_IO_H

#include "txt_main.h"

void TXT_PutChar(int c);
void TXT_Puts(const char *s);
void TXT_GotoXY(int x, int y);
void TXT_FGColor(txt_color_t color);
void TXT_BGColor(int color, int blinking);

#endif
