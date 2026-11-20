// Heretic 360: what the port leaves out, for the code that still calls it.
//
// - Networking: Chocolate Doom's net_client and net_server stay in, for the single-player game runs through them, but
//   there is no network module (net_sdl.c is SDL2_net's), no dedicated server and no waiting-for-players screen.
// - The ENDOOM-style exit screen and the text-mode startup screen (textscreen, SDL2's).

#include <stdio.h>

#include "doomtype.h"
#include "i_endoom.h"
#include "i_system.h"
#include "net_dedicated.h"
#include "net_defs.h"
#include "net_gui.h"
#include "net_sdl.h"
#include "txt_io.h"
#include "txt_main.h"

// net_sdl.c's module, with nothing behind it: it starts neither a client nor a server.

static boolean NoInitClient(void)
{
    return false;
}

static boolean NoInitServer(void)
{
    return false;
}

static void NoSendPacket(net_addr_t *addr, net_packet_t *packet)
{
    (void) addr;
    (void) packet;
}

static boolean NoRecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    (void) addr;
    (void) packet;
    return false;
}

static void NoAddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    (void) addr;
    if (buffer_len > 0)
    {
        buffer[0] = '\0';
    }
}

static void NoFreeAddress(net_addr_t *addr)
{
    (void) addr;
}

static net_addr_t *NoResolveAddress(const char *address)
{
    (void) address;
    return NULL;
}

net_module_t net_sdl_module =
{
    NoInitClient,
    NoInitServer,
    NoSendPacket,
    NoRecvPacket,
    NoAddrToString,
    NoFreeAddress,
    NoResolveAddress,
};

void NET_DedicatedServer(void)
{
    I_Error("Heretic 360 has no dedicated server.");
}

void NET_WaitForLaunch(void)
{
    I_Error("Heretic 360 has no network game.");
}

void I_Endoom(byte *data)
{
    (void) data;
}

// The text-mode startup screen: TXT_Init failing has Heretic start without it.

int TXT_Init(void)
{
    return 0;
}

void TXT_Shutdown(void)
{
}

unsigned char *TXT_GetScreenData(void)
{
    static unsigned char screen[80 * 25 * 2];
    return screen;
}

void TXT_UpdateScreen(void)
{
}

int TXT_GetChar(void)
{
    return -1;
}

void TXT_PutChar(int c)
{
    (void) c;
}

void TXT_Puts(const char *s)
{
    (void) s;
}

void TXT_GotoXY(int x, int y)
{
    (void) x;
    (void) y;
}

void TXT_FGColor(txt_color_t color)
{
    (void) color;
}

void TXT_BGColor(int color, int blinking)
{
    (void) color;
    (void) blinking;
}
