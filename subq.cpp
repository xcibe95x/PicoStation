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

extern picostation::DiscImage discImage;

extern uint subqOffset;

void printf_subq(uint8_t *subqdata)
{
    for (int i = 0; i < 12; i++)
    {
        DEBUG_PRINT("%02X ", subqdata[i]);
    }
}

static inline void send_subq(uint8_t *subqdata)
{
    subq_program_init(pio0, SM::c_subq, subqOffset, Pin::SQSO, Pin::SQCK);
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

void start_subq(int sector)
{
    SubQ tracksubq;

    discImage.generateSubQ(&tracksubq, sector);
    send_subq(tracksubq.raw);

#if DEBUG_SUBQ
    if (sector % (50 + discImage.numLogicalTracks()) == 0)
    {
        printf_subq(tracksubq);
        DEBUG_PRINT("%d\n", sector);
    }
#endif
}
