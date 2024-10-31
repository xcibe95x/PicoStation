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

extern picostation::DiscImage g_discImage;

extern uint g_subqOffset;

void printf_subq(uint8_t *data)
{
    for (int i = 0; i < 12; i++)
    {
        DEBUG_PRINT("%02X ", data[i]);
    }
}

static inline void send_subq(uint8_t *data)
{
    subq_program_init(pio0, SM::SUBQ, g_subqOffset, Pin::SQSO, Pin::SQCK);
    pio_sm_set_enabled(pio0, SM::SUBQ, true);

    uint sub1 = (data[3] << 24) |
                (data[2] << 16) |
                (data[1] << 8) |
                (data[0]);

    uint sub2 = (data[7] << 24) |
                (data[6] << 16) |
                (data[5] << 8) |
                (data[4]);

    uint sub3 = (data[11] << 24) |
                (data[10] << 16) |
                (data[9] << 8) |
                (data[8]);

    pio_sm_put_blocking(pio0, SM::SUBQ, sub1);
    pio_sm_put_blocking(pio0, SM::SUBQ, sub2);
    pio_sm_put_blocking(pio0, SM::SUBQ, sub3);

    pio_sm_put_blocking(pio1, SM::SCOR, 1);
}

void start_subq(int sector)
{
    SubQ tracksubq;

    g_discImage.generateSubQ(&tracksubq, sector);
    send_subq(tracksubq.raw);

#if DEBUG_SUBQ
    if (sector % (50 + g_discImage.numLogicalTracks()) == 0)
    {
        printf_subq(tracksubq.raw);
        DEBUG_PRINT("%d\n", sector);
    }
#endif
}
