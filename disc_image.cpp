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
#include "values.h"

#include "third_party/posix_file.h"

#if DEBUG_CUE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

static inline int msfToSector(int mm, int ss, int ff)
{
    return (mm * 60 * 75 + ss * 75 + ff);
}

static inline int toBCD(int in)
{
    if (in > 99)
    {
        return 0x99;
    }
    else
    {
        return (in / 10) << 4 | (in % 10);
    }
}

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
    if (sector < c_leadIn)
    {
        const int point = ((sector / 3) % (3 + m_cueDisc.trackCount)) + 1; // TOC entries are repeated 3 times

        if (point <= m_cueDisc.trackCount) // TOC Entries
        {
            int logical_track = point;
            if (logical_track == 1) // 2 sec pause track
            {
                sector_track = c_preGap;
            }
            else
            {
                sector_track = m_cueDisc.tracks[logical_track].fileOffset - c_leadIn;
            }
            subqdata->ctrladdr = (m_cueDisc.tracks[logical_track].trackType == CueTrackType::TRACK_TYPE_DATA) ? 0x41 : 0x01;
            subqdata->tno = 0x00;
            subqdata->x = toBCD(logical_track);
            subqdata->pmin = toBCD(sector_track / 75 / 60);
            subqdata->psec = toBCD((sector_track / 75) % 60);
            subqdata->pframe = toBCD(sector_track % 75);
        }
        else if (point == m_cueDisc.trackCount + 1) // A0
        {
            subqdata->ctrladdr = m_hasData ? 0x41 : 0x01;
            subqdata->tno = 0x00;
            subqdata->point = 0xA0;
            subqdata->pmin = 0x01;
            subqdata->psec = m_hasData ? 0x20 : 0x00; // 0 = audio, 20 = CDROM-XA
            subqdata->pframe = 0x00;
        }
        else if (point == m_cueDisc.trackCount + 2) // A1
        {
            subqdata->ctrladdr = m_hasData ? 0x41 : 0x01;
            subqdata->tno = 0x00;
            subqdata->point = 0xA1;
            subqdata->pmin = toBCD(m_cueDisc.trackCount);
            subqdata->psec = 0x00;
            subqdata->pframe = 0x00;
        }
        else if (point == m_cueDisc.trackCount + 3) // A2
        {
            const int sector_lead_out = m_cueDisc.tracks[m_cueDisc.trackCount + 1].fileOffset - c_leadIn;
            subqdata->ctrladdr = m_hasData ? 0x41 : 0x01;
            subqdata->tno = 0x00;
            subqdata->point = 0xA2;
            subqdata->pmin = toBCD(sector_lead_out / 75 / 60);
            subqdata->psec = toBCD((sector_lead_out / 75) % 60);
            subqdata->pframe = toBCD(sector_lead_out % 75);
        }

        subqdata->min = toBCD(sector / 75 / 60);
        subqdata->sec = toBCD((sector / 75) % 60);
        subqdata->frame = toBCD(sector % 75);
        subqdata->zero = 0x00;
        subqdata->crc = 0x00;
    }
    else // Program area + lead-out
    {
        int logical_track = m_cueDisc.trackCount + 1; // in case seek overshoots past end of disc
        for (int i = 0; i < m_cueDisc.trackCount + 2; i++)
        { // + 2 for lead in & lead out
            if (m_cueDisc.tracks[i + 1].fileOffset > sector)
            {
                logical_track = i;
                break;
            }
        }
        sector_track = sector - m_cueDisc.tracks[logical_track].fileOffset;
        const int sector_abs = (sector - c_leadIn);
        m_currentLogicalTrack = logical_track;

        subqdata->ctrladdr = (m_cueDisc.tracks[logical_track].trackType == CueTrackType::TRACK_TYPE_DATA) ? 0x41 : 0x01;

        if (logical_track == m_cueDisc.trackCount + 1)
        {
            subqdata->tno = 0xAA; // Lead-out track
        }
        else
        {
            subqdata->tno = toBCD(logical_track); // Track numbers
        }
        if (sector_track < c_preGap && logical_track == 1)
        { // 2 sec pause track
            subqdata->x = 0x00;
            subqdata->min = 0x00;                              // min
            subqdata->sec = toBCD(1 - (sector_track / 75));    // sec (count down)
            subqdata->frame = toBCD(74 - (sector_track % 75)); // frame (count down)
        }
        else
        {
            const int sector_track_after_pause = (logical_track == 1) ? sector_track - c_preGap : sector_track;

            subqdata->x = 0x01;
            subqdata->min = toBCD(sector_track_after_pause / 75 / 60);
            subqdata->sec = toBCD((sector_track_after_pause / 75) % 60);
            subqdata->frame = toBCD(sector_track_after_pause % 75);
        }
        subqdata->zero = 0x00;
        subqdata->amin = toBCD(sector_abs / 75 / 60);
        subqdata->asec = toBCD((sector_abs / 75) % 60);
        subqdata->aframe = toBCD(sector_abs % 75);
        subqdata->crc = ((sector % 2) == 0) ? 0x00 : 0x80;
    }
}

// WIP
/*FRESULT picostation::DiscImage::loadv2(const TCHAR *targetCue)
{
    int logical_track = 0;
    m_cueDisc.trackCount = 0;
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
}*/

struct Context
{
    TCHAR parentPath[128];
};

static void close_cb(struct CueParser *parser, struct CueScheduler *scheduler, const char *error)
{
    if (error)
    {
        printf("Error closing cue parser: %s\n", error);
    }
}

static void size_cb(struct CueFile *file, struct CueScheduler *scheduler, uint64_t size)
{
    printf("File size: %zu\n", size);
}

static void parser_cb(struct CueParser *parser, struct CueScheduler *scheduler, const char *error)
{
    if (error)
    {
        printf("parser error: %s\n", error);
    }
}

static struct CueFile *fileopen(struct CueFile *file, struct CueScheduler *scheduler, const char *filename)
{
    Context *context = reinterpret_cast<Context *>(scheduler->opaque);
    TCHAR fullpath[256];
    strcpy(fullpath, context->parentPath);
    strcat(fullpath, "/");
    strcat(fullpath, filename);
    return create_posix_file(file, fullpath, "r");
}

FRESULT picostation::DiscImage::load(const TCHAR *targetCue, const TCHAR *targetBin)
{

    struct CueScheduler scheduler;
    Scheduler_construct(&scheduler);
    Context context;
    getParentPath(targetCue, context.parentPath);
    scheduler.opaque = &context;

    struct CueFile cue;
    struct CueParser parser;
    // struct CueDisc disc;

    if (!create_posix_file(&cue, targetCue, "r"))
    {
        printf("create_posix_file failed for: %s.\n", targetCue);
    }
    cue.cfilename = targetCue;
    CueParser_construct(&parser, &m_cueDisc);
    CueParser_parse(&parser, &cue, &scheduler, fileopen, parser_cb);
    Scheduler_run(&scheduler);
    CueParser_close(&parser, &scheduler, close_cb);

    m_hasData = true;

    printf("Disc track count: %d\n", m_cueDisc.trackCount);
    for(int i = 0; i < m_cueDisc.trackCount; i++)
    {
        printf("Track %d: Offset %d\n", i, m_cueDisc.tracks[i].fileOffset);
        printf ("Track %d: Type %d\n", i, m_cueDisc.tracks[i].trackType);
    }

    /*
        char buf[128];
        int logical_track = 0;
        m_cueDisc.trackCount = 0;
        FRESULT fr;

        TCHAR parentPath[128];

        if (&m_fil)
        {
            f_close(&m_fil);
        }

        DEBUG_PRINT("Opening files: cue:'%s'\n", targetCue);

        fr = f_open(&m_fil, targetCue, FA_READ); // Open cue sheet
        if (fr == FR_OK)
        {
            f_gets(buf, 128, &m_fil);

            while (1)
            {
                f_gets(buf, 128, &m_fil);
                char *token = strtok(buf, " ");
                if (strcmp("TRACK", token) == 0)
                {
                    m_cueDisc.trackCount++;
                }
                if (f_eof(&m_fil))
                {
                    break;
                }
            }

            f_rewind(&m_fil);
            DEBUG_PRINT("Logical tracks: %d\n", m_cueDisc.trackCount);

            m_cueDisc.tracks[0].fileOffset = 0;        // Lead-in / TOC
            m_cueDisc.tracks[1].fileOffset = c_leadIn; // 4500-4650 - 2 sec pre-gap

            f_gets(buf, 128, &m_fil);
            while (1)
            {
                f_gets(buf, 128, &m_fil);

                if (f_eof(&m_fil))
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
                m_cueDisc.tracks[logical_track].trackType = strcmp("AUDIO", token) ? CueTrackType::TRACK_TYPE_DATA : CueTrackType::TRACK_TYPE_AUDIO;
                if (m_cueDisc.tracks[logical_track].trackType == CueTrackType::TRACK_TYPE_DATA)
                {
                    m_hasData = true;
                }
                f_gets(buf, 128, &m_fil);
                token = strtok(buf, " ");
                token = strtok(NULL, " ");
                token = strtok(NULL, " ");

                int mm = atoi(strtok(token, ":"));
                int ss = atoi(strtok(NULL, ":"));
                int ff = atoi(strtok(NULL, ":"));
                if (logical_track != 1)
                {
                    m_cueDisc.tracks[logical_track].fileOffset = msfToSector(mm, ss, ff) + c_leadIn + c_preGap;
                }
                DEBUG_PRINT("cue: %d %d %d %d\n", logical_track, mm, ss, ff);
            }

            f_close(&m_fil);
            fr = f_open(&m_fil, targetBin, FA_READ);
            if (FR_OK != fr && FR_EXIST != fr)
                panic("f_open(%s) %s error: (%d)\n", targetBin, FRESULT_str(fr), fr);

            m_cueDisc.tracks[0].trackType = CueTrackType::TRACK_TYPE_UNKNOWN;
            m_cueDisc.tracks[m_cueDisc.trackCount + 1].fileOffset = (f_size(&m_fil) / 2352) + c_leadIn + c_preGap; // Lead-out
            m_cueDisc.tracks[m_cueDisc.trackCount + 1].trackType = CueTrackType::TRACK_TYPE_UNKNOWN;

            for (int i = 1; i <= m_cueDisc.trackCount; i++)
            {
                DEBUG_PRINT("sector_t: track: %d %d data: %d\n", i, m_cueDisc.tracks[i].fileOffset, m_cueDisc.tracks[i].trackType == CueTrackType::TRACK_TYPE_DATA);
            }
        }
        else
        {
            panic("f_open(%s) %s error: (%d)\n", targetCue, FRESULT_str(fr), fr);
        }*/

    return FR_OK;
}

void picostation::DiscImage::readData(void *buffer, int sector, int count)
{
    FRESULT fr;
    UINT br;
    const uint64_t seekBytes = (sector - (c_leadIn + c_preGap)) * 2352LL;
    if (seekBytes >= 0)
    {
        fr = f_lseek((FIL *)m_cueDisc.tracks[1].file->opaque, seekBytes);
        if (FR_OK != fr)
        {
            f_rewind((FIL *)m_cueDisc.tracks[1].file->opaque);
            panic("f_lseek(%s) error: (%d)\n", FRESULT_str(fr), fr);
        }
    }

    fr = f_read((FIL *)m_cueDisc.tracks[1].file->opaque, buffer, c_cdSamplesBytes, &br);
    if (FR_OK != fr)
    {
        panic("f_read(%s) error: (%d)\n", FRESULT_str(fr), fr);
    }
}