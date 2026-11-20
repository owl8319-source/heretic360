// Heretic 360: WAD files read whole into memory when they're opened, so the game never waits on the hard drive while
// it plays. Chocolate Doom reads a lump from the WAD the first time it's needed (a status bar icon, a sound), and on
// the console a read after the drive has been idle can take seconds.
//
// Reads still copy into the lumps' own zone memory, as Chocolate Doom's stdio class does, so lumps keep the zone's
// alignment; only the file is in memory.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

typedef struct
{
    wad_file_t wad;
    byte *data;
} memory_wad_file_t;

extern wad_file_class_t memory_wad_file;

static wad_file_t *W_Memory_OpenFile(const char *path)
{
    memory_wad_file_t *result;
    FILE *fstream;
    long length;
    byte *data;

    fstream = M_fopen(path, "rb");

    if (fstream == NULL)
    {
        return NULL;
    }

    length = M_FileLength(fstream);
    data = malloc(length > 0 ? (size_t) length : 1);

    if (data == NULL || fread(data, 1, (size_t) length, fstream) != (size_t) length)
    {
        // Too big or unreadable: the caller falls back to reading from the file.
        printf("W_Memory_OpenFile: couldn't read %s into memory\n", path);
        free(data);
        fclose(fstream);
        return NULL;
    }

    fclose(fstream);

    result = Z_Malloc(sizeof(memory_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &memory_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = (unsigned int) length;
    result->wad.path = M_StringDuplicate(path);
    result->data = data;

    printf("W_Memory_OpenFile: %s, %ld bytes, in memory\n", path, length);

    return &result->wad;
}

static void W_Memory_CloseFile(wad_file_t *wad)
{
    memory_wad_file_t *memory_wad = (memory_wad_file_t *) wad;

    free(memory_wad->data);
    Z_Free(memory_wad);
}

static size_t W_Memory_Read(wad_file_t *wad, unsigned int offset, void *buffer, size_t buffer_len)
{
    memory_wad_file_t *memory_wad = (memory_wad_file_t *) wad;

    if (offset >= wad->length)
    {
        return 0;
    }

    if (buffer_len > wad->length - offset)
    {
        buffer_len = wad->length - offset;
    }

    memcpy(buffer, memory_wad->data + offset, buffer_len);

    return buffer_len;
}

wad_file_class_t memory_wad_file =
{
    W_Memory_OpenFile,
    W_Memory_CloseFile,
    W_Memory_Read,
};
