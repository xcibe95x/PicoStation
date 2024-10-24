#include "disc_image.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "ff.h"
#include "f_util.h"

#include "logging.h"
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

void picostation::DiscImage::generateSubQ(uint8_t *subqdata, int sector)
{
    // Sub-Q data format:
    // 0: CONTROL[4], ADR[4]
    // 1: TNO/Track number[8] BCD
    // DATA-Q:
    //
    // Mode 1: ADR = 1
    // 2: X[8] BCD
    // 3: MIN[8] BCD
    // 4: SEC[8] BCD
    // 5: FRAME[8] BCD
    // 6: ZERO[8]
    // 7: AMIN[8] BCD
    // 8: ASEC[8] BCD
    // 9: AFRAME[8] BCD
    // 10-11: CRC[16]

    int sector_track;

    if (sector < 4500)
    {
        const int subq_entry = sector % (3 + m_numLogicalTracks);

        if (subq_entry == 0)
        {
            subqdata[0] = m_hasData ? 0x61 : 0x21;
            subqdata[1] = 0x00;
            subqdata[2] = 0xA0; // PMIN = TNO FIRST, PSEC = 0?, PFRAME = 0
            subqdata[7] = 0x01; // pmin
            subqdata[8] = 0x20; // psec
            subqdata[9] = 0x00; // pframe
        }
        else if (subq_entry == 1)
        {
            subqdata[0] = m_hasData ? 0x61 : 0x21;
            subqdata[1] = 0x00;
            subqdata[2] = 0xA1;                      // PMIN = TNO LAST, PSEC = 0, PFRAME = 0
            subqdata[7] = toBCD(m_numLogicalTracks); // pmin
            subqdata[8] = 0x00;                      // psec
            subqdata[9] = 0x00;                      // pframe
        }
        else if (subq_entry == 2)
        {
            const int sector_lead_out = m_logicalTrackToSector[m_numLogicalTracks + 1] - 4500;
            subqdata[0] = m_hasData ? 0x61 : 0x21;
            subqdata[1] = 0x00;
            subqdata[2] = 0xA2;                               // PMIN = PSEC = PFRAME = Lead-out
            subqdata[7] = toBCD(sector_lead_out / 75 / 60);   // pmin
            subqdata[8] = toBCD((sector_lead_out / 75) % 60); // psec
            subqdata[9] = toBCD(sector_lead_out % 75);        // pframe
        }
        else if (subq_entry > 2)
        {
            int logical_track = subq_entry - 2;
            if (logical_track == 1)
            {
                sector_track = 150;
            }
            else
            {
                sector_track = m_logicalTrackToSector[logical_track] - 4500;
            }
            subqdata[0] = m_isDataTrack[logical_track] ? 0x41 : 0x01;
            subqdata[1] = 0x00;
            subqdata[2] = toBCD(logical_track);            // Track numbers
            subqdata[7] = toBCD(sector_track / 75 / 60);   // min
            subqdata[8] = toBCD((sector_track / 75) % 60); // sec
            subqdata[9] = toBCD(sector_track % 75);        // frame
        }

        subqdata[3] = toBCD(sector / 75 / 60);   // min
        subqdata[4] = toBCD((sector / 75) % 60); // sec
        subqdata[5] = toBCD(sector % 75);        // frame
        subqdata[6] = 0x00;
        subqdata[10] = 0x00;
        subqdata[11] = 0x00;
    }
    else
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

        subqdata[0] = m_isDataTrack[logical_track] ? 0x41 : 0x01;

        if (logical_track == m_numLogicalTracks + 1)
        {
            subqdata[1] = 0xAA; // Lead-out track
        }
        else
        {
            subqdata[1] = toBCD(logical_track); // Track numbers
        }
        if (sector_track < 150 && logical_track == 1)
        { // 2 sec pause track
            subqdata[2] = 0x00;
            subqdata[3] = 0x00;                            // min
            subqdata[4] = toBCD(1 - (sector_track / 75));  // sec (count down)
            subqdata[5] = toBCD(74 - (sector_track % 75)); // frame (count down)
        }
        else
        {
            const int sector_track_after_pause = (logical_track == 1) ? sector_track - 150 : sector_track;

            subqdata[2] = 0x01;
            subqdata[3] = toBCD(sector_track_after_pause / 75 / 60);   // min
            subqdata[4] = toBCD((sector_track_after_pause / 75) % 60); // sec
            subqdata[5] = toBCD(sector_track_after_pause % 75);        // frame
        }
        subqdata[6] = 0x00;
        subqdata[7] = toBCD(sector_abs / 75 / 60);   // amin
        subqdata[8] = toBCD((sector_abs / 75) % 60); // asec
        subqdata[9] = toBCD(sector_abs % 75);        // aframe
        subqdata[10] = 0x00;
        subqdata[11] = ((sector % 2) == 0) ? 0x00 : 0x80;
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
                    bool isEOL = (c == '\r' ) || (c == '\n');
                    bool isSpace = isEOW && !isEOL;
                    if (!isEOW && !inQuotes) {
                        if(keywordIndex < 128)
                        {
                            keyword[keywordIndex++] = c;
                        }
                    }
                    else{
                        printf("Keyword: %s\n", keyword);
                        memset(keyword, 0, 128);
                        keyword[keywordIndex] = 0;
                        keywordIndex = 0;
                    }



                    if(inQuotes)
                    {
                        if(c == '"')
                        {
                            inQuotes = false;
                            afterQuotes = true;
                            printf("Quotes: %s\n", word);
                        }
                        else
                        {
                            if(wordIndex < 128)
                            {
                                word[wordIndex++] = c;
                            }
                        }
                    }
                    //DEBUG_PRINT("%c", c);
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

        m_logicalTrackToSector[0] = 0; // 0-4500 Lead-in
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