// Heretic 360: Chocolate Doom's i_joystick.h without SDL. i_joystick.c reads the Xbox 360 controller through SDL3
// and posts ev_joystick events in the form Chocolate Doom's game code expects.

#ifndef __I_JOYSTICK__
#define __I_JOYSTICK__

// Virtual buttons the game code knows by number (joyb* variables in m_controls.c).
#define NUM_VIRTUAL_BUTTONS 17
#define MAX_VIRTUAL_BUTTONS 20

#define TRIGGER_THRESHOLD (30 * 32767 / 255)

// The virtual button held while LB, RB and Back are all held: the developer cheat menu's combination
// (src/heretic/devmenu.c). Past the buttons the joyb* variables name.
#define JOY_DEV_COMBO_BUTTON 12

// Directional input in an ev_joystick's data6, as the menus read it.
#define DPAD_SHIFT 0
#define LSTICK_SHIFT 4
#define RSTICK_SHIFT 8

#define JOY_GET_DPAD(x) (((x) >> DPAD_SHIFT) & 0xf)
#define JOY_GET_LSTICK(x) (((x) >> LSTICK_SHIFT) & 0xf)
#define JOY_GET_RSTICK(x) (((x) >> RSTICK_SHIFT) & 0xf)

enum
{
    JOY_DIR_NONE = 0x0,
    JOY_DIR_UP = 0x1,
    JOY_DIR_DOWN = 0x2,
    JOY_DIR_LEFT = 0x4,
    JOY_DIR_RIGHT = 0x8
};

extern int use_analog;
extern int joystick_turn_sensitivity;
extern int joystick_move_sensitivity;
extern int joystick_look_sensitivity;

void I_InitJoystick(void);
void I_ShutdownJoystick(void);
void I_UpdateJoystick(void);

void I_BindJoystickVariables(void);

#endif
