#include "i2s.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "main.pio.h"

#include "cmd.h"
#include "disc_image.h"
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "logging.h"
#include "rtc.h"
#include "subq.h"
#include "utils.h"
#include "values.h"

#if DEBUG_I2S
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

const TCHAR target_Bins[NUM_IMAGES][128] = {
    "STREET MUSIC.bin",
    "UNIROM_BOOTDISC_8.0.K.bin",
    "C\\celeste\\celeste.bin",
    "F\\fromage-0.93e\\fromage.bin",
};

const TCHAR target_Cues[NUM_IMAGES][128] = {
    "STREET MUSIC.cue",
    "UNIROM_BOOTDISC_8.0.K.cue",
    "C\\celeste\\celeste.cue", // Unable to parse atm
    "F\\fromage-0.93e\\fromage.cue",
};

volatile int imageIndex = 1;
int loadedImageIndex = -1;

extern volatile int sector;
extern volatile uint latched;
extern volatile int num_logical_tracks;
extern volatile int current_logical_track;
extern volatile int sector_sending;
extern volatile bool SENS_data[16];
extern volatile bool soct;
extern volatile bool hasData;
extern int *logical_track_to_sector;
extern bool *is_data_track;
extern mutex_t mechacon_mutex;
extern volatile bool core_ready[2];

int sector_t = -1;
uint64_t psneeTimer;
int psnee_hysteresis = 0;

FRESULT fr;
FIL fil;

void i2s_data_thread();
void psnee();

// External functions, in main.c
void select_sens(uint8_t new_sens);
void set_sens(uint8_t what, bool new_value);
void updateMechSens();

char SCExData[][44] = {
    {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0},
    {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0},
    {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0},
};

void i2s_data_thread()
{
    // TODO: separate PSNEE, cue parse, and i2s functions
    uint bytesRead;
    uint32_t *pio_samples[2];
    psneeTimer = time_us_64();
    uint64_t sector_change_timer = 0;
    int buffer_for_dma = 1;
    int buffer_for_sd_read = 0;
    int cachedSectors[SECTOR_CACHE] = {-1};
    int sector_loaded[2] = {-1};
    int roundRobinCacheIndex = 0;
    sd_card_t *pSD;
    int bytes;
    uint16_t *cd_samples[SECTOR_CACHE];
    uint16_t CD_scrambling_key[1176] = {0};
    int key = 1;

    // Allocate memory for samples
    pio_samples[0] = (uint32_t*)malloc(CD_SAMPLES_BYTES * 2);
    pio_samples[1] = (uint32_t*)malloc(CD_SAMPLES_BYTES * 2);
    memset(pio_samples[0], 0, CD_SAMPLES_BYTES * 2);
    memset(pio_samples[1], 0, CD_SAMPLES_BYTES * 2);

    // Allocate memory for cache
    for (int i = 0; i < SECTOR_CACHE; i++)
    {
        cd_samples[i] = (uint16_t*)malloc(CD_SAMPLES_BYTES);
        if (cd_samples[i] == NULL)
        {
            while (true)
            {
                DEBUG_PRINT("not enough memory for cache!\n");
            }
        }
    }

    // Generate CD scrambling key
    for (int i = 6; i < 1176; i++)
    {
        char upper = key & 0xFF;
        for (int j = 0; j < 8; j++)
        {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }

        char lower = key & 0xFF;

        CD_scrambling_key[i] = (lower << 8) | upper;

        for (int j = 0; j < 8; j++)
        {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }
    }

    pSD = sd_get_by_num(0);
    fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr)
    {
        panic("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    }

    int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq(&c, DREQ_PIO0_TX0);
    dma_channel_configure(channel, &c, &pio0->txf[I2S_DATA_SM], pio_samples[0], CD_SAMPLES * 2, false);

    core_ready[1] = true;

    while (!core_ready[0])
    {
        sleep_ms(1);
    }

    while (true)
    {
        sector_t = sector;

        // Update latching, output SENS
        if (mutex_try_enter(&mechacon_mutex, 0))
        {
            updateMechSens();
            mutex_exit(&mechacon_mutex);
        }

        // PSNEE
        psnee();

        if (loadedImageIndex != imageIndex)
        {
            picostation::cueparser::loadImage(&fil, target_Cues[imageIndex], target_Bins[imageIndex]);
            loadedImageIndex = imageIndex;
            memset(cachedSectors, -1, sizeof(cachedSectors));
            sector_loaded[0] = -1;
            sector_loaded[1] = -1;
            roundRobinCacheIndex = 0;
            buffer_for_dma = 1;
            buffer_for_sd_read = 0;
            memset(pio_samples[0], 0, CD_SAMPLES_BYTES * 2);
            memset(pio_samples[1], 0, CD_SAMPLES_BYTES * 2);
        }

        if (buffer_for_dma != buffer_for_sd_read)
        {
            sector_change_timer = time_us_64();
            while ((time_us_64() - sector_change_timer) < 100)
            {
                if (sector_t != sector)
                {
                    sector_t = sector;
                    sector_change_timer = time_us_64();
                }
            }
            int cacheHit = -1;
            int sector_to_search = sector_t < 4650 ? (sector_t % SECTOR_CACHE) + 4650 : sector_t;
            for (int i = 0; i < SECTOR_CACHE; i++)
            {
                if (cachedSectors[i] == sector_to_search)
                {
                    cacheHit = i;
                    break;
                }
            }

            if (cacheHit == -1)
            {
                uint64_t seek_bytes = (sector_to_search - 4650) * 2352LL;
                if (seek_bytes >= 0)
                {
                    fr = f_lseek(&fil, seek_bytes);
                    if (FR_OK != fr)
                    {
                        f_rewind(&fil);
                    }
                }

                fr = f_read(&fil, cd_samples[roundRobinCacheIndex], CD_SAMPLES_BYTES, &bytesRead);
                if (FR_OK != fr)
                    panic("f_read(%s) error: (%d)\n", FRESULT_str(fr), fr);

                cachedSectors[roundRobinCacheIndex] = sector_to_search;
                cacheHit = roundRobinCacheIndex;
                roundRobinCacheIndex = (roundRobinCacheIndex + 1) % SECTOR_CACHE;
            }

            if (sector_t >= 4650)
            {
                for (int i = 0; i < CD_SAMPLES * 2; i++)
                {
                    uint32_t i2s_data;
                    if (is_data_track[current_logical_track])
                    {
                        i2s_data = (cd_samples[cacheHit][i] ^ CD_scrambling_key[i]) << 8;
                    }
                    else
                    {
                        i2s_data = (cd_samples[cacheHit][i]) << 8;
                    }

                    if (i2s_data & 0x100)
                    {
                        i2s_data |= 0xFF;
                    }

                    pio_samples[buffer_for_sd_read][i] = i2s_data;
                }
            }
            else
            {
                memset(pio_samples[buffer_for_sd_read], 0, CD_SAMPLES_BYTES * 2);
            }

            sector_loaded[buffer_for_sd_read] = sector_t;
            buffer_for_sd_read = (buffer_for_sd_read + 1) % 2;
        }

        if (!dma_channel_is_busy(channel))
        {
            buffer_for_dma = (buffer_for_dma + 1) % 2;
            sector_sending = sector_loaded[buffer_for_dma];

            dma_hw->ch[channel].read_addr = (uint32_t)pio_samples[buffer_for_dma];

            while (gpio_get(Pin::LRCK) == 1)
            {
                tight_loop_contents();
            }
            while (gpio_get(Pin::LRCK) == 0)
            {
                tight_loop_contents();
            }

            dma_channel_start(channel);
        }
    }
}

void psnee()
{
    if (sector_t > 0 && sector_t < PSNEE_SECTOR_LIMIT &&
        SENS_data[SENS::GFS] && !soct && hasData &&
        ((time_us_64() - psneeTimer) > 13333))
    {
        psnee_hysteresis++;
        psneeTimer = time_us_64();
    }

    if (psnee_hysteresis > 100)
    {
        psnee_hysteresis = 0;
        DEBUG_PRINT("+SCEX\n");
        gpio_put(Pin::SCEX_DATA, 0);
        psneeTimer = time_us_64();
        while ((time_us_64() - psneeTimer) < 90000)
        {
            if (sector >= PSNEE_SECTOR_LIMIT || soct)
            {
                goto abort_psnee;
            }
        }
        for (int i = 0; i < 6; i++)
        {
            for (int j = 0; j < 44; j++)
            {
                gpio_put(Pin::SCEX_DATA, SCExData[i % 3][j]);
                psneeTimer = time_us_64();
                while ((time_us_64() - psneeTimer) < 4000)
                {
                    if (sector >= PSNEE_SECTOR_LIMIT || soct)
                    {
                        goto abort_psnee;
                    }
                }
            }
            gpio_put(Pin::SCEX_DATA, 0);
            psneeTimer = time_us_64();
            while ((time_us_64() - psneeTimer) < 90000)
            {
                if (sector >= PSNEE_SECTOR_LIMIT || soct)
                {
                    goto abort_psnee;
                }
            }
        }

    abort_psnee:
        gpio_put(Pin::SCEX_DATA, 0);
        psneeTimer = time_us_64();
        DEBUG_PRINT("-SCEX\n");
    }
}