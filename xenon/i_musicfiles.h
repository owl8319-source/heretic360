// Heretic 360: recordings of Heretic's songs, as OGG or MP3 files in a music folder beside the .xex, in place of the
// AdLib music (xenon/i_musicfiles.c; heretic/s_sound.c chooses).

#ifndef I_MUSICFILES_H
#define I_MUSICFILES_H

#include "doomtype.h"

// The setting (Options > More > Music, crispy-heretic.cfg's music_files): the files in place of the AdLib music.
extern int music_files;

// Lists the music folder (once) and starts the decoder; false if there's no sound to play them in.
boolean I_MusicFilesInit(void);
// The file for a song's lump (MUS_E1M1 and so on), as a number for I_MusicFilesPlay; -1 if there's none.
int I_MusicFilesFind(const char *lumpname);
// Plays the file, looping or not; -1 stops the music. False if it can't.
boolean I_MusicFilesPlay(int file, boolean loop);
// Whether a file is playing: false once one not looping is done, or once one couldn't be played (that file then
// counts as missing, so its song plays as AdLib).
boolean I_MusicFilesPlaying(void);
// The game's pause: the music waits where it is.
void I_MusicFilesPause(boolean pause);
// The music volume, 0 to 15.
void I_MusicFilesSetVolume(int volume);

#endif
