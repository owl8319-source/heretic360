// Heretic 360: the Xbox 360 controller, read through SDL3 (SDL-xenon reads it with XamInputGetState), posted to the
// game as Crispy Doom's ev_joystick events with a fixed layout:
//
//   In the game                          In the menus
//   Left stick    move and strafe        Left stick, D-pad  choose
//   Right stick   turn and look          A                  select (yes, on the quit question)
//   RT            fire                   B                  back (no, on the quit question)
//   LT            use (doors, switches)  Start              open the menu
//   A             use, or jump with Crispness' jumping on
//   LB / RB       previous / next weapon LB / RB            previous / next page
//   D-pad left / right  inventory
//   D-pad up or X  use the artifact      On the map
//   Y / B         fly up / down          LB / RB            zoom out / in
//   Left stick click  walk (run, with    Right stick click  follow the player or not
//                 always run off)        Left stick         scroll, when not following
//   Right stick click  centre the view and stop flying
//   Back          map
//   Start         menu
//
// The Controller menu (heretic/mn_menu.c) sets the turn and look speeds, the dead zone, inverting the look, always run
// and vibration. LB + RB + Back, held about a second in a level, opens the developer cheat menu
// (src/heretic/devmenu.c), so Back opens no map while both bumpers are held. In that menu A selects and B closes, as in
// Heretic's. When the Guide or one of the console's dialogs opens, the game opens its menu, which pauses it, and the
// controller reads as let go until the dialog closes.

#include <stdio.h>

#include <SDL3/SDL.h>

#include "d_event.h"
#include "doomtype.h"
#include "i_joystick.h"
#include "i_system.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_fixed.h"

#include "xsdk/xam.h"

// Heretic's menu state (heretic/mn_menu.c) and the developer cheat menu's (heretic/devmenu.c): A and B mean select
// and back there, use and fly down in the game.
extern boolean MenuActive;
extern boolean askforquit;
extern boolean DM_Active;
// Whether the map is up (heretic/am_map.c): the bumpers and the right stick's click work it.
extern boolean automapactive;
// Whether jumping is on (Crispness), as the game may use it: off in demos and network games (crispy.c's critical).
extern int MN_JumpingOn(void);
// Opens Heretic's menu, pausing the game, when the Guide opens over a level (heretic/mn_menu.c).
extern void MN_SystemUIOpened(void);

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
    // 12 is JOY_DEV_COMBO_BUTTON.
    VB_SPEED = 13,
    VB_JUMP,
    VB_MAP_ZOOM_OUT = JOY_MAP_ZOOM_OUT_BUTTON,
    VB_MAP_ZOOM_IN = JOY_MAP_ZOOM_IN_BUTTON,
    VB_MAP_FOLLOW = JOY_MAP_FOLLOW_BUTTON,
};

// Heretic's g_game.c treats a speed button past its 20 buttons as always held: always run.
#define ALWAYS_RUN_BUTTON 29

// No speed button: the run button reads as never held (g_game.c's speedkeydown), so the game walks.
#define NO_SPEED_BUTTON -1
// Menus move once the stick is past half its travel.
#define DIRECTION_DEAD_ZONE (50 * 32768 / 100)

static SDL_Gamepad *gamepad = NULL;
static boolean joystick_initialized = false;

int use_analog = 1;
int joystick_turn_sensitivity = 10;
int joystick_move_sensitivity = 10;
int joystick_look_sensitivity = 10;
int joystick_look_invert = 0;
// Percent of a stick's travel.
int joystick_dead_zone = 25;
int joystick_always_run = 1;
int joystick_vibration = 1;

// With always run on, the speed button walks while it's held: Heretic runs with its speed button past the 20 it
// knows, and walks with none (g_game.c). With always run off, the speed button is an ordinary button that runs.
static void SetSpeedButton(boolean held)
{
    if (joystick_always_run)
    {
        joybspeed = held ? NO_SPEED_BUTTON : ALWAYS_RUN_BUTTON;
    }
    else
    {
        joybspeed = VB_SPEED;
    }
}

static void SetButtonLayout(void)
{
    joybfire = VB_FIRE;
    joybuse = VB_USE;
    joybstrafe = -1;
    joybstrafeleft = -1;
    joybstraferight = -1;
    joybjump = VB_JUMP;
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
    const int dead_zone = joystick_dead_zone * 32768 / 100;

    if (value < dead_zone && value > -dead_zone)
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
    if (TriggerPressed(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) SET(VB_USE);
    // A jumps with Crispness' jumping on, and is a second use button otherwise.
    if (Pressed(SDL_GAMEPAD_BUTTON_SOUTH)) SET(MN_JumpingOn() ? VB_JUMP : VB_USE);
    if (Pressed(SDL_GAMEPAD_BUTTON_LEFT_STICK)) SET(VB_SPEED);
    SetSpeedButton(Pressed(SDL_GAMEPAD_BUTTON_LEFT_STICK));
    if (automapactive)
    {
        // On the map the bumpers zoom and the right stick's click follows the player or lets the map scroll
        // (heretic/am_map.c), in place of changing weapons and centring the view.
        if (Pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) && !bumpers) SET(VB_MAP_ZOOM_OUT);
        if (Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) && !bumpers) SET(VB_MAP_ZOOM_IN);
        if (Pressed(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) SET(VB_MAP_FOLLOW);
    }
    else
    {
        if (Pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) SET(VB_PREV_WEAPON);
        if (Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) SET(VB_NEXT_WEAPON);
        if (Pressed(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) SET(VB_FLY_CENTER);
    }
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) SET(VB_INV_LEFT);
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) SET(VB_INV_RIGHT);
    if (Pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) || Pressed(SDL_GAMEPAD_BUTTON_WEST)) SET(VB_USE_ARTIFACT);
    if (Pressed(SDL_GAMEPAD_BUTTON_NORTH)) SET(VB_FLY_UP);
    if (Pressed(SDL_GAMEPAD_BUTTON_EAST)) SET(VB_FLY_DOWN);
    if (Pressed(SDL_GAMEPAD_BUTTON_BACK) && !bumpers) SET(VB_AUTOMAP);

#undef SET

    return buttons;
}

static boolean SystemUIOpen(void);

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

    // Asked once a frame, controller or not (xsdk's docs/GUIDE.md).
    const boolean ui_open = SystemUIOpen();

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

    // While the Guide or a dialog of the console's is up, the game's menu is open and the controller reads as let go.
    if (ui_open)
    {
        ev.data1 = ev.data2 = ev.data3 = ev.data4 = ev.data5 = ev.data6 = 0;
        SetSpeedButton(false);
        D_PostEvent(&ev);
        return;
    }

    ev.data1 = ButtonState();
    ev.data2 = Axis(SDL_GAMEPAD_AXIS_RIGHTX);    // turn
    ev.data3 = Axis(SDL_GAMEPAD_AXIS_LEFTY);     // forward and back
    ev.data4 = Axis(SDL_GAMEPAD_AXIS_LEFTX);     // strafe
    ev.data5 = Axis(SDL_GAMEPAD_AXIS_RIGHTY);    // look
    if (joystick_look_invert)
    {
        ev.data5 = -ev.data5;
    }
    ev.data6 = DirectionalInput();

    D_PostEvent(&ev);
}

// Whether the Guide or one of the console's dialogs is up (xsdk_system_ui_open, asked once a frame); as it opens, the
// game's menu opens and the controller stops shaking.
static boolean SystemUIOpen(void)
{
    static boolean was_open = false;
    const boolean open = xsdk_system_ui_open();

    if (open && !was_open)
    {
        if (gamepad != NULL)
        {
            SDL_RumbleGamepad(gamepad, 0, 0, 0);
        }
        MN_SystemUIOpened();
    }

    was_open = open;
    return open;
}

void I_Rumble(int strength, int ms)
{
    if (!joystick_vibration || gamepad == NULL || strength <= 0)
    {
        return;
    }

    if (strength > 100)
    {
        strength = 100;
    }

    // The left motor is the heavy one, the right the light one.
    SDL_RumbleGamepad(gamepad, (Uint16) (strength * 65535 / 100), (Uint16) (strength * 65535 / 160), (Uint32) ms);
}

void I_BindJoystickVariables(void)
{
    M_BindIntVariable("use_analog",                 &use_analog);
    M_BindIntVariable("joystick_turn_sensitivity",  &joystick_turn_sensitivity);
    M_BindIntVariable("joystick_move_sensitivity",  &joystick_move_sensitivity);
    M_BindIntVariable("joystick_look_sensitivity",  &joystick_look_sensitivity);
    M_BindIntVariable("joystick_look_invert",       &joystick_look_invert);
    M_BindIntVariable("joystick_dead_zone",         &joystick_dead_zone);
    M_BindIntVariable("joystick_always_run",        &joystick_always_run);
    M_BindIntVariable("joystick_vibration",         &joystick_vibration);
}
