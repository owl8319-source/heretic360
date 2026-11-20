// Heretic 360: the Xbox 360 controller, read through SDL3 (SDL-xenon reads it with XamInputGetState), posted to the
// game as Crispy Doom's ev_joystick events with a fixed layout:
//
//   In the game                          In the menus
//   Left stick    move and strafe        Left stick, D-pad  choose
//   Right stick   turn and look          A                  select (yes, on the quit question)
//   RT            fire                   B                  back (no, on the quit question)
//   LT or A       use (doors, switches)  Start              open the menu
//   LB / RB       previous / next weapon LB / RB            previous / next page
//   D-pad left / right  inventory        The game always runs; push the stick part way to walk.
//   D-pad up or X  use the artifact
//   Y / B         fly up / down (Wings of Wrath)
//   Right stick click  centre the view and stop flying
//   Back          map
//   Start         menu
//
// LB + RB + Back, held about a second in a level, opens the developer cheat menu (src/heretic/devmenu.c), so Back opens
// no map while both bumpers are held. In that menu A selects and B closes, as in Heretic's.

#include <stdio.h>

#include <SDL3/SDL.h>

#include "d_event.h"
#include "doomtype.h"
#include "i_joystick.h"
#include "i_system.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_fixed.h"

// Heretic's menu state (heretic/mn_menu.c) and the developer cheat menu's (heretic/devmenu.c): A and B mean select
// and back there, use and fly down in the game.
extern boolean MenuActive;
extern boolean askforquit;
extern boolean DM_Active;

// The virtual buttons the joyb* variables name.
enum
{
    VB_FIRE,
    VB_USE,
    VB_PREV_WEAPON,
    VB_NEXT_WEAPON,
    VB_INV_LEFT,
    VB_INV_RIGHT,
    VB_USE_ARTIFACT,
    VB_FLY_UP,
    VB_FLY_DOWN,
    VB_FLY_CENTER,
    VB_MENU,
    VB_AUTOMAP,
};

// Heretic's g_game.c treats a speed button past its 20 buttons as always held: always run.
#define ALWAYS_RUN_BUTTON 29

// A stick within this share of its travel of the middle reads as the middle.
#define STICK_DEAD_ZONE (25 * 32768 / 100)
// Menus move once the stick is past half its travel.
#define DIRECTION_DEAD_ZONE (50 * 32768 / 100)

static SDL_Gamepad *gamepad = NULL;
static boolean joystick_initialized = false;

int use_analog = 1;
int joystick_turn_sensitivity = 10;
int joystick_move_sensitivity = 10;
int joystick_look_sensitivity = 10;

static void SetButtonLayout(void)
{
    joybfire = VB_FIRE;
    joybuse = VB_USE;
    joybstrafe = -1;
    joybspeed = ALWAYS_RUN_BUTTON;
    joybstrafeleft = -1;
    joybstraferight = -1;
    joybjump = -1;
    joybprevweapon = VB_PREV_WEAPON;
    joybnextweapon = VB_NEXT_WEAPON;
    joybmenu = VB_MENU;
    joybautomap = VB_AUTOMAP;
    joybuseartifact = VB_USE_ARTIFACT;
    joybinvleft = VB_INV_LEFT;
    joybinvright = VB_INV_RIGHT;
    joybflyup = VB_FLY_UP;
    joybflydown = VB_FLY_DOWN;
    joybflycenter = VB_FLY_CENTER;
}

static void OpenFirstGamepad(void)
{
    SDL_JoystickID *ids;
    int count = 0;

    ids = SDL_GetGamepads(&count);
    if (ids == NULL)
    {
        return;
    }

    if (count > 0)
    {
        gamepad = SDL_OpenGamepad(ids[0]);
        if (gamepad == NULL)
        {
            printf("I_InitJoystick: SDL_OpenGamepad failed: %s\n", SDL_GetError());
        }
        else
        {
            printf("I_InitJoystick: using \"%s\"\n", SDL_GetGamepadName(gamepad));
        }
    }

    SDL_free(ids);
}

void I_InitJoystick(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        printf("I_InitJoystick: SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return;
    }

    joystick_initialized = true;
    use_analog = 1;
    SetButtonLayout();
    OpenFirstGamepad();

    I_AtExit(I_ShutdownJoystick, true);
}

void I_ShutdownJoystick(void)
{
    if (gamepad != NULL)
    {
        SDL_CloseGamepad(gamepad);
        gamepad = NULL;
    }

    if (joystick_initialized)
    {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        joystick_initialized = false;
    }
}

static boolean Pressed(SDL_GamepadButton button)
{
    return SDL_GetGamepadButton(gamepad, button);
}

static boolean TriggerPressed(SDL_GamepadAxis trigger)
{
    return SDL_GetGamepadAxis(gamepad, trigger) > TRIGGER_THRESHOLD;
}

static int Axis(SDL_GamepadAxis axis)
{
    int value = SDL_GetGamepadAxis(gamepad, axis);

    if (value < STICK_DEAD_ZONE && value > -STICK_DEAD_ZONE)
    {
        return 0;
    }

    // Fixed point, -1 to 1.
    return value * (FRACUNIT / 32768);
}

static int Direction(int x, int y)
{
    int direction = JOY_DIR_NONE;

    if (y < -DIRECTION_DEAD_ZONE) direction |= JOY_DIR_UP;
    if (y > DIRECTION_DEAD_ZONE) direction |= JOY_DIR_DOWN;
    if (x < -DIRECTION_DEAD_ZONE) direction |= JOY_DIR_LEFT;
    if (x > DIRECTION_DEAD_ZONE) direction |= JOY_DIR_RIGHT;

    return direction;
}

static int DirectionalInput(void)
{
    int dpad = JOY_DIR_NONE;

    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_UP)) dpad |= JOY_DIR_UP;
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) dpad |= JOY_DIR_DOWN;
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) dpad |= JOY_DIR_LEFT;
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) dpad |= JOY_DIR_RIGHT;

    const int left = Direction(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX),
                               SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    const int right = Direction(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX),
                                SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));

    return (dpad << DPAD_SHIFT) | (left << LSTICK_SHIFT) | (right << RSTICK_SHIFT);
}

static int ButtonState(void)
{
    int buttons = 0;
    const boolean bumpers = Pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) && Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

#define SET(vb) (buttons |= 1 << (vb))

    if (Pressed(SDL_GAMEPAD_BUTTON_START)) SET(VB_MENU);
    if (bumpers && Pressed(SDL_GAMEPAD_BUTTON_BACK)) SET(JOY_DEV_COMBO_BUTTON);

    if (MenuActive || askforquit || DM_Active)
    {
        // The menus read fire as select, use as back, and the weapon buttons as the page buttons: LB / RB turn the
        // save and load screens' pages and Crispy's option pages (heretic/mn_menu.c).
        if (Pressed(SDL_GAMEPAD_BUTTON_SOUTH)) SET(VB_FIRE);
        if (Pressed(SDL_GAMEPAD_BUTTON_EAST)) SET(VB_USE);
        if (!bumpers && Pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) SET(VB_PREV_WEAPON);
        if (!bumpers && Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) SET(VB_NEXT_WEAPON);
        return buttons;
    }

    if (TriggerPressed(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) SET(VB_FIRE);
    if (TriggerPressed(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) || Pressed(SDL_GAMEPAD_BUTTON_SOUTH)) SET(VB_USE);
    if (Pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) SET(VB_PREV_WEAPON);
    if (Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) SET(VB_NEXT_WEAPON);
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) SET(VB_INV_LEFT);
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) SET(VB_INV_RIGHT);
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) || Pressed(SDL_GAMEPAD_BUTTON_WEST)) SET(VB_USE_ARTIFACT);
    if (Pressed(SDL_GAMEPAD_BUTTON_NORTH)) SET(VB_FLY_UP);
    if (Pressed(SDL_GAMEPAD_BUTTON_EAST)) SET(VB_FLY_DOWN);
    if (Pressed(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) SET(VB_FLY_CENTER);
    if (Pressed(SDL_GAMEPAD_BUTTON_BACK) && !bumpers) SET(VB_AUTOMAP);

#undef SET

    return buttons;
}

void I_UpdateJoystick(void)
{
    event_t ev;

    if (!joystick_initialized)
    {
        return;
    }

    // Reads the controllers. Nothing else reads SDL's events, so they're dropped rather than left to pile up.
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

    if (gamepad != NULL && !SDL_GamepadConnected(gamepad))
    {
        SDL_CloseGamepad(gamepad);
        gamepad = NULL;
    }

    if (gamepad == NULL)
    {
        OpenFirstGamepad();
        if (gamepad == NULL)
        {
            return;
        }
    }

    // The configuration file can't move the buttons.
    SetButtonLayout();

    ev.type = ev_joystick;
    ev.data1 = ButtonState();
    ev.data2 = Axis(SDL_GAMEPAD_AXIS_RIGHTX);    // turn
    ev.data3 = Axis(SDL_GAMEPAD_AXIS_LEFTY);     // forward and back
    ev.data4 = Axis(SDL_GAMEPAD_AXIS_LEFTX);     // strafe
    ev.data5 = Axis(SDL_GAMEPAD_AXIS_RIGHTY);    // look
    ev.data6 = DirectionalInput();

    D_PostEvent(&ev);
}

void I_BindJoystickVariables(void)
{
    M_BindIntVariable("use_analog",                 &use_analog);
    M_BindIntVariable("joystick_turn_sensitivity",  &joystick_turn_sensitivity);
    M_BindIntVariable("joystick_move_sensitivity",  &joystick_move_sensitivity);
    M_BindIntVariable("joystick_look_sensitivity",  &joystick_look_sensitivity);
}
