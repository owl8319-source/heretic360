// Heretic 360: the INFO menu's controls page, in place of Heretic's two PC keyboard pages (HELP1 and HELP2). It shows
// a controller (drawn by tools/make_controls_art.py, info360_art.h) over the title screen, darkened, and what each
// button does. INFO then goes on to Raven's credits page, as before.

#include "doomdef.h"
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
#define LIST_TOP 126
#define LIST_ROW 10
#define LEFT_BUTTON_X 6
#define LEFT_ACTION_X 73
#define RIGHT_BUTTON_X 166
#define RIGHT_ACTION_X 222

typedef struct
{
    const char *button;
    const char *action;
} control_t;

static const control_t left_column[] = {
    { "LT",       "USE" },
    { "LB",       "PREV WEAPON" },
    { "L STICK",  "MOVE" },
    { "D-PAD",    "INVENTORY" },
    { "D-PAD UP", "USE ITEM" },
    { "BACK",     "MAP" },
    { "START",    "MENU" },
};

static const control_t right_column[] = {
    { "RT",       "FIRE" },
    { "RB",       "NEXT WEAPON" },
    { "R STICK",  "TURN / LOOK" },
    { "R CLICK",  "CENTER VIEW" },
    { "A",        "USE" },
    { "X",        "USE ITEM" },
    { "Y / B",    "FLY UP / DOWN" },
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

    V_DrawRawScreen(W_CacheLumpName(DEH_String("TITLE"), PU_CACHE));

    for (int i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
    {
        I_VideoBuffer[i] = colormap[I_VideoBuffer[i]];
    }
}

// Unpacks info360_art.h into its box: per byte h, h & 0x80 is a run of (h & 0x7f) + 1 clear pixels, otherwise
// h + 1 palette indexes follow.
static void DrawController(void)
{
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

            const int x = CONTROLS_ART_X + i % CONTROLS_ART_W;
            const int y = CONTROLS_ART_Y + i / CONTROLS_ART_W;

            I_VideoBuffer[y * SCREENWIDTH + x] = *in++;
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
    const char *title = "CONTROLS";

    DrawDarkTitle();
    MN_DrTextB(title, 160 - MN_TextBWidth(title) / 2, TITLE_Y);
    DrawController();

    for (int i = 0; i < COUNT(tags); i++)
    {
        MN_DrTextA(tags[i].text, tags[i].x, tags[i].y);
    }

    DrawColumn(left_column, COUNT(left_column), LEFT_BUTTON_X, LEFT_ACTION_X);
    DrawColumn(right_column, COUNT(right_column), RIGHT_BUTTON_X, RIGHT_ACTION_X);
}
