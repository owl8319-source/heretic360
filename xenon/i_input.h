// Heretic 360: Chocolate Doom's i_input.h without SDL. The Xbox 360 has no keyboard or mouse in this port; the
// controller goes through i_joystick.c.

#ifndef __I_INPUT__
#define __I_INPUT__

#include "doomtype.h"

#define MAX_MOUSE_BUTTONS 8

extern float mouse_acceleration;
extern int mouse_threshold;

void I_BindInputVariables(void);
void I_ReadMouse(void);

// Text input (save game names) has no keyboard to come from; these do nothing.
void I_StartTextInput(int x1, int y1, int x2, int y2);
void I_StopTextInput(void);

#endif
