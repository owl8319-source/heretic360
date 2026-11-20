// Heretic 360: Crispy Doom's i_video.c for the Xbox 360 through xsdk.
//
// Heretic draws each frame into I_VideoBuffer, SCREENWIDTH x SCREENHEIGHT bytes of palette indexes: 320x200, or
// 640x400 with Crispy's high resolution, and wider with its widescreen (I_GetScreenDimensions). I_FinishUpdate turns
// them into colours through the palette, uploads them into a texture and has the GPU draw it, point sampled, on a quad
// in the middle of the 1280x720 display, shaped as Crispy's aspect_ratio_correct shows it on a PC: 4:3, 960x720 with
// black bars either side, or the display's full 16:9 in widescreen. xsdk scales the 720p frame to the console's
// output. Crispy's menu changes the resolution and widescreen as the game runs (I_ReInitGraphics).

#include "config.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/present.h"
#include "xsdk/shader.h"
#include "xsdk/texture.h"

#include "heretic_shaders.h"

#include "d_event.h"
#include "d_loop.h"
#include "deh_str.h"
#include "doomtype.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "crispy.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_diskicon.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

enum
{
    BUFFERS = 2,
    SWAP_INTERVAL = 1,
    // The Guide's first opening held a frame up 2.7 s (xsdk docs/HARDWARE.md, "The Guide, milestone 37").
    BEGIN_TIMEOUT_MS = 5000,
    FENCE_TIMEOUT_MS = 1000,
    CLEAR_ARGB = 0xFF000000,
    // Two triangles: position then texture coordinates, blit.vert's a_position and a_uv.
    QUAD_VERTICES = 6,
    VERTEX_FLOATS = 4,
    STRIDE_BYTES = VERTEX_FLOATS * 4,
    UV_OFFSET_BYTES = 8,
    QUAD_BYTES = QUAD_VERTICES * STRIDE_BYTES,
    // Each xsdk_gpu_alloc takes at least 64 KiB (xsdk docs/PITFALLS.md).
    VERTEX_ALLOC_BYTES = 64 * 1024,
    FRAME_TEXTURES = 2,
};

// The vertex shader's inputs, by location: 0 the position, 2 the texture coordinates (xenos-toolchain docs/xsc.md,
// "Interface conventions").
static const XsdkVertexElement quad_layout[] = {
    { .usage = XSDK_USAGE_POSITION, .format = XSDK_VERTEX_FLOAT2, .offset_bytes = 0 },
    { .usage = XSDK_USAGE_TEXCOORD, .format = XSDK_VERTEX_FLOAT2, .offset_bytes = UV_OFFSET_BYTES },
};

static XsdkShader vertex_shader;
static XsdkShader pixel_shader;
static volatile uint32_t *quad_vertices;
static XsdkTexture frame_textures[FRAME_TEXTURES];
static uint32_t frame_fences[FRAME_TEXTURES];
static XsdkSamplerState frame_sampler;
static uint32_t display_width;
static uint32_t display_height;
static unsigned int frame_count;
static boolean gpu_failed;

int SCREENWIDTH, SCREENHEIGHT, SCREENHEIGHT_4_3;
int NONWIDEWIDTH; // [crispy] non-widescreen SCREENWIDTH
int WIDESCREENDELTA; // [crispy] horizontal widescreen offset

// [AM] Fractional part of the current tic, in [0.0, 1.0), for drawing between tics.
fixed_t fractionaltic;

// [crispy] the gamma levels, Crispy's 18 (I_SetGammaTable).
byte gamma2table[18][256];

// Heretic's frame as palette indexes, and as colours, 0xAARRGGBB, before it's uploaded; both as big as Crispy's
// largest frame, SCREENWIDTH x SCREENHEIGHT of them used.
static pixel_t video_buffer_memory[MAXWIDTH * MAXHEIGHT];
static uint32_t argb_frame[MAXWIDTH * MAXHEIGHT];
static uint32_t palette_argb[256];
static byte palette_rgb[256][3];

// The size the textures and the quad were made for.
static int texture_width;
static int texture_height;

static boolean initialized = false;

// The slow frame report: a frame shown more than SLOW_FRAME_MS after the one before goes in the log (through stdout,
// which i_main.c's writer thread takes to the disk, so the report itself doesn't wait on the drive), with how long
// this frame waited on the display, on its texture, and on the sound mixer (i_sound.c).
#define SLOW_FRAME_MS 150
int I_TakeSoundLockWaitMS(void);
static int last_frame_ms = -1;
static int present_wait_ms;
static int texture_wait_ms;
static unsigned int slow_frames;

// Variables Chocolate Doom's configuration file keeps. Most mean nothing on the console; they're bound so the
// configuration file reads and writes as it does on a PC.

int usemouse = 0;
int png_screenshots = 0;
char *video_driver = "";
char *window_position = "center";
int video_display = 0;
int window_width = 1280;
int window_height = 720;
int fullscreen_width = 0, fullscreen_height = 0;
static int max_scaling_buffer_pixels = 16000000;
int fullscreen = true;
// 1, as on a PC: Heretic's 200 lines shown as 240, a 320x200 frame as 4:3. Crispy's widescreen needs it.
int aspect_ratio_correct = 1;
// 0 draws the frame point sampled, sharp pixels; 1 filters it.
int smooth_pixel_scaling = false;
int integer_scaling = false;
int vga_porch_flash = false;
int force_software_renderer = false;
static int startup_delay = 0;
static int grabmouse = false;
int screen_width = 1280;
int screen_height = 720;

pixel_t *I_VideoBuffer = NULL;
boolean screensaver_mode = false;
boolean screenvisible = true;
static boolean display_fps_dots;
static boolean noblit;
int usegamma = 0;
unsigned int joywait = 0;

void I_SetGrabMouseCallback(grabmouse_callback_t func)
{
    (void) func;
}

void I_DisplayFPSDots(boolean dots_on)
{
    display_fps_dots = dots_on;
}

void I_ShutdownGraphics(void)
{
    // xsdk's exit closes the display and gives the GPU back.
    initialized = false;
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    if (!initialized)
    {
        return;
    }

    if (joywait < (unsigned int) I_GetTime())
    {
        I_UpdateJoystick();
    }
}

void I_UpdateNoBlit(void)
{
}

// Writes words into GPU memory with volatile 32-bit stores, then drops the GPU's cached copy.
static void WriteGPU(volatile uint32_t *memory, const void *data, uint32_t size_bytes)
{
    for (uint32_t i = 0; i < size_bytes / 4; i++)
    {
        uint32_t word;
        memcpy(&word, (const uint8_t *) data + i * 4, 4);
        memory[i] = word;
    }

    xsdk_gpu_invalidate(xsdk_gpu_address(memory), size_bytes);
}

// The quad in the middle of the display, as two triangles, in clip space (y up), v = 0 the frame's top row, shaped as
// the frame is shown: SCREENWIDTH to SCREENHEIGHT, the height stretched to 6/5 with aspect_ratio_correct. The full
// height and 960 wide for 4:3 (320x200 or 640x400); the full 1280 for 16:9; for a frame wider than the display (21:9)
// the full width, with black bars above and below, as Crispy shows it on a PC.
static void WriteQuad(void)
{
    const int shown_height = (aspect_ratio_correct == 1) ? 6 * SCREENHEIGHT / 5 : SCREENHEIGHT;
    int quad_width = (int) display_height * SCREENWIDTH / shown_height;
    int quad_height = (int) display_height;

    if (quad_width > (int) display_width)
    {
        quad_width = (int) display_width;
        quad_height = (int) display_width * shown_height / SCREENWIDTH;
    }

    printf("heretic360: frame %dx%d, drawn %dx%d in the middle of %ux%u\n", SCREENWIDTH, SCREENHEIGHT, quad_width,
           quad_height, (unsigned) display_width, (unsigned) display_height);

    const float half_width = (float) quad_width / (float) display_width;
    const float half_height = (float) quad_height / (float) display_height;
    const float l = -half_width, r = half_width, t = half_height, b = -half_height;
    const float quad[QUAD_VERTICES][VERTEX_FLOATS] = {
        { l, t, 0.0f, 0.0f }, { r, t, 1.0f, 0.0f }, { l, b, 0.0f, 1.0f },
        { l, b, 0.0f, 1.0f }, { r, t, 1.0f, 0.0f }, { r, b, 1.0f, 1.0f },
    };

    WriteGPU(quad_vertices, quad, sizeof quad);
}

static void MakeSampler(void)
{
    const uint32_t filter = smooth_pixel_scaling ? XSDK_FILTER_LINEAR : XSDK_FILTER_POINT;
    const XsdkSampler sampler = {
        .mag_filter = filter,
        .min_filter = filter,
        .mip_filter = XSDK_FILTER_POINT,
        .address_u = XSDK_ADDRESS_CLAMP,
        .address_v = XSDK_ADDRESS_CLAMP,
    };

    xsdk_texture_sampler_state(&frame_sampler, &sampler);
}

static void OpenGPU(void)
{
    NTSTATUS status;

    status = xsdk_gpu_open();
    if (!NT_SUCCESS(status))
    {
        I_Error("I_InitGraphics: xsdk_gpu_open failed: 0x%08X", (unsigned) status);
    }

    status = xsdk_shader_load(&vertex_shader, blit_vert.container, blit_vert.container_bytes);
    if (NT_SUCCESS(status))
    {
        status = xsdk_shader_load(&pixel_shader, blit_frag.container, blit_frag.container_bytes);
    }
    if (NT_SUCCESS(status))
    {
        status = xsdk_shader_set_vertex_layout(&vertex_shader, quad_layout,
                                               sizeof quad_layout / sizeof quad_layout[0],
                                               (const uint32_t[]){ STRIDE_BYTES });
    }
    if (!NT_SUCCESS(status))
    {
        I_Error("I_InitGraphics: loading the shaders failed: 0x%08X", (unsigned) status);
    }

    quad_vertices = xsdk_gpu_alloc(VERTEX_ALLOC_BYTES);
    if (quad_vertices == NULL)
    {
        I_Error("I_InitGraphics: xsdk_gpu_alloc failed");
    }

    status = xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, SWAP_INTERVAL, &display_width, &display_height);
    if (!NT_SUCCESS(status))
    {
        I_Error("I_InitGraphics: xsdk_present_open failed: 0x%08X", (unsigned) status);
    }

    screen_width = (int) display_width;
    screen_height = (int) display_height;

    MakeSampler();
}

// Waits until the GPU has drawn every frame it was given, so its textures and the quad can change.
static void WaitForGPU(void)
{
    for (unsigned int i = 0; i < FRAME_TEXTURES; i++)
    {
        if (frame_fences[i] != 0)
        {
            const NTSTATUS status = xsdk_gpu_wait(frame_fences[i], FENCE_TIMEOUT_MS);
            if (!NT_SUCCESS(status))
            {
                printf("heretic360: xsdk_gpu_wait failed: 0x%08X\n", (unsigned) status);
            }
            frame_fences[i] = 0;
        }
    }
}

// The textures the frames go into, SCREENWIDTH x SCREENHEIGHT, and the quad they're drawn on: made at the start and
// again when Crispy's menu changes the resolution, widescreen or the aspect ratio.
static void MakeFrameTextures(void)
{
    if (texture_width == SCREENWIDTH && texture_height == SCREENHEIGHT)
    {
        return;
    }

    WaitForGPU();

    for (unsigned int i = 0; i < FRAME_TEXTURES; i++)
    {
        if (texture_width != 0)
        {
            xsdk_texture_free(&frame_textures[i]);
        }

        const NTSTATUS status =
            xsdk_texture_create(&frame_textures[i], XSDK_FORMAT_A8R8G8B8, SCREENWIDTH, SCREENHEIGHT, 1, true);
        if (!NT_SUCCESS(status))
        {
            I_Error("I_InitGraphics: xsdk_texture_create %dx%d failed: 0x%08X", SCREENWIDTH, SCREENHEIGHT,
                    (unsigned) status);
        }
    }

    texture_width = SCREENWIDTH;
    texture_height = SCREENHEIGHT;
}

static void MakeFrame(void)
{
    MakeFrameTextures();
    WaitForGPU();
    WriteQuad();
    MakeSampler();
}

static void ConvertFrame(void)
{
    const byte *src = I_VideoBuffer;
    uint32_t *dest = argb_frame;

    for (int i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
    {
        dest[i] = palette_argb[src[i]];
    }
}

static void ReportSlowFrame(void)
{
    const int now = I_GetTimeMS();
    const int sound_wait_ms = I_TakeSoundLockWaitMS();

    if (last_frame_ms >= 0 && now - last_frame_ms > SLOW_FRAME_MS)
    {
        slow_frames++;
        // A wait on the menus' pause or a level's loading is expected; a long one in play is the freeze.
        printf("heretic360: slow frame %u: %d ms since the last; waited %d ms for the display, %d ms for "
                       "the texture, %d ms for the sound mixer\n",
                       slow_frames, now - last_frame_ms, present_wait_ms, texture_wait_ms, sound_wait_ms);
    }

    last_frame_ms = now;
}

static void DrawFrame(void)
{
    uint32_t address;
    NTSTATUS status;
    const unsigned int t = frame_count % FRAME_TEXTURES;
    XsdkTexture *const texture = &frame_textures[t];

    int started = I_GetTimeMS();
    status = xsdk_present_begin(&address, BEGIN_TIMEOUT_MS);
    present_wait_ms = I_GetTimeMS() - started;
    if (!NT_SUCCESS(status))
    {
        // The GPU has hung: say so once, and stop drawing rather than wait 5 s a frame.
        printf("heretic360: xsdk_present_begin failed: 0x%08X\n", (unsigned) status);
        gpu_failed = true;
        return;
    }

    // The texture was last drawn two frames ago; wait until the GPU has finished with it before writing it again.
    started = I_GetTimeMS();
    if (frame_fences[t] != 0)
    {
        status = xsdk_gpu_wait(frame_fences[t], FENCE_TIMEOUT_MS);
        if (!NT_SUCCESS(status))
        {
            printf("heretic360: xsdk_gpu_wait failed: 0x%08X\n", (unsigned) status);
        }
    }

    texture_wait_ms = I_GetTimeMS() - started;

    xsdk_texture_upload(texture, 0, argb_frame, SCREENWIDTH * sizeof(uint32_t));
    xsdk_gpu_invalidate(texture->address, texture->size_bytes);

    // A resolve leaves no target, shaders or texture slot 0 behind, so each frame sets them again.
    xsdk_gpu_set_target(display_width, display_height);
    xsdk_gpu_set_shaders(&vertex_shader, &pixel_shader);
    xsdk_built_shader_set_literals(&blit_vert);
    xsdk_built_shader_set_literals(&blit_frag);
    xsdk_gpu_set_texture(BLIT_FRAG_SAMPLER_0_0, texture, &frame_sampler);
    xsdk_gpu_set_vertices(0, xsdk_gpu_address(quad_vertices), QUAD_BYTES);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, QUAD_VERTICES);
    // Copies the frame into the front buffer, then clears eDRAM to black for the next frame's bars.
    xsdk_gpu_resolve(address, display_width, display_height, CLEAR_ARGB);
    frame_fences[t] = xsdk_gpu_fence();

    xsdk_present_end();

    if (frame_count == 0)
    {
        printf("heretic360: first frame presented\n");
    }

    ReportSlowFrame();

    frame_count++;
}

void I_FinishUpdate(void)
{
    static int lasttic;
    int tics;
    int i;

    if (!initialized || noblit || gpu_failed)
    {
        return;
    }

    // Little dots on the bottom of the screen, the ticker cheat.
    if (display_fps_dots)
    {
        i = I_GetTime();
        tics = i - lasttic;
        lasttic = i;
        if (tics > 20) tics = 20;

        for (i = 0; i < tics * 4; i += 4)
            I_VideoBuffer[(SCREENHEIGHT - 1) * SCREENWIDTH + i] = 0xff;
        for ( ; i < 20 * 4; i += 4)
            I_VideoBuffer[(SCREENHEIGHT - 1) * SCREENWIDTH + i] = 0x0;
    }

    // [crispy] [AM] the frame-rate counter
    {
        static int lastmili;
        static int fpscount;
        const int mili = I_GetTimeMS();

        fpscount++;

        if (mili - lastmili >= 1000)
        {
            crispy->fps = (fpscount * 1000) / (mili - lastmili);
            fpscount = 0;
            lastmili = mili;
        }
    }

    V_DrawDiskIcon();
    ConvertFrame();
    V_RestoreDiskBackground();

    DrawFrame();

    // [crispy] the frame-rate limit, for an uncapped frame rate; the display's 60 Hz caps it anyway, as each frame
    // waits for the one before it to be shown.
    if (crispy->uncapped && !singletics && crispy->fpslimit >= TICRATE)
    {
        const uint64_t target_time = 1000000ull / crispy->fpslimit;
        static uint64_t start_time;

        while (1)
        {
            const uint64_t current_time = I_GetTimeUS();
            const uint64_t elapsed_time = current_time - start_time;

            if (elapsed_time >= target_time)
            {
                start_time = current_time;
                break;
            }

            if (target_time - elapsed_time > 1000)
            {
                I_Sleep((int) ((target_time - elapsed_time - 1000) / 1000));
            }
        }
    }
}

void I_ReadScreen(pixel_t *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT * sizeof(*scr));
}

void I_SetPalette(byte *doompalette)
{
    for (int i = 0; i < 256; ++i)
    {
        // The PC's VGA kept 6 bits a channel, so the bottom two go, as Chocolate Doom does.
        // [crispy] Crispy's 18 gamma levels.
        const byte r = gamma2table[crispy->gamma][*doompalette++] & ~3;
        const byte g = gamma2table[crispy->gamma][*doompalette++] & ~3;
        const byte b = gamma2table[crispy->gamma][*doompalette++] & ~3;

        palette_rgb[i][0] = r;
        palette_rgb[i][1] = g;
        palette_rgb[i][2] = b;
        palette_argb[i] = 0xFF000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
    }
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, best_diff = INT_MAX;

    for (int i = 0; i < 256; ++i)
    {
        const int dr = r - palette_rgb[i][0];
        const int dg = g - palette_rgb[i][1];
        const int db = b - palette_rgb[i][2];
        const int diff = dr * dr + dg * dg + db * db;

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

void I_SetWindowTitle(const char *title)
{
    (void) title;
}

void I_InitWindowTitle(void)
{
}

void I_RegisterWindowIcon(const unsigned int *icon, int width, int height)
{
    (void) icon;
    (void) width;
    (void) height;
}

void I_InitWindowIcon(void)
{
}

void I_GraphicsCheckCommandLine(void)
{
    noblit = M_CheckParm("-noblit");
}

void I_CheckIsScreensaver(void)
{
}

void I_GetWindowPosition(int *x, int *y, int w, int h)
{
    (void) w;
    (void) h;
    *x = 0;
    *y = 0;
}

void I_InitGraphics(void)
{
    byte *doompal;

    OpenGPU();

    I_GetScreenDimensions();
    V_Init();
    MakeFrame();

    I_VideoBuffer = video_buffer_memory;
    V_RestoreBuffer();
    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT * sizeof(*I_VideoBuffer));

    doompal = W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE);
    I_SetPalette(doompal);

    initialized = true;

    I_AtExit(I_ShutdownGraphics, true);
}

// [crispy] SCREENWIDTH, SCREENHEIGHT, NONWIDEWIDTH and WIDESCREENDELTA from Crispy's resolution and widescreen
// settings, as Crispy Doom works them out, the display's shape being the console's 16:9.
void I_GetScreenDimensions(void)
{
    int w = 16, h = 9;
    int ah;

    SCREENWIDTH = ORIGWIDTH << crispy->hires;
    SCREENHEIGHT = ORIGHEIGHT << crispy->hires;
    SCREENHEIGHT_4_3 = (ORIGHEIGHT_4_3 << crispy->hires);

    NONWIDEWIDTH = SCREENWIDTH;

    ah = (aspect_ratio_correct == 1) ? (6 * SCREENHEIGHT / 5) : SCREENHEIGHT;

    // [crispy] widescreen rendering makes no sense without aspect ratio correction
    if (crispy->widescreen && aspect_ratio_correct == 1)
    {
        switch (crispy->widescreen)
        {
            case RATIO_16_10:
                w = 16;
                h = 10;
                break;
            case RATIO_16_9:
                w = 16;
                h = 9;
                break;
            case RATIO_21_9:
                w = 21;
                h = 9;
                break;
            default:
                // RATIO_MATCH_SCREEN: the console's display.
                break;
        }

        SCREENWIDTH = w * ah / h;
        // [crispy] make sure SCREENWIDTH is an integer multiple of 4 ...
        SCREENWIDTH = (SCREENWIDTH + (crispy->hires ? 0 : 3)) & (int) ~3;
        // [crispy] ... but never exceeds MAXWIDTH (array size!)
        SCREENWIDTH = MIN(SCREENWIDTH, MAXWIDTH);
    }

    WIDESCREENDELTA = ((SCREENWIDTH - NONWIDEWIDTH) >> crispy->hires) / 2;
}

// [crispy] Crispy's menu has changed the resolution, widescreen, the aspect ratio or vsync. The frame buffer is as big
// as the largest frame, so only the textures and the quad change.
void I_ReInitGraphics(int reinit)
{
    if (reinit & REINIT_FRAMEBUFFERS)
    {
        I_GetScreenDimensions();

        // [crispy] re-initialize resolution-agnostic patch drawing
        V_Init();
        I_VideoBuffer = video_buffer_memory;
        V_RestoreBuffer();
        memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT * sizeof(*I_VideoBuffer));
    }

    if (initialized)
    {
        MakeFrame();
    }
}

// [crispy] Each frame waits for the display's vertical blank (xsdk_present_open's swap interval of 1).
void I_ToggleVsync(void)
{
}

void I_UpdateFracTic(void) // [crispy]
{
    fractionaltic = I_GetFracRealTime();
}

void I_StartDisplay(void) // [crispy]
{
}

static const float gammalevels[9] =
{
    // Darker
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f,
};

// [crispy] 9 darker gamma levels, the 5 original ones, and 4 between them.
void I_SetGammaTable(void)
{
    int i, j, k;

    for (i = 0; i < 9; ++i)
    {
        for (j = 0; j < 256; ++j)
        {
            gamma2table[i][j] = (byte) (pow(j / 255.0, 1.0 / gammalevels[i]) * 255.0 + 0.5);
        }
    }

    for (i = 9, k = 0; i < 18 && k < 5; i += 2, k++)
    {
        memcpy(gamma2table[i], gammatable[k], 256);
    }

    for (i = 10, k = 0; i < 18 && k < 4; i += 2, k++)
    {
        for (j = 0; j < 256; j++)
        {
            gamma2table[i][j] = (gammatable[k][j] + gammatable[k + 1][j]) / 2;
        }
    }
}

void I_BindVideoVariables(void)
{
    M_BindIntVariable("use_mouse",                 &usemouse);
    M_BindIntVariable("fullscreen",                &fullscreen);
    M_BindIntVariable("video_display",             &video_display);
    M_BindIntVariable("aspect_ratio_correct",      &aspect_ratio_correct);
    M_BindIntVariable("integer_scaling",           &integer_scaling);
    M_BindIntVariable("smooth_pixel_scaling",      &smooth_pixel_scaling);
    M_BindIntVariable("vga_porch_flash",           &vga_porch_flash);
    M_BindIntVariable("startup_delay",             &startup_delay);
    M_BindIntVariable("fullscreen_width",          &fullscreen_width);
    M_BindIntVariable("fullscreen_height",         &fullscreen_height);
    M_BindIntVariable("force_software_renderer",   &force_software_renderer);
    M_BindIntVariable("max_scaling_buffer_pixels", &max_scaling_buffer_pixels);
    M_BindIntVariable("window_width",              &window_width);
    M_BindIntVariable("window_height",             &window_height);
    M_BindIntVariable("grabmouse",                 &grabmouse);
    M_BindStringVariable("video_driver",           &video_driver);
    M_BindStringVariable("window_position",        &window_position);
    M_BindIntVariable("usegamma",                  &usegamma);
    M_BindIntVariable("png_screenshots",           &png_screenshots);
}
