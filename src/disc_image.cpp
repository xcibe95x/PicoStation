#include "disc_image.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "logging.h"
#include "picostation.h"
#include "subq.h"
#include "third_party/posix_file.h"
#include "values.h"
#include "global.h"

#if DEBUG_CUE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

extern const uint8_t  loaderImage[];
extern const uint32_t loaderImageSize;

struct MSF
{
    int mm;
    int ss;
    int ff;
};

namespace Submode
{
	enum : uint8_t
	{
		EndOfRecord = 1 << 0,
		Video = 1 << 1,
		Audio = 1 << 2,
		Data = 1 << 3,
		Trigger = 1 << 4,
		Form = 1 << 5,
		RealTime = 1 << 6,
		EndOfFile = 1 << 7,
	};
}

picostation::DiscImage picostation::g_discImage;

static constexpr uint16_t crc16_lut[256] =
{
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad,
    0xe1ce, 0xf1ef, 0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b, 0xa35a,
    0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b,
    0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861,
    0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 0x5af5, 0x4ad4, 0x7ab7, 0x6a96,
    0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a, 0x6ca6, 0x7c87,
    0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
    0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3,
    0x5004, 0x4025, 0x7046, 0x6067, 0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1, 0x1290,
    0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e,
    0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634, 0xd94c, 0xc96d, 0xf90e, 0xe92f,
    0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3, 0xcb7d, 0xdb5c,
    0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83,
    0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
    0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

static uint8_t s_userData[c_cdSamplesBytes] = {0};

static MSF __time_critical_func(sectorToMSF)(const int sector)
{
    MSF msf;
    msf.mm = abs(sector / 75 / 60);
    msf.ss = abs((sector / 75) % 60);
    msf.ff = abs(sector % 75);
    return msf;
}

static inline int toBCD(const int in)
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

static void __time_critical_func(getParentPath)(const TCHAR *path, TCHAR *parentPath)
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

void __time_critical_func(picostation::DiscImage::buildSector)(const int sector, uint32_t *buffer, uint16_t *userData, const uint16_t *scramling)
{
	uint32_t tmp;
	static uint8_t header[24] =
	{
		// Sync - 12 bytes
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
		// Header - 4 bytes
		0, 0, 0, 2,
		// Sub-Header - 8 bytes (Green book)
		0, 0, 0, 0, 0, 0, 0, 0
	};
	
	const MSF msf = sectorToMSF(sector);
	
	header[12] = toBCD(msf.mm);
	header[13] = toBCD(msf.ss);
	header[14] = toBCD(msf.ff);
	
	header[18] = (Submode::Form);
	
	uint16_t *src = (uint16_t *) header;
	
	for (int i = 0; i < 12; i++)
	{
		tmp = (src[i] ^ scramling[i]) << 8;
		
		if (tmp & 0x100)
		{
			tmp |= 0xFF;
		}
		
		buffer[i] = tmp;
	}
    
	for (int i = 12; i < 1174; i++)
	{
		tmp = (((userData) ? *userData++ : 0) ^ scramling[i]) << 8;
		
		if (tmp & 0x100)
		{
			tmp |= 0xFF;
		}
		
		buffer[i] = tmp;
	}

    // EDC/ECC - 4 bytes
    tmp = scramling[1174] << 8;
    if (tmp & 0x100)
    {
		tmp |= 0xFF;
	}
    buffer[1174] = tmp;
    
    tmp = scramling[1175] << 8;
    if (tmp & 0x100)
    {
		tmp |= 0xFF;
	}
	buffer[1175] = tmp;
}

picostation::SubQ::Data __time_critical_func(picostation::DiscImage::generateSubQ)(const int sector)
{
    SubQ::Data subqdata;

    int sector_track;

    if (sector < c_leadIn)  // Lead-in area
    {
        const int point = (((sector - 1) / 3) % (3 + m_cueDisc.trackCount)) + 1;  // TOC entries are repeated 3 times

        if (point <= m_cueDisc.trackCount)  // TOC Entries
        {
            const int logical_track = point;
            if (logical_track == 1)
            {
                // Track 1 has a hardcoded 2 second pre-gap
                sector_track = c_preGap;
            }
            else 
            {
                // Offset each track by track 1's pre-gap
                sector_track = m_cueDisc.tracks[logical_track].indices[1] + c_preGap;
            }
            
            const MSF msf_track = sectorToMSF(sector_track);

            subqdata.ctrladdr =
                (m_cueDisc.tracks[logical_track].trackType == CueTrackType::TRACK_TYPE_DATA) ? 0x41 : 0x01;
            subqdata.tno = 0x00;
            subqdata.x = toBCD(logical_track);
            subqdata.pmin = toBCD(msf_track.mm);
            subqdata.psec = toBCD(msf_track.ss);
            subqdata.pframe = toBCD(msf_track.ff);
        } 
        else if (point == m_cueDisc.trackCount + 1)  // A0 - Report first track number
        {
            subqdata.ctrladdr = m_cueDisc.tracks[1].trackType == CueTrackType::TRACK_TYPE_DATA ? 0x41 : 0x01;
            subqdata.tno = 0x00;
            subqdata.point = 0xA0;
            subqdata.pmin = 0x01;
            subqdata.psec = m_hasData ? 0x20 : 0x00;  // 0 = audio, 20 = CDROM-XA
            subqdata.pframe = 0x00;
        } 
        else if (point == m_cueDisc.trackCount + 2)  // A1 - Report last track number
        {
            // Thanks rama! )
            subqdata.ctrladdr =
                m_cueDisc.tracks[m_cueDisc.trackCount].trackType == CueTrackType::TRACK_TYPE_DATA ? 0x41 : 0x01;
            subqdata.tno = 0x00;
            subqdata.point = 0xA1;
            subqdata.pmin = toBCD(m_cueDisc.trackCount);
            subqdata.psec = 0x00;
            subqdata.pframe = 0x00;
        } 
        else if (point == m_cueDisc.trackCount + 3)  // A2 - Report lead-out track location
        {
            // <3
            const int sector_lead_out = m_cueDisc.tracks[m_cueDisc.trackCount + 1].indices[1] + c_preGap;
            const MSF msf_lead_out = sectorToMSF(sector_lead_out);
            subqdata.ctrladdr =
                m_cueDisc.tracks[m_cueDisc.trackCount].trackType == CueTrackType::TRACK_TYPE_DATA ? 0x41 : 0x01;
            subqdata.tno = 0x00;
            subqdata.point = 0xA2;
            subqdata.pmin = toBCD(msf_lead_out.mm);
            subqdata.psec = toBCD(msf_lead_out.ss);
            subqdata.pframe = toBCD(msf_lead_out.ff);
        }

        const MSF msf_sector = sectorToMSF(sector);
        subqdata.min = toBCD(msf_sector.mm);
        subqdata.sec = toBCD(msf_sector.ss);
        subqdata.frame = toBCD(msf_sector.ff);
        subqdata.zero = 0x00;
    } 
    else  // Program area + lead-out
    {
        m_currentLogicalTrack = m_cueDisc.trackCount + 1;  // in case seek overshoots past end of disc

        if (sector - c_leadIn < c_preGap)
        {
            m_currentLogicalTrack = 1;
        } 
        else
        {
            for (size_t i = 1; i < m_cueDisc.trackCount + 2; i++)   // + 2 for lead in & lead out
			{
                if (m_cueDisc.tracks[i + 1].indices[0] > sector - c_leadIn - c_preGap) 
                {
                    m_currentLogicalTrack = i;
                    break;
                }
            }
        }
        sector_track = sector - m_cueDisc.tracks[m_currentLogicalTrack].indices[1] - c_leadIn - c_preGap;
        const MSF msf_track = sectorToMSF(sector_track);

        const int sector_abs = (sector - c_leadIn);
        const MSF msf_abs = sectorToMSF(sector_abs);

        subqdata.ctrladdr =
            (m_cueDisc.tracks[m_currentLogicalTrack].trackType == CueTrackType::TRACK_TYPE_DATA) ? 0x41 : 0x01;

        if (m_currentLogicalTrack == m_cueDisc.trackCount + 1)
        {
            subqdata.tno = 0xAA;  // Lead-out track
        } 
        else
        {
            subqdata.tno = toBCD(m_currentLogicalTrack);  // Track numbers
        }
        
        if (sector_track < 0)					   // 2 sec pause track
        {
            subqdata.x = 0x00;                     // Pause encoding
            subqdata.min = 0x00;                   // min
            subqdata.sec = toBCD(msf_track.ss);    // sec (count down)
            subqdata.frame = toBCD(msf_track.ff);  // frame (count down)
        } 
        else
        {
            subqdata.x = 0x01;
            subqdata.min = toBCD(msf_track.mm);
            subqdata.sec = toBCD(msf_track.ss);
            subqdata.frame = toBCD(msf_track.ff);
        }
        subqdata.zero = 0x00;
        subqdata.amin = toBCD(msf_abs.mm);
        subqdata.asec = toBCD(msf_abs.ss);
        subqdata.aframe = toBCD(msf_abs.ff);
    }

    uint16_t crc = 0;
    for (size_t i = 0; i < 10; i++)
    {
        crc = (crc << 8) ^ crc16_lut[((crc >> 8) ^ subqdata.raw[i]) & 0xFF];
    }
    subqdata.crc = (crc << 8) | (crc >> 8);  // swap endianness

    return subqdata;
}

struct Context
{
    TCHAR parentPath[128];
};

static void close_cb(struct CueParser *parser, struct CueScheduler *scheduler, const char *error)
{
    if (error)
    {
        DEBUG_PRINT("Error closing cue parser: %s\n", error);
    }
}

static void size_cb(struct CueFile *file, struct CueScheduler *scheduler, uint64_t size)
{
    DEBUG_PRINT("File size: %zu\n", size);
}

static void parser_cb(struct CueParser *parser, struct CueScheduler *scheduler, const char *error)
{
    if (error)
    {
        DEBUG_PRINT("parser error: %s\n", error);
    }
}

static struct CueFile *__time_critical_func(fileopen)(struct CueFile *file, struct CueScheduler *scheduler, const char *filename)
{
    Context *context = reinterpret_cast<Context *>(scheduler->opaque);
    TCHAR fullpath[256];
    strcpy(fullpath, context->parentPath);
    strcat(fullpath, "/");
    strcat(fullpath, filename);
    return create_posix_file(file, fullpath, FA_READ);
}

FRESULT __time_critical_func(picostation::DiscImage::load)(const TCHAR *targetCue)
{
    // To-do: Need alternate code paths here for parsing cue from alternate sources.
    struct CueScheduler scheduler;
    Scheduler_construct(&scheduler);
    Context context;
    getParentPath(targetCue, context.parentPath);
    scheduler.opaque = &context;

    struct CueFile cue;
    struct CueParser parser;
    
    if (!create_posix_file(&cue, targetCue, FA_READ))
    {
        DEBUG_PRINT("create_posix_file failed for: %s.\n", targetCue);
    }
    
    cue.cfilename = targetCue;
    CueParser_construct(&parser, &m_cueDisc);
    CueParser_parse(&parser, &cue, &scheduler, fileopen, parser_cb);
    Scheduler_run(&scheduler);
    CueParser_close(&parser, &scheduler, close_cb);
    cue.close(&cue, NULL, NULL);

    DEBUG_PRINT("Disc track count: %d\n", m_cueDisc.trackCount);

    // Lead-out
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].fileOffset =
        m_cueDisc.tracks[m_cueDisc.trackCount].indices[1] + m_cueDisc.tracks[m_cueDisc.trackCount].size;
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].indices[0] = m_cueDisc.tracks[m_cueDisc.trackCount + 1].fileOffset;
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].indices[1] = m_cueDisc.tracks[m_cueDisc.trackCount + 1].indices[0];

    m_hasData = false;
    DEBUG_PRINT("Track\tStart\tLength\tPregap\n");
    for (size_t i = 0; i <= m_cueDisc.trackCount + 1; i++)
    {
        if (m_cueDisc.tracks[i].trackType == CueTrackType::TRACK_TYPE_DATA)
        {
            m_hasData = true;
        }
        DEBUG_PRINT("%d\t%d\t%d\t%d\n", i, m_cueDisc.tracks[i].indices[0], m_cueDisc.tracks[i].size,
										   m_cueDisc.tracks[i].indices[1] - m_cueDisc.tracks[i].indices[0]);
    }
    
    c_sectorMax = m_cueDisc.tracks[m_cueDisc.trackCount+1].indices[0] + 4652;
    
    return FR_OK;
}

void __time_critical_func(picostation::DiscImage::unload)()
{
	DEBUG_PRINT("Close:\nTrack\tStart\tLength\tPregap\n");
	if (m_cueDisc.trackCount == 1 || m_cueDisc.tracks[1].file->opaque == m_cueDisc.tracks[2].file->opaque)
	{
		DEBUG_PRINT("1\t%d\t%d\t%d\n" , m_cueDisc.tracks[1].indices[0], m_cueDisc.tracks[1].size,
										m_cueDisc.tracks[1].indices[1] - m_cueDisc.tracks[1].indices[0]);
		m_cueDisc.tracks[1].file->close(m_cueDisc.tracks[1].file, NULL, NULL);
	}
	else
	{
		for (size_t i = 1; i <= m_cueDisc.trackCount; i++)
		{
			DEBUG_PRINT("%d\t%d\t%d\t%d\n", i, m_cueDisc.tracks[i].indices[0], m_cueDisc.tracks[i].size,
													 m_cueDisc.tracks[i].indices[1] - m_cueDisc.tracks[i].indices[0]);
			if (m_cueDisc.tracks[i].file->opaque)
			{
				m_cueDisc.tracks[i].file->close(m_cueDisc.tracks[i].file, NULL, NULL);
			}
		}
	}
}

void __time_critical_func(picostation::DiscImage::makeDummyCue)()
{
    // Create a dummy cue disc with a single data track, as well as lead-in and lead-out tracks.

    constexpr uint32_t c_sectorCount = (98 * 75 * 60) + (57 * 75) + 74;  // 98:57:74(mm:ss:ff) leaving room for 2 sec pre-gap and lead-in

    m_cueDisc.trackCount = 1;

    // Lead-in track
    m_cueDisc.tracks[0].trackType = CueTrackType::TRACK_TYPE_UNKNOWN;
    m_cueDisc.tracks[0].indices[0] = 0;
    m_cueDisc.tracks[0].indices[1] = 0;
    m_cueDisc.tracks[0].size = 0;

    // Data track
    m_cueDisc.tracks[1].trackType = CueTrackType::TRACK_TYPE_DATA;
    m_cueDisc.tracks[1].indices[0] = 0;
    m_cueDisc.tracks[1].indices[1] = 0;
    m_cueDisc.tracks[1].size = c_sectorCount;

    // Lead-out track
    m_cueDisc.tracks[2].trackType = CueTrackType::TRACK_TYPE_UNKNOWN;
    m_cueDisc.tracks[2].fileOffset = m_cueDisc.tracks[1].indices[1] + m_cueDisc.tracks[1].size;
    m_cueDisc.tracks[2].indices[0] = m_cueDisc.tracks[2].fileOffset;
    m_cueDisc.tracks[2].indices[1] = m_cueDisc.tracks[2].indices[0];

    m_hasData = true;

    DEBUG_PRINT("Track\tStart\tLength\tPregap\n");
    for (size_t i = 0; i <= m_cueDisc.trackCount + 1; i++)
    {
        DEBUG_PRINT("%d\t%d\t%d\t%d\n", i, m_cueDisc.tracks[i].indices[0], m_cueDisc.tracks[i].size,
                    m_cueDisc.tracks[i].indices[1] - m_cueDisc.tracks[i].indices[0]);
    }
    
	c_sectorMax = 333000;  // 74:00:00
}

void __time_critical_func(picostation::DiscImage::readSector)(void *buffer, const int sector, DataLocation location, const uint16_t *scramling)
{
    switch (location)
    {
        case DataLocation::SDCard:
            readSectorSD(buffer, sector, scramling);
            break;
        
        case DataLocation::RAM:
            readSectorRAM(buffer, sector, scramling);
            break;
        
        default:
            break;
    }
}

void __time_critical_func(picostation::DiscImage::readSectorRAM)(void *buffer, const int sector, const uint16_t *scramling)
{
    const int adjustedSector = sector - c_preGap;
    size_t targetOffset = adjustedSector * c_cdSamplesBytes;
    
    if (targetOffset >= 0 && targetOffset <= loaderImageSize - c_cdSamplesBytes)
    {
		scramble_data((uint32_t *) buffer, (uint16_t *) &loaderImage[targetOffset], scramling, 1176);
    } 
    else
    {
        buildSector(sector, static_cast<uint32_t *>(buffer), (uint16_t *) s_userData, scramling);
    }
}

void __time_critical_func(picostation::DiscImage::readSectorSD)(void *buffer, const int sector, const uint16_t *scramling)
{
    FRESULT fr;
    UINT br = 0;
    size_t i;

    const int adjustedSector = sector - c_preGap;
    if (adjustedSector < 16 && m_cueDisc.tracks[1].trackType == CueTrackType::TRACK_TYPE_DATA)
	{
		scramble_data((uint32_t *) buffer, (uint16_t *) &loaderImage[adjustedSector * 2352], scramling, 1176);
		return;
	}
    
    for (i = 1; i <= m_cueDisc.trackCount; i++)
    {
        if (adjustedSector < m_cueDisc.tracks[i + 1].indices[0])
        {
            if (m_cueDisc.tracks[i].file->opaque)
            {
                const int64_t seekBytes = (adjustedSector - m_cueDisc.tracks[i].fileOffset) * c_cdSamplesBytes;
                if (seekBytes >= 0)
                {
                    fr = f_lseek((FIL *)m_cueDisc.tracks[i].file->opaque, seekBytes);
                    if (FR_OK != fr)
                    {
                        f_rewind((FIL *)m_cueDisc.tracks[i].file->opaque);
                        DEBUG_PRINT("f_lseek error: (%d)\n", fr);
                    }
                }

                fr = f_read_scramble((FIL *)m_cueDisc.tracks[i].file->opaque, buffer, c_cdSamplesBytes, &br, 
									 scramling, m_cueDisc.tracks[i].trackType == CueTrackType::TRACK_TYPE_DATA);
                //static uint16_t tmpbuf[1176];
                //fr = f_read((FIL *)m_cueDisc.tracks[i].file->opaque, tmpbuf, 2352, &br);
                if (FR_OK != fr)
                {
                    DEBUG_PRINT("f_read error: (%d)\n",  fr);
                }
                //scramble_data((uint32_t *) buffer, tmpbuf, isCurrentTrackData() ? scramling : NULL, 1176);
                break;
            }
        }
    }
    
    if (i > m_cueDisc.trackCount)
	{
		DEBUG_PRINT("out of range image sec (%d)\n", adjustedSector);
		memset(s_userData, 0, c_cdSamplesBytes);
		buildSector(sector, static_cast<uint32_t *>(buffer), (uint16_t *) s_userData, scramling);
	}
    else if (br < c_cdSamplesBytes)
    {
		DEBUG_PRINT("readed %llu of 2352\n", br);
        //buildSector(sector, static_cast<uint8_t *>(buffer), s_userData);
        //br = c_cdSamplesBytes;
    }
}

