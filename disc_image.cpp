#include "disc_image.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "ff.h"
#include "f_util.h"

#include "logging.h"

#if DEBUG_CUE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

FRESULT picostation::DiscImage::load(FIL *fil, const TCHAR *targetCue, const TCHAR *targetBin)
{
    char buf[128];
    int logical_track = 0;
    FRESULT fr;

    if (&fil)
    {
        f_close(fil);
    }

    DEBUG_PRINT("Opening files: cue:'%s' bin:'%s'\n", targetCue, targetBin);

    fr = f_open(fil, targetCue, FA_READ); // Open cue sheet
    if (FR_OK != fr && FR_EXIST != fr)
        panic("f_open(%s) %s error: (%d)\n", targetCue, FRESULT_str(fr), fr);

    f_gets(buf, 128, fil);

    while (1)
    {
        f_gets(buf, 128, fil);
        char *token = strtok(buf, " ");
        if (strcmp("TRACK", token) == 0)
        {
            m_numLogicalTracks++;
        }
        if (f_eof(fil))
        {
            break;
        }
    }

    f_rewind(fil);
    DEBUG_PRINT("Logical tracks: %d\n", m_numLogicalTracks);

    m_logicalTrackToSector[0] = 0;
    m_logicalTrackToSector[1] = 4500;

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
        m_isDataTrack[logical_track] = strcmp("AUDIO", token);
        if (m_isDataTrack[logical_track])
        {
            m_hasData = 1;
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
            m_logicalTrackToSector[logical_track] = mm * 60 * 75 + ss * 75 + ff + 4650;
        }
        DEBUG_PRINT("cue: %d %d %d %d\n", logical_track, mm, ss, ff);
    }

    f_close(fil);
    fr = f_open(fil, targetBin, FA_READ);
    if (FR_OK != fr && FR_EXIST != fr)
        panic("f_open(%s) %s error: (%d)\n", targetBin, FRESULT_str(fr), fr);

    m_logicalTrackToSector[m_numLogicalTracks + 1] = f_size(fil) / 2352 + 4650;
    m_isDataTrack[m_numLogicalTracks + 1] = 0;
    m_isDataTrack[0] = 0;

    for (int i = 0; i < m_numLogicalTracks + 2; i++)
    {
        DEBUG_PRINT("sector_t: %d data: %d\n", m_logicalTrackToSector[i], m_isDataTrack[i]);
    }

    // CUE parsing done

    return fr;
}