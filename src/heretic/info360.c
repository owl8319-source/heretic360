// Heretic 360: the INFO menu's controls page, in place of Heretic's two PC keyboard pages (HELP1 and HELP2). It shows
// a controller (drawn by tools/make_controls_art.py, info360_art.h) over the title screen, darkened, and what each
// button does. INFO then goes on to Raven's credits page, as before.

#include "doomdef.h"
#include "crispy.h"
#include "v_trans.h"
#include "deh_str.h"
#include "i_video.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

// The controller: art/360_controller.svg, made into Heretic's palette by tools/make_controls_art.py.
#include "info360_art.h"

// How dark the title screen behind the page is: a light level of COLORMAP, 0 (as bright as the game) to 31.
#define BACKGROUND_DARKNESS 29

#define TITLE_Y 3

// The list under the controller: two columns, the button then what it does.
#define LIST_TOP 135
#define LIST_ROW 9
#define LEFT_BUTTON_X 6
#define LEFT_ACTION_X 73
#define RIGHT_BUTTON_X 166
#define RIGHT_ACTION_X 228

typedef struct
{
    const char *button;
    const char *action;
} control_t;

// The controls as the game starts; Options > Button Layout changes them (xenon/i_joystick.c).
static const control_t left_column[] = {
    { "LT",       "USE" },
    { "RT",       "FIRE" },
    { "LB / RB",  "WEAPONS" },
    { "L STICK",  "MOVE" },
    { "R STICK",  "TURN / LOOK" },
    { "L CLICK",  "WALK / RUN" },
    { "R CLICK",  "CENTER VIEW" },
};

static const control_t right_column[] = {
    { "A",        "USE / JUMP" },
    { "X / D-UP", "USE ITEM" },
    { "Y / B",    "FLY UP / DOWN" },
    { "D-PAD",    "INVENTORY" },
    { "BACK",     "MAP" },
    { "START",    "MENU" },
    { "ON MAP",   "LB / RB ZOOM" },
};

// Names beside the shoulder buttons on the drawing.
typedef struct
{
    const char *text;
    int x;
    int y;
} tag_t;

static const tag_t tags[] = {
    { "LT", 92, 26 },
    { "RT", 216, 26 },
    { "LB", 76, 38 },
    { "RB", 230, 38 },
};

#define COUNT(a) ((int) (sizeof(a) / sizeof((a)[0])))

static void DrawDarkTitle(void)
{
    const byte *colormap = (const byte *) W_CacheLumpName(DEH_String("COLORMAP"), PU_CACHE)
                         + 256 * BACKGROUND_DARKNESS;

    // [crispy] the title screen, centred, its sides filled in widescreen as Crispy fills them.
    V_DrawFullscreenRawOrPatch(W_GetNumForName(DEH_String("TITLE")));

    for (int i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
    {
        I_VideoBuffer[i] = colormap[I_VideoBuffer[i]];
    }
}

// Unpacks info360_art.h into its box: per byte h, h & 0x80 is a run of (h & 0x7f) + 1 clear pixels, otherwise
// h + 1 palette indexes follow. The box is on Heretic's 320x200 screen: with Crispy's high resolution each pixel is
// drawn 2x2, and widescreen moves it right by WIDESCREENDELTA, as Crispy draws the menus.
static void DrawController(void)
{
    const int scale = 1 << crispy->hires;

    const unsigned char *in = controls_art;
    const unsigned char *const end = controls_art + sizeof(controls_art);
    int i = 0;

    while (in < end && i < CONTROLS_ART_W * CONTROLS_ART_H)
    {
        const int header = *in++;
        const int count = (header & 0x7f) + 1;

        for (int k = 0; k < count && i < CONTROLS_ART_W * CONTROLS_ART_H; k++, i++)
        {
            if (header & 0x80)
            {
                continue;
            }

            const int x = (CONTROLS_ART_X + WIDESCREENDELTA + i % CONTROLS_ART_W) * scale;
            const int y = (CONTROLS_ART_Y + i / CONTROLS_ART_W) * scale;
            const pixel_t colour = *in++;

            for (int dy = 0; dy < scale; dy++)
            {
                for (int dx = 0; dx < scale; dx++)
                {
                    I_VideoBuffer[(y + dy) * SCREENWIDTH + x + dx] = colour;
                }
            }
        }
    }
}

static void DrawColumn(const control_t *controls, int count, int button_x, int action_x)
{
    for (int i = 0; i < count; i++)
    {
        const int y = LIST_TOP + i * LIST_ROW;

        MN_DrTextA(controls[i].button, button_x, y);
        MN_DrTextA(controls[i].action, action_x, y);
    }
}

void MN_DrawControls360(void)
{
    const char *title = "DEFAULT CONTROLS";
    const char *note = "CHANGE THEM IN OPTIONS - BUTTON LAYOUT";

    DrawDarkTitle();
    MN_DrTextB(title, 160 - MN_TextBWidth(title) / 2, TITLE_Y);
    DrawController();

    for (int i = 0; i < COUNT(tags); i++)
    {
        MN_DrTextA(tags[i].text, tags[i].x, tags[i].y);
    }

    // Under the controller, in gold.
    dp_translation = cr[CR_GOLD];
    MN_DrTextA(note, 160 - MN_TextAWidth(note) / 2, CONTROLS_ART_Y + CONTROLS_ART_H + 2);
    dp_translation = NULL;

    DrawColumn(left_column, COUNT(left_column), LEFT_BUTTON_X, LEFT_ACTION_X);
    DrawColumn(right_column, COUNT(right_column), RIGHT_BUTTON_X, RIGHT_ACTION_X);
}
