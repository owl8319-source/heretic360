// Heretic 360: the developer cheat menu.
//
// Hold LB + RB + Back for about a second in a level and it opens over the game, paused, as Heretic's own menu pauses
// it. It has these five cheats and no others, and they work on every skill level, Black Plague Possesses Thee too:
//
//   God Mode                     on and off, as quicken
//   All Keys                     as skel
//   All Weapons and Full Ammo    as rambo
//   Kill Every Monster           as massacre
//   Frame-Rate Counter           on and off, as ticker
//
// Each runs the typed cheat's own function in sb_bar.c (SB_DevCheat), not a copy of it; only the skill level check,
// which is in the code that reads typed cheats, is left out. The menu isn't in Heretic's main menu or options.
//
// D-pad or left stick: choose. A: select. B: close. (Not Start: Heretic's menu opens on Start held, so closing on it
// would open that straight after.)

#include "doomdef.h"
#include "deh_str.h"
#include "i_joystick.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_controls.h"
#include "s_sound.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

// How long LB + RB + Back are held before the menu opens.
#define HOLD_MS 1000

// How long a held direction waits before it moves again, in tics; Heretic's menu waits the same.
#define REPEAT_TICS 5

// The box the menu is drawn in, on Heretic's 320x200 screen.
#define BOX_LEFT 40
#define BOX_RIGHT 280
#define BOX_TOP 36
#define BOX_BOTTOM 156
#define TITLE_Y 44
#define FIRST_ITEM_Y 72
#define ITEM_HEIGHT 13
#define ITEM_X 66
#define VALUE_RIGHT 262
#define CURSOR_X 54
#define HINT_Y 145

// How dark the box behind the menu is: a light level of COLORMAP, 0 (as bright as the game) to 31.
#define BOX_DARKNESS 22

boolean DM_Active = false;

typedef struct
{
    const char *name;
    devcheat_t cheat;
    boolean toggle;
} devmenu_item_t;

static const devmenu_item_t items[] = {
    { "GOD MODE",                   DEVCHEAT_GOD,      true  },
    { "ALL KEYS",                   DEVCHEAT_KEYS,     false },
    { "ALL WEAPONS AND FULL AMMO",  DEVCHEAT_WEAPONS,  false },
    { "KILL EVERY MONSTER",         DEVCHEAT_MASSACRE, false },
    { "FRAME-RATE COUNTER",         DEVCHEAT_TICKER,   true  },
};

#define NUM_ITEMS ((int) (sizeof(items) / sizeof(items[0])))

static int cursor = 0;
static boolean was_paused;

// The combination: when it was first seen held, and whether this hold has opened the menu already.
static boolean combo_held = false;
static int combo_started_ms;
static boolean combo_used = false;

// The buttons the last event had, to act on presses, not holds.
static int last_buttons = 0;

static boolean ButtonPressed(int buttons, int button)
{
    if (button < 0)
    {
        return false;
    }

    return (buttons & (1 << button)) != 0 && (last_buttons & (1 << button)) == 0;
}

static boolean CanOpen(void)
{
    return gamestate == GS_LEVEL && !MenuActive && !askforquit && !demoplayback && !netgame
        && players[consoleplayer].mo != NULL;
}

static void Open(void)
{
    DM_Active = true;
    was_paused = paused;
    paused = true;
    S_StartSound(NULL, sfx_dorcls);
}

static void Close(void)
{
    DM_Active = false;
    paused = was_paused;
    S_StartSound(NULL, sfx_dorcls);
}

static void Select(void)
{
    SB_DevCheat(&players[consoleplayer], items[cursor].cheat);
    S_StartSound(NULL, sfx_chat);
}

static void Move(int direction)
{
    if (JOY_GET_DPAD(direction) & JOY_DIR_UP || JOY_GET_LSTICK(direction) & JOY_DIR_UP)
    {
        cursor = (cursor + NUM_ITEMS - 1) % NUM_ITEMS;
        S_StartSound(NULL, sfx_switch);
        joywait = I_GetTime() + REPEAT_TICS;
    }
    else if (JOY_GET_DPAD(direction) & JOY_DIR_DOWN || JOY_GET_LSTICK(direction) & JOY_DIR_DOWN)
    {
        cursor = (cursor + 1) % NUM_ITEMS;
        S_StartSound(NULL, sfx_switch);
        joywait = I_GetTime() + REPEAT_TICS;
    }
}

// Watches for the combination, and runs the menu while it's open. True when it takes the event.
boolean DM_Responder(event_t *event)
{
    boolean taken = false;

    if (event->type != ev_joystick)
    {
        // With the menu open, nothing reaches the game.
        return DM_Active;
    }

    const int buttons = event->data1;

    if (buttons & (1 << JOY_DEV_COMBO_BUTTON))
    {
        if (!combo_held)
        {
            combo_held = true;
            combo_started_ms = I_GetTimeMS();
        }
        else if (!combo_used && !DM_Active && I_GetTimeMS() - combo_started_ms >= HOLD_MS && CanOpen())
        {
            combo_used = true;
            Open();
            last_buttons = buttons;
            return true;
        }
    }
    else
    {
        combo_held = false;
        combo_used = false;
    }

    if (DM_Active)
    {
        // The level can end under the menu (a death exit, say); then it closes.
        if (gamestate != GS_LEVEL)
        {
            Close();
        }
        else if (ButtonPressed(buttons, joybuse))
        {
            Close();
        }
        else if (ButtonPressed(buttons, joybfire))
        {
            Select();
        }
        else
        {
            Move(event->data6);
        }

        taken = true;
    }

    last_buttons = buttons;

    return taken;
}

// Darkens the box behind the menu through COLORMAP, as Heretic darkens a dim room.
static void DarkenBox(void)
{
    const byte *colormap = (const byte *) W_CacheLumpName(DEH_String("COLORMAP"), PU_CACHE) + 256 * BOX_DARKNESS;

    for (int y = BOX_TOP; y < BOX_BOTTOM; y++)
    {
        pixel_t *row = I_VideoBuffer + y * SCREENWIDTH;

        for (int x = BOX_LEFT; x < BOX_RIGHT; x++)
        {
            row[x] = colormap[row[x]];
        }
    }
}

static const char *ItemValue(int i)
{
    if (!items[i].toggle)
    {
        return NULL;
    }

    switch (items[i].cheat)
    {
        case DEVCHEAT_GOD:
            return (players[consoleplayer].cheats & CF_GODMODE) ? "ON" : "OFF";
        case DEVCHEAT_TICKER:
            return SB_DevTickerOn() ? "ON" : "OFF";
        default:
            return NULL;
    }
}

void DM_Drawer(void)
{
    const char *title = "CHEATS";
    const char *hint = "A SELECT   B CLOSE";

    if (!DM_Active)
    {
        return;
    }

    DarkenBox();

    MN_DrTextB(title, 160 - MN_TextBWidth(title) / 2, TITLE_Y);

    for (int i = 0; i < NUM_ITEMS; i++)
    {
        const int y = FIRST_ITEM_Y + i * ITEM_HEIGHT;
        const char *value = ItemValue(i);

        MN_DrTextA(items[i].name, ITEM_X, y);

        if (value != NULL)
        {
            MN_DrTextA(value, VALUE_RIGHT - MN_TextAWidth(value), y);
        }
    }

    // The cursor blinks, as Heretic's menu skull turns.
    if ((I_GetTime() / 8) & 1)
    {
        MN_DrTextA(">", CURSOR_X, FIRST_ITEM_Y + cursor * ITEM_HEIGHT);
    }

    MN_DrTextA(hint, 160 - MN_TextAWidth(hint) / 2, HINT_Y);
}
