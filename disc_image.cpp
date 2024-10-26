#include "disc_image.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "ff.h"
#include "f_util.h"

#include "logging.h"
#include "subq.h"
#include "utils.h"

#if DEBUG_CUE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

static void getParentPath(const TCHAR *path, TCHAR *parentPath)
{
    strcpy(parentPath, path);
    char *lastSlash = strrchr(parentPath, '/');
    char *lastBackslash = strrchr(parentPath, '\\');
    if (lastBackslash && (!lastSlash || lastBackslash > lastSlash))
    {
        lastSlash = lastBackslash;
    }
    if (lastSlash)
    {
        *lastSlash = 0;
    }
    else
    {
        parentPath[0] = 0;
    }
}

void picostation::DiscImage::generateSubQ(SubQ *subqdata, int sector)
{
    int sector_track;

    // Lead-in
    if (sector < 4500)
    {
        const int point = ((sector / 3) % (3 + m_numLogicalTracks)) + 1;

        if (point <= m_numLogicalTracks) // TOC Entries
        {
            int logical_track = point;
            if (logical_track == 1) // 2 sec pause track
            {
                sector_track = 150;
            }
            else
            {
                sector_track = m_logicalTrackToSector[logical_track] - 4500;
            }
            subqdata->ctrladdr = m_isDataTrack[logical_track] ? 0x41 : 0x01;
            subqdata->tno = 0x00;
            subqdata->x = toBCD(logical_track);               // Track numbers
            subqdata->pmin = toBCD(sector_track / 75 / 60);   // min
            subqdata->psec = toBCD((sector_track / 75) % 60); // sec
            subqdata->pframe = toBCD(sector_track % 75);      // frame
        }
        else if (point == m_numLogicalTracks + 1) // A0
        {
            subqdata->ctrladdr = m_hasData ? 0x61 : 0x21;
            subqdata->tno = 0x00;    // TNO = 0
            subqdata->point = 0xA0;  // PMIN = TNO FIRST, PSEC = 0?, PFRAME = 0
            subqdata->pmin = 0x01;   // pmin
            subqdata->psec = 0x20;   // psec
            subqdata->pframe = 0x00; // pframe
        }
        else if (point == m_numLogicalTracks + 2) // A1
        {
            subqdata->ctrladdr = m_hasData ? 0x61 : 0x21;
            subqdata->tno = 0x00;
            subqdata->point = 0xA1;                     // PMIN = TNO LAST, PSEC = 0, PFRAME = 0
            subqdata->pmin = toBCD(m_numLogicalTracks); // pmin
            subqdata->psec = 0x00;                      // psec
            subqdata->pframe = 0x00;                    // pframe
        }
        else if (point == m_numLogicalTracks + 3) // A2
        {
            const int sector_lead_out = m_logicalTrackToSector[m_numLogicalTracks + 1] - 4500;
            subqdata->ctrladdr = m_hasData ? 0x61 : 0x21;
            subqdata->tno = 0x00;
            subqdata->point = 0xA2;                              // PMIN = PSEC = PFRAME = Lead-out
            subqdata->pmin = toBCD(sector_lead_out / 75 / 60);   // pmin
            subqdata->psec = toBCD((sector_lead_out / 75) % 60); // psec
            subqdata->pframe = toBCD(sector_lead_out % 75);      // pframe
        }

        subqdata->min = toBCD(sector / 75 / 60);   // min
        subqdata->sec = toBCD((sector / 75) % 60); // sec
        subqdata->frame = toBCD(sector % 75);      // frame
        subqdata->zero = 0x00;                     // ZERO
        subqdata->crc = 0x00;                      // CRC
    }
    else // Program area + lead-out
    {
        int logical_track = m_numLogicalTracks + 1; // in case seek overshoots past end of disc
        for (int i = 0; i < m_numLogicalTracks + 2; i++)
        { // + 2 for lead in & lead out
            if (m_logicalTrackToSector[i + 1] > sector)
            {
                logical_track = i;
                break;
            }
        }
        sector_track = sector - m_logicalTrackToSector[logical_track];
        const int sector_abs = (sector - 4500);
        m_currentLogicalTrack = logical_track;

        subqdata->ctrladdr = m_isDataTrack[logical_track] ? 0x41 : 0x01;

        if (logical_track == m_numLogicalTracks + 1)
        {
            subqdata->tno = 0xAA; // Lead-out track
        }
        else
        {
            subqdata->tno = toBCD(logical_track); // Track numbers
        }
        if (sector_track < 150 && logical_track == 1)
        { // 2 sec pause track
            subqdata->x = 0x00;
            subqdata->min = 0x00;                              // min
            subqdata->sec = toBCD(1 - (sector_track / 75));    // sec (count down)
            subqdata->frame = toBCD(74 - (sector_track % 75)); // frame (count down)
        }
        else
        {
            const int sector_track_after_pause = (logical_track == 1) ? sector_track - 150 : sector_track;

            subqdata->x = 0x01;
            subqdata->min = toBCD(sector_track_after_pause / 75 / 60);   // min
            subqdata->sec = toBCD((sector_track_after_pause / 75) % 60); // sec
            subqdata->frame = toBCD(sector_track_after_pause % 75);      // frame
        }
        subqdata->zero = 0x00;
        subqdata->amin = toBCD(sector_abs / 75 / 60);   // amin
        subqdata->asec = toBCD((sector_abs / 75) % 60); // asec
        subqdata->aframe = toBCD(sector_abs % 75);      // aframe
        subqdata->crc = ((sector % 2) == 0) ? 0x00 : 0x80;
    }
}

// WIP
FRESULT picostation::DiscImage::loadv2(const TCHAR *targetCue)
{
    int logical_track = 0;
    m_numLogicalTracks = 0;
    FIL fil = {0};
    FRESULT fr;

    TCHAR parentPath[128];

    if (&fil)
    {
        f_close(&fil);
    }

    DEBUG_PRINT("Opening files: cue:'%s'\n", targetCue);

    fr = f_open(&fil, targetCue, FA_READ); // Open cue sheet
    if (fr == FR_OK)
    {
        // Get filesize
        int filesize = f_size(&fil);
        UINT br;
        bool fEOF = false;
        char c;
        char keyword[128] = {0};
        int keywordIndex = 0;
        char word[128] = {0};
        int wordIndex = 0;

        bool inQuotes = false;
        bool afterQuotes = true;

        if (filesize > 0)
        {
            DEBUG_PRINT("Filesize: %d\n", filesize);
            {
                while (f_read(&fil, &c, 1, &br) == FR_OK && !fEOF)
                {
                    fEOF = f_eof(&fil);
                    bool isEOW = isspace(c);
                    bool isEOL = (c == '\r') || (c == '\n');
                    bool isSpace = isEOW && !isEOL;
                    if (!isEOW && !inQuotes)
                    {
                        if (keywordIndex < 128)
                        {
                            keyword[keywordIndex++] = c;
                        }
                    }
                    else
                    {
                        printf("Keyword: %s\n", keyword);
                        memset(keyword, 0, 128);
                        keyword[keywordIndex] = 0;
                        keywordIndex = 0;
                    }

                    if (inQuotes)
                    {
                        if (c == '"')
                        {
                            inQuotes = false;
                            afterQuotes = true;
                            printf("Quotes: %s\n", word);
                        }
                        else
                        {
                            if (wordIndex < 128)
                            {
                                word[wordIndex++] = c;
                            }
                        }
                    }
                    // DEBUG_PRINT("%c", c);
                }
            }
        }

        f_close(&fil);

        getParentPath(targetCue, parentPath);
        DEBUG_PRINT("Parent path: %s\n", parentPath);
    }

    return fr;
}

FRESULT picostation::DiscImage::load(FIL *fil, const TCHAR *targetCue, const TCHAR *targetBin)
{
    char buf[128];
    int logical_track = 0;
    m_numLogicalTracks = 0;
    FRESULT fr;

    TCHAR parentPath[128];

    if (&fil)
    {
        f_close(fil);
    }

    DEBUG_PRINT("Opening files: cue:'%s'\n", targetCue);

    fr = f_open(fil, targetCue, FA_READ); // Open cue sheet
    if (fr == FR_OK)
    {
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

        m_logicalTrackToSector[0] = 0;    // 0-4500 Lead-in
        m_logicalTrackToSector[1] = 4500; // 4500-4650 - 2 sec pre-gap

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
                m_hasData = true;
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
            DEBUG_PRINT("sector_t: track: %d %d data: %d\n", i, m_logicalTrackToSector[i], m_isDataTrack[i]);
        }
    }
    else
    {
        panic("f_open(%s) %s error: (%d)\n", targetCue, FRESULT_str(fr), fr);
    }

    return fr;
}