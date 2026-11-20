// Heretic 360: Crispy Doom's i_input.c without a keyboard or mouse. The controller is read in i_joystick.c; these
// are the variables the configuration file keeps, and the calls the game code makes.

#include "doomtype.h"
#include "i_input.h"
#include "m_config.h"

float mouse_acceleration = 2.0;
int mouse_threshold = 10;
int vanilla_keyboard_mapping = 1;
int novert = 1; // [crispy]
float mouse_acceleration_y = 1.0;
int mouse_threshold_y = 0;
int mouse_y_invert = 0;
int runcentering = 1; // [crispy]

void I_ReadMouse(void)
{
}

void I_ReadMouseUncapped(void)
{
}

double I_AccelerateMouse(int val)
{
    return val;
}

double I_AccelerateMouseY(int val)
{
    return val;
}

void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    (void) x1;
    (void) y1;
    (void) x2;
    (void) y2;
}

void I_StopTextInput(void)
{
}

void I_BindInputVariables(void)
{
    M_BindFloatVariable("mouse_acceleration",      &mouse_acceleration);
    M_BindIntVariable("mouse_threshold",           &mouse_threshold);
    M_BindIntVariable("vanilla_keyboard_mapping",  &vanilla_keyboard_mapping);
    M_BindIntVariable("novert",                    &novert);
    M_BindFloatVariable("mouse_acceleration_y",    &mouse_acceleration_y);
    M_BindIntVariable("mouse_threshold_y",         &mouse_threshold_y);
    M_BindIntVariable("mouse_y_invert",            &mouse_y_invert);
}

void I_BindStrifeInputVariables(void)
{
    M_BindIntVariable("runcentering",              &runcentering);
}
