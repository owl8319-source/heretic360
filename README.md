# Heretic 360

Heretic: Shadow of the Serpent Riders on an RGH, JTAG or Bad Avatar Xbox 360, from Chocolate Doom 3.1.1's Heretic,
built with [xsdk](https://github.com/xenosworks/xsdk) and [SDL-xenon](https://github.com/xenosworks/SDL-xenon).

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

Launch `heretic.xex` from Aurora's file browser. The settings (`heretic.cfg`, `chocolate-heretic.cfg`) and saves
(`hticsav0.hsg` to `hticsav5.hsg`) are written beside it. The log, `heretic.log`, is too:

```
curl.exe ftp://<console>/Hdd1/Homebrew/Heretic/heretic.log --user '<user>:<password>'
```

## Controls

| In the game | | In the menus | |
|---|---|---|---|
| Left stick | move and strafe | Left stick or D-pad | choose |
| Right stick | turn and look up or down | A | select (yes, on the quit question) |
| RT | fire | B | back (no, on the quit question) |
| LT or A | use (doors, switches) | Start | open the menu |
| LB / RB | previous / next weapon | | |
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
| Frame-Rate Counter (on / off) | `ticker` |

D-pad or left stick chooses, A selects, B closes. It isn't in the main menu or the options.

## What this version does and doesn't do yet

- **Does:** the whole game, 4:3 at 720p in the middle of the screen with black bars, sharp pixels; sound effects;
  music, as Heretic sounded on a Sound Blaster (AdLib OPL, emulated by Nuked OPL3); the controller; settings and saves;
  a developer cheat menu.
- **Not yet:** true widescreen (Crispy Heretic, later).

## Layout

- `src/` is Chocolate Doom 3.1.1's code, the files Heretic needs.
- `art/` holds the controller picture for the INFO page (`360_controller.svg`, CC BY-SA 3.0; its credit is in
  `art/README.md`).
- `tools/` has `make_controls_art.py`, which turns that picture into `src/heretic/info360_art.h` in Heretic's
  palette (needs Python 3, Pillow and `rsvg-convert`).
- `opl/` is Chocolate Doom's OPL library, for the music: the chip (Nuked OPL3, `opl3.c`) and its timing.
- `xenon/` is the Xbox 360's side: video through xsdk's GPU layer (`i_video.c`, `blit.vert`, `blit.frag`), the
  controller and sound through SDL3 (`i_joystick.c`, `i_sound.c`), the timer, system and start (`i_timer.c`,
  `i_system.c`, `i_main.c`), headers that replace Chocolate Doom's SDL2 ones, and stubs for what's left out
  (`stubs.c`: networking, the text-mode startup screen and the exit screen). `opl_xsdk.c` runs the music's OPL chip
  inside the sound mixer, in place of Chocolate Doom's `opl_sdl.c`, which needs SDL2_mixer.

### What changed in Chocolate Doom's code

- `src/m_config.c`: `GetDefaultConfigDir` keeps the settings beside the `.xex` (the `HERETIC360` define), as Chocolate
  Doom does on Windows, not in a folder of SDL's.
- `src/aes_prng.c`: `SDL_SwapLE32` is SDL3's `SDL_Swap32LE`.
- `src/w_file.c`: `W_OpenFile` reads the whole WAD into memory first (`xenon/w_file_memory.c`), so play never waits
  on the hard drive.
- `src/midifile.c`: SDL2's `SDL_SwapBE16`/`SDL_SwapBE32` are SDL3's `SDL_Swap16BE`/`SDL_Swap32BE`; `MIDI_LoadMemory`
  reads a song from memory (`fmemopen`).
- `src/i_oplmusic.c`: `I_OPL_RegisterSong` converts and reads the song in memory, a temporary file only if that fails.
- `src/m_misc.c`: `M_TempFile` puts temporary files in `game:`, the console having no `/tmp`.
- `src/heretic/g_game.c`: logs how long each level takes to load.
- `src/heretic/devmenu.c` (new): the developer cheat menu.
- `src/heretic/sb_bar.c`: `SB_DevCheat` runs the typed cheats' own functions for the cheat menu; `SB_DevTickerOn`.
- `src/heretic/doomdef.h`: declares the cheat menu, those two and the controls page.
- `src/heretic/d_main.c`: the cheat menu takes events first and draws last; the PAUSED graphic hides under it.
- `src/v_video.c`: `V_DrawPatch` and `V_DrawPatchFlipped` skip a patch off the screen and log it, in place of
  Chocolate Doom's `I_Error`; `V_SetDrawText` (`v_video.h`) names the menu text being drawn.
- `src/heretic/mn_menu.c`: `MN_DrTextA` and `MN_DrTextB` name their text for that log line; the save and load
  screens' slot names are read once at startup, not each time a screen opens; INFO's first page is the controls
  page and the PC keyboard pages are skipped.
- `src/heretic/info360.c` (new): the INFO controls page. Its controller picture, `info360_art.h`, is made by
  `tools/make_controls_art.py` from `art/360_controller.svg`.
- `opl/opl.c`: SDL3's mutex and condition names; its only driver is `opl_xsdk_driver`.
- `opl/opl_internal.h`: declares `opl_xsdk_driver` in place of `opl_sdl_driver`.

## Changes

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

GPL 2 or later, as Chocolate Doom (`COPYING.md`). Heretic's WAD is id Software and Raven Software's, not covered. The controller picture
(`art/360_controller.svg` and `src/heretic/info360_art.h`) is CC BY-SA 3.0, after Alphathon; see `art/README.md`.
