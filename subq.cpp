#include "subq.h"

#include <stdio.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "disc_image.h"
#include "logging.h"
#include "main.pio.h"
#include "utils.h"
#include "values.h"

#if DEBUG_SUBQ
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

extern volatile uint subq_offset;
extern volatile int current_logical_track;
extern volatile int sector;

extern picostation::DiscImage discImage;


uint8_t tracksubq[12];

void printf_subq(uint8_t *subqdata)
{
    for (int i = 0; i < 12; i++)
    {
        DEBUG_PRINT("%02X ", subqdata[i]);
    }
}

static inline void send_subq(uint8_t *subqdata)
{
    subq_program_init(pio0, SM::c_subq, subq_offset, Pin::SQSO, Pin::SQCK);
    pio_sm_set_enabled(pio0, SM::c_subq, true);

    uint sub1 = (subqdata[3] << 24) |
                (subqdata[2] << 16) |
                (subqdata[1] << 8) |
                (subqdata[0]);

    uint sub2 = (subqdata[7] << 24) |
                (subqdata[6] << 16) |
                (subqdata[5] << 8) |
                (subqdata[4]);

    uint sub3 = (subqdata[11] << 24) |
                (subqdata[10] << 16) |
                (subqdata[9] << 8) |
                (subqdata[8]);

    pio_sm_put_blocking(pio0, SM::c_subq, sub1);
    pio_sm_put_blocking(pio0, SM::c_subq, sub2);
    pio_sm_put_blocking(pio0, SM::c_subq, sub3);

    pio_sm_put_blocking(pio1, SM::c_scor, 1);
}

void start_subq()
{
    if (sector < 4500)
    {
        int subq_entry = sector % (3 + discImage.numLogicalTracks());

        if (subq_entry == 0)
        {
            tracksubq[0] = discImage.hasData() ? 0x61 : 0x21;
            tracksubq[1] = 0x00;
            tracksubq[2] = 0xA0;
            tracksubq[7] = 0x01;
            tracksubq[8] = 0x20;
            tracksubq[9] = 0x00;
        }
        else if (subq_entry == 1)
        {
            tracksubq[0] = discImage.hasData() ? 0x61 : 0x21;
            tracksubq[1] = 0x00;
            tracksubq[2] = 0xA1;
            tracksubq[7] = tobcd(discImage.numLogicalTracks());
            tracksubq[8] = 0x00;
            tracksubq[9] = 0x00;
        }
        else if (subq_entry == 2)
        {
            int sector_lead_out = discImage.logicalTrackToSector(discImage.numLogicalTracks() + 1) - 4500;
            tracksubq[0] = discImage.hasData() ? 0x61 : 0x21;
            tracksubq[1] = 0x00;
            tracksubq[2] = 0xA2;
            tracksubq[7] = tobcd(sector_lead_out / 75 / 60);
            tracksubq[8] = tobcd((sector_lead_out / 75) % 60);
            tracksubq[9] = tobcd(sector_lead_out % 75);
        }
        else if (subq_entry > 2)
        {
            int sector_track;
            int logical_track = subq_entry - 2;
            if (logical_track == 1)
            {
                sector_track = 150;
            }
            else
            {
                sector_track = discImage.logicalTrackToSector(logical_track) - 4500;
            }
            tracksubq[0] = discImage.isDataTrack(logical_track) ? 0x41 : 0x01;
            tracksubq[1] = 0x00;
            tracksubq[2] = tobcd(logical_track);
            tracksubq[7] = tobcd(sector_track / 75 / 60);
            tracksubq[8] = tobcd((sector_track / 75) % 60);
            tracksubq[9] = tobcd(sector_track % 75);
        }

        tracksubq[3] = tobcd(sector / 75 / 60);
        tracksubq[4] = tobcd((sector / 75) % 60);
        tracksubq[5] = tobcd(sector % 75);
        tracksubq[6] = 0x00;
        tracksubq[10] = 0x00;
        tracksubq[11] = 0x00;

        send_subq(tracksubq);
    }
    else
    {
        int logical_track = discImage.numLogicalTracks() + 1; // in case seek overshoots past end of disc
        for (int i = 0; i < discImage.numLogicalTracks() + 2; i++)
        { // + 2 for lead in & lead out
            if (discImage.logicalTrackToSector(i + 1) > sector)
            {
                logical_track = i;
                break;
            }
        }
        int sector_track = sector - discImage.logicalTrackToSector(logical_track);
        int sector_abs = (sector - 4500);
        int sector_track_after_pause;

        if (logical_track == 1)
        {
            sector_track_after_pause = sector_track - 150;
        }
        else
        {
            sector_track_after_pause = sector_track;
        }

        current_logical_track = logical_track;

        tracksubq[0] = discImage.isDataTrack(logical_track) ? 0x41 : 0x01;

        if (logical_track == discImage.numLogicalTracks() + 1)
        {
            tracksubq[1] = 0xAA;
        }
        else
        {
            tracksubq[1] = tobcd(logical_track);
        }
        if (sector_track < 150 && logical_track == 1)
        { // 2 sec pause track
            tracksubq[2] = 0x00;
            tracksubq[3] = 0x00;                            // min
            tracksubq[4] = tobcd(1 - (sector_track / 75));  // sec (count down)
            tracksubq[5] = tobcd(74 - (sector_track % 75)); // frame (count down)
        }
        else
        {
            tracksubq[2] = 0x01;
            tracksubq[3] = tobcd(sector_track_after_pause / 75 / 60);   // min
            tracksubq[4] = tobcd((sector_track_after_pause / 75) % 60); // sec
            tracksubq[5] = tobcd(sector_track_after_pause % 75);        // frame
        }
        tracksubq[6] = 0x00;
        tracksubq[7] = tobcd(sector_abs / 75 / 60);   // amin
        tracksubq[8] = tobcd((sector_abs / 75) % 60); // asec
        tracksubq[9] = tobcd(sector_abs % 75);        // aframe
        tracksubq[10] = 0x00;
        tracksubq[11] = ((sector % 2) == 0) ? 0x00 : 0x80;

        send_subq(tracksubq);
    }

#if DEBUG_SUBQ
    if (sector % (50 + discImage.numLogicalTracks()) == 0)
    {
        printf_subq(tracksubq);
        DEBUG_PRINT("%d\n", sector);
    }
#endif
}
