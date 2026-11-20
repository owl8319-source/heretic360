# Heretic 360

Heretic: Shadow of the Serpent Riders on an RGH, JTAG or Bad Avatar Xbox 360, from
[Crispy Doom](https://github.com/fabiangreffrath/crispy-doom) 7.2's Heretic, built with
[xsdk](https://github.com/xenosworks/xsdk) and [SDL-xenon](https://github.com/xenosworks/SDL-xenon): true widescreen,
double resolution (640x400), an uncapped frame rate and Crispy's other options, on the 360 controller.

Versions up to 0.7 were built on Chocolate Doom 3.1.1, closer to the original; they're on the `chocolate` tag.

You need your own `heretic.wad` (v1.3, Shadow of the Serpent Riders). It isn't included.

## Building (WSL Debian)

With xsdk built at `~/xsdk` and SDL-xenon built at `~/SDL-xenon/build-xsdk`, from this folder:

```
./build.sh
```

That makes `build/heretic.xex`.

## Installing

Put `heretic.xex` and your `heretic.wad` in one folder on the console, such as `Hdd1:\Homebrew\Heretic\`. From
PowerShell, with the console's address and Aurora's FTP login:

```
curl.exe -T \\wsl.localhost\Debian\home\<you>\heretic360\build\heretic.xex --ftp-create-dirs ftp://<console>/Hdd1/Homebrew/Heretic/ --user '<user>:<password>'
curl.exe -T C:\path\to\heretic.wad ftp://<console>/Hdd1/Homebrew/Heretic/ --user '<user>:<password>'
```

Launch `heretic.xex` from Aurora's file browser. The settings (`heretic.cfg`, `crispy-heretic.cfg`), the saves
(`hticsav0.hsg` and on, six to a page; saves from the Chocolate versions are on the first page) and the log,
`heretic.log`, are written beside it:

```
curl.exe ftp://<console>/Hdd1/Homebrew/Heretic/heretic.log --user '<user>:<password>'
```

## Controls

| In the game | | In the menus | |
|---|---|---|---|
| Left stick | move and strafe | Left stick or D-pad | choose; left / right change an option |
| Right stick | turn and look up or down | A | select (yes, on the quit question) |
| RT | fire | B | back (no, on the quit question) |
| LT or A | use (doors, switches) | Start | open the menu |
| LB / RB | previous / next weapon | LB / RB | previous / next page (saves, Crispness) |
| D-pad left / right | move through the inventory | | |
| D-pad up or X | use the chosen artifact | | |
| Y / B | fly up / down (Wings of Wrath) | | |
| Right stick click | centre the view, stop flying | | |
| Back | map | | |
| Start | menu | | |

The game always runs; push the stick part way to walk. The main menu's INFO shows these controls on a picture of the
controller, then Raven's credits, in place of the PC's two keyboard pages. The controller picture is Alphathon's
"360 controller.svg" from Wikimedia Commons (CC BY-SA 3.0), changed a little; see `art/README.md`.

### Developer cheat menu

Hold **LB + RB + Back** for about a second during a level. The game pauses and a small menu opens over it with five
cheats, which work on every skill level:

| Cheat | Same as typing |
|---|---|
| God Mode (on / off) | `quicken` |
| All Keys | `skel` |
| All Weapons and Full Ammo | `rambo` |
| Kill Every Monster | `massacre` |
| Frame-Rate Counter (on / off) | `showfps` (Crispy's) |

D-pad or left stick chooses, A selects, B closes. It isn't in the main menu or the options.

## Display

Crispy's options are under **Options > Crispness**. As Crispy starts:

- **High resolution rendering: on.** 640x400, twice Heretic's 320x200 each way. Off is the original 320x200.
- **Aspect ratio: match screen.** True widescreen: the game drawn wider, filling the 16:9 display. **Off** is 4:3
  with black bars either side, as Chocolate Heretic shows it. 16:10 and 21:9 are there too (21:9 with bars above and below).
- **Uncapped frame rate: off.** On, frames are drawn between the game's 35 tics a second, up to the display's 60.
- **Smooth pixel scaling: off,** sharp pixels.

The game is always drawn the right shape on the 720p display (Heretic's 200 lines shown as 240, as on a PC's 4:3
monitor) and scaled to the console's output. If high resolution with widescreen runs slowly, the developer menu's
frame-rate counter shows it; high resolution off is about a quarter of the drawing.

## What this version does and doesn't do yet

- **Does:** the whole game with Crispy's options; sound effects; music, as Heretic sounded on a Sound Blaster (AdLib
  OPL, emulated by Nuked OPL3); the controller; settings and saves; a developer cheat menu.
- **Not yet:** a keyboard for naming saves (they're named after the level).

## Layout

- `src/` is Crispy Doom 7.2's code, the files Heretic needs.
- `art/` holds the controller picture for the INFO page (`360_controller.svg`, CC BY-SA 3.0; its credit is in
  `art/README.md`).
- `tools/` has `make_controls_art.py`, which turns that picture into `src/heretic/info360_art.h` in Heretic's
  palette (needs Python 3, Pillow and `rsvg-convert`).
- `opl/` is Crispy Doom's OPL library, for the music: the chip (Nuked OPL3, `opl3.c`) and its timing.
- `xenon/` is the Xbox 360's side: video through xsdk's GPU layer (`i_video.c`, `blit.vert`, `blit.frag`), the
  controller and sound through SDL3 (`i_joystick.c`, `i_sound.c`), the timer, system and start (`i_timer.c`,
  `i_system.c`, `i_main.c`), headers that replace Crispy Doom's SDL2 ones, and stubs for what's left out
  (`stubs.c`: networking, the text-mode startup screen and the exit screen). `opl_xsdk.c` runs the music's OPL chip
  inside the sound mixer, in place of Crispy Doom's `opl_sdl.c`, which needs SDL2_mixer. `i_video.c` sizes the frame
  as Crispy does (`I_GetScreenDimensions`) and makes its textures again when Crispy's menu changes the resolution or
  the aspect ratio (`I_ReInitGraphics`).

### What changed in Crispy Doom's code

All under the `HERETIC360` define, as in the Chocolate versions.

- `src/m_config.c`: `GetDefaultConfigDir` keeps the settings beside the `.xex`, as Crispy Doom does on Windows, not
  in a folder of SDL's.
- `src/aes_prng.c`: `SDL_SwapLE32` is SDL3's `SDL_Swap32LE`.
- `src/w_file.c`: `W_OpenFile` reads the whole WAD into memory first (`xenon/w_file_memory.c`), so play never waits
  on the hard drive.
- `src/midifile.c`: SDL2's `SDL_SwapBE16`/`SDL_SwapBE32` are SDL3's `SDL_Swap16BE`/`SDL_Swap32BE`; `MIDI_LoadMemory`
  reads a song from memory (`fmemopen`).
- `src/i_oplmusic.c`: `I_OPL_RegisterSong` converts and reads the song in memory, a temporary file only if that fails.
- `src/m_misc.c`: `M_TempFile` puts temporary files in `game:`, the console having no `/tmp`.
- `src/heretic/g_game.c`: logs how long each level takes to load; includes `<math.h>` (a PC build gets it through
  SDL's headers).
- `src/heretic/in_lude.c`: includes `<ctype.h>`, for the same reason.
- `src/heretic/devmenu.c` (new): the developer cheat menu.
- `src/heretic/sb_bar.c`: `SB_DevCheat` runs the typed cheats' own functions for the cheat menu (the frame-rate
  counter is Crispy's `showfps`); `SB_DevTickerOn`.
- `src/heretic/doomdef.h`: declares the cheat menu, those two and the controls page.
- `src/heretic/d_main.c`: the cheat menu takes events first and draws last; the PAUSED graphic hides under it.
- `src/heretic/mn_menu.c`: the save and load screens' slot names are read at startup and when the page changes, not
  each time a screen opens; INFO's first page is the controls page and the PC keyboard pages are skipped; the
  controller's bumpers turn the save and Crispness pages, and the save screen says LB / RB, not PgUp / PgDn.
- `src/heretic/info360.c` (new): the INFO controls page. Its controller picture, `info360_art.h`, is made by
  `tools/make_controls_art.py` from `art/360_controller.svg`.
- `opl/opl.c`: SDL3's mutex and condition names; its only driver is `opl_xsdk_driver`.
- `opl/opl_internal.h`: declares `opl_xsdk_driver` in place of `opl_sdl_driver`.

Crispy draws a patch partly off the screen clipped, so 0.6's check for one (in `src/v_video.c`) is gone.

## Changes

- **0.8:** built on Crispy Doom 7.2 in place of Chocolate Doom 3.1.1: true widescreen, 640x400, an uncapped frame
  rate and the rest of Crispy's options (Options > Crispness), changeable as the game runs; LB / RB turn the save
  screen's and Crispness menu's pages; the developer menu's frame-rate counter is Crispy's, a number; the game's
  memory is 32 MB, as Crispy's.
- **0.7:** the log (`heretic.log`) is written by its own thread, once a second, so the game never waits on the
  hard drive to log a line (the 2.3 s pause starting E1M2); the save and load screens open at once (their slot names
  are read at startup); INFO shows the 360's controls on a picture of the controller (Alphathon's, from Wikimedia
  Commons).
- **0.6:** a patch drawn off the screen is skipped and noted in `heretic.log` (where, its size, the text, the code
  address), not the end of the game: saving ended 0.5 with "Bad V_DrawPatch" on the console, not on a PC.
- **0.5:** the developer cheat menu (LB + RB + Back, held).
- **0.4:** songs are converted and read in memory, not through a temporary file (the console has no `/tmp`, so 0.3
  played none); the log notes how long each level takes to load, and any music call over 50 ms.
- **0.3:** music: Chocolate Doom's OPL (AdLib) player, its chip emulated in the sound mixer.
- **0.2:** the WAD is read into memory at startup (for the short freezes picking up items); a frame shown more than
  150 ms after the one before is noted in `heretic.log` with what it waited on.
- **0.1:** first version: the game, sound effects, the controller.

## Licence

GPL 2 or later, as Crispy Doom and Chocolate Doom (`COPYING.md`). Heretic's WAD is id Software and Raven Software's, not covered. The controller picture
(`art/360_controller.svg` and `src/heretic/info360_art.h`) is CC BY-SA 3.0, after Alphathon; see `art/README.md`.
