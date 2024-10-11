#include "disc_image.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "ff.h"
#include "f_util.h"

#if DEBUG_CUE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

extern volatile bool hasData;
extern bool *is_data_track;
extern int *logical_track_to_sector;
extern volatile int num_logical_tracks;

FRESULT picostation::cueparser::loadImage(FIL *fil, const TCHAR *targetCue, const TCHAR *targetBin)
{
    char buf[128];
    int logical_track = 0;
    
    FRESULT fr;

    // CUE parsing
    // if fil is loaded, close it
    if (&fil)
    {
        f_close(fil);
    }

    printf("Opening files: cue:'%s' bin:'%s'\n", targetCue, targetBin);

    fr = f_open(fil, targetCue, FA_READ); // Open cue sheet
    if (FR_OK != fr && FR_EXIST != fr)
        panic("f_open(%s) %s error: (%d)\n", targetCue, FRESULT_str(fr), fr);

    f_gets(buf, 128, fil);
    char *token_test;
    token_test = strtok(buf, " ");
    if (strcmp("FILE", token_test) == 0)
    {
        // Parse the file name
        token_test = strtok(NULL, " ");
        printf("FILE: %s\n", token_test);
    }

    while (1)
    {
        f_gets(buf, 128, fil);
        char *token = strtok(buf, " ");
        if (strcmp("TRACK", token) == 0)
        {
            num_logical_tracks++;
        }
        if (f_eof(fil))
        {
            break;
        }
    }

    f_rewind(fil);
    DEBUG_PRINT("num_logical_tracks: %d\n", num_logical_tracks);

    logical_track_to_sector = (int*)malloc(sizeof(int) * (num_logical_tracks + 2));
    is_data_track = (bool*)malloc(sizeof(bool) * (num_logical_tracks + 2));
    logical_track_to_sector[0] = 0;
    logical_track_to_sector[1] = 4500;

    f_gets(buf, 128, fil);
    while (1)
    {
        f_gets(buf, 128, fil);

        if (f_eof(fil))
        {
            break;
        }
        char *token;
        token = strtok(buf, " ");
        if (strcmp("TRACK", token) == 0)
        {
            token = strtok(NULL, " ");
            logical_track = atoi(token);
        }
        token = strtok(NULL, " ");
        token[strcspn(token, "\r\n")] = 0;
        is_data_track[logical_track] = strcmp("AUDIO", token);
        if (is_data_track[logical_track])
        {
            hasData = 1;
        }
        f_gets(buf, 128, fil);
        token = strtok(buf, " ");
        token = strtok(NULL, " ");
        token = strtok(NULL, " ");

        int mm = atoi(strtok(token, ":"));
        int ss = atoi(strtok(NULL, ":"));
        int ff = atoi(strtok(NULL, ":"));
        if (logical_track != 1)
        {
            logical_track_to_sector[logical_track] = mm * 60 * 75 + ss * 75 + ff + 4650;
        }
        DEBUG_PRINT("cue: %d %d %d %d\n", logical_track, mm, ss, ff);
    }

    f_close(fil);
    fr = f_open(fil, targetBin, FA_READ);
    if (FR_OK != fr && FR_EXIST != fr)
        panic("f_open(%s) %s error: (%d)\n", targetBin, FRESULT_str(fr), fr);

    logical_track_to_sector[num_logical_tracks + 1] = f_size(fil) / 2352 + 4650;
    is_data_track[num_logical_tracks + 1] = 0;
    is_data_track[0] = 0;

    for (int i = 0; i < num_logical_tracks + 2; i++)
    {
        DEBUG_PRINT("sector_t: %d data: %d\n", logical_track_to_sector[i], is_data_track[i]);
    }

    // CUE parsing done

    return fr;
}