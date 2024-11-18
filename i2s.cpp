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

const TCHAR target_Cues[NUM_IMAGES][128] = {
    "UNIROM.cue",
};

volatile int g_imageIndex = 0;

extern volatile int g_sector;
extern volatile int g_sectorSending;
extern volatile bool g_sensData[16];
extern volatile bool g_soctEnabled;
extern mutex_t g_mechaconMutex;
extern volatile bool g_coreReady[2];

static uint64_t s_psneeTimer;

extern picostation::DiscImage g_discImage;

void generateScramblingKey(uint16_t *cd_scrambling_key);
void i2sDataThread();
void psnee(int sector);
void __time_critical_func(updateMechSens)();

void generateScramblingKey(uint16_t *cd_scrambling_key)
{
    int key = 1;

    for (int i = 6; i < 1176; i++)
    {
        char upper = key & 0xFF;
        for (int j = 0; j < 8; j++)
        {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }

        char lower = key & 0xFF;

        cd_scrambling_key[i] = (lower << 8) | upper;

        for (int j = 0; j < 8; j++)
        {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }
    }
}

void mountSDCard()
{
    sd_card_t *pSD = sd_get_by_num(0);
    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr)
    {
        panic("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    }
}

void __time_critical_func(i2sDataThread)()
{
    static constexpr int c_sectorCache = 50;

    // TODO: separate PSNEE, cue parse, and i2s functions
    uint32_t pio_samples[2][(c_cdSamplesBytes * 2) / sizeof(uint32_t)] = {0, 0};
    s_psneeTimer = time_us_64();
    uint64_t sector_change_timer = 0;
    int buffer_for_dma = 1;
    int buffer_for_sd_read = 0;
    int cached_sectors[c_sectorCache] = {-1};
    int sector_loaded[2] = {-1};
    int round_robin_cache_index = 0;
    sd_card_t *pSD;
    int bytes;
    uint16_t cd_samples[c_sectorCache][c_cdSamplesBytes / sizeof(uint16_t)] = {0};
    uint16_t cd_scrambling_key[1176] = {0};
    int current_sector = -1;
    int loaded_image_index = -1;

    // Generate CD scrambling key
    generateScramblingKey(cd_scrambling_key);
    mountSDCard();

    // Init DMA
    int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq(&c, DREQ_PIO0_TX0);
    dma_channel_configure(channel, &c, &pio0->txf[SM::I2SDATA], pio_samples[0], c_cdSamples * 2, false);

    g_coreReady[1] = true; // Core 1 is ready

    while (!g_coreReady[0]) // Wait for Core 0 to be ready
    {
        tight_loop_contents();
    }

    while (true)
    {
        // Update latching, output SENS
        if (mutex_try_enter(&g_mechaconMutex, 0))
        {
            updateMechSens();
            mutex_exit(&g_mechaconMutex);
        }

        // Sector could change during the loop, so we need to keep track of it
        current_sector = g_sector;

        psnee(current_sector);

        if (loaded_image_index != g_imageIndex)
        {
            g_discImage.load(target_Cues[g_imageIndex]);
            loaded_image_index = g_imageIndex;
            // Reset cache and loaded sectors
            memset(cached_sectors, -1, sizeof(cached_sectors));
            sector_loaded[0] = -1;
            sector_loaded[1] = -1;
            round_robin_cache_index = 0;
            buffer_for_dma = 1;
            buffer_for_sd_read = 0;
            memset(pio_samples[0], 0, c_cdSamplesBytes * 2);
            memset(pio_samples[1], 0, c_cdSamplesBytes * 2);
        }

        if (buffer_for_dma != buffer_for_sd_read)
        {
            sector_change_timer = time_us_64();
            while ((time_us_64() - sector_change_timer) < 100)
            {
                if (current_sector != g_sector)
                {
                    current_sector = g_sector;
                    sector_change_timer = time_us_64();
                }
            }

            // Sector cache lookup/update
            int cache_hit = -1;
            int sector_to_search = current_sector < c_leadIn + c_preGap ? c_leadIn + c_preGap : current_sector;
            for (int i = 0; i < c_sectorCache; i++)
            {
                if (cached_sectors[i] == sector_to_search)
                {
                    cache_hit = i;
                    break;
                }
            }

            if (cache_hit == -1)
            {
                g_discImage.readData(cd_samples[round_robin_cache_index], sector_to_search - c_leadIn - c_preGap);

                cached_sectors[round_robin_cache_index] = sector_to_search;
                cache_hit = round_robin_cache_index;
                round_robin_cache_index = (round_robin_cache_index + 1) % c_sectorCache;
            }

            // Copy CD samples to PIO buffer
            for (int i = 0; i < c_cdSamples * 2; i++)
            {
                uint32_t i2s_data;
                if (g_discImage.isCurrentTrackData())
                {
                    i2s_data = (cd_samples[cache_hit][i] ^ cd_scrambling_key[i]) << 8;
                }
                else
                {
                    i2s_data = (cd_samples[cache_hit][i]) << 8;
                }

                if (i2s_data & 0x100)
                {
                    i2s_data |= 0xFF;
                }

                pio_samples[buffer_for_sd_read][i] = i2s_data;
            }

            sector_loaded[buffer_for_sd_read] = current_sector;
            buffer_for_sd_read = (buffer_for_sd_read + 1) % 2;
        }

        if (!dma_channel_is_busy(channel))
        {
            buffer_for_dma = (buffer_for_dma + 1) % 2;
            g_sectorSending = sector_loaded[buffer_for_dma];

            dma_hw->ch[channel].read_addr = (uint32_t)pio_samples[buffer_for_dma];

            // Sync with the I2S clock
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

void psnee(int sector)
{
    static constexpr int PSNEE_SECTOR_LIMIT = c_leadIn;
    static constexpr char SCEX_DATA[][44] = {
        // To-do: Change psnee to UART(250 baud)
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0},
    };

    static int psnee_hysteresis = 0;

    if (sector > 0 && sector < PSNEE_SECTOR_LIMIT &&
        g_sensData[SENS::GFS] && !g_soctEnabled && g_discImage.hasData() &&
        ((time_us_64() - s_psneeTimer) > 13333))
    {
        psnee_hysteresis++;
        s_psneeTimer = time_us_64();
    }

    if (psnee_hysteresis > 100)
    {
        psnee_hysteresis = 0;
        DEBUG_PRINT("+SCEX\n");
        gpio_put(Pin::SCEX_DATA, 0);
        s_psneeTimer = time_us_64();
        while ((time_us_64() - s_psneeTimer) < 90000)
        {
            if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled)
            {
                goto abort_psnee;
            }
        }
        for (int i = 0; i < 6; i++)
        {
            for (int j = 0; j < 44; j++)
            {
                gpio_put(Pin::SCEX_DATA, SCEX_DATA[i % 3][j]);
                s_psneeTimer = time_us_64();
                while ((time_us_64() - s_psneeTimer) < 4000)
                {
                    if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled)
                    {
                        goto abort_psnee;
                    }
                }
            }
            gpio_put(Pin::SCEX_DATA, 0);
            s_psneeTimer = time_us_64();
            while ((time_us_64() - s_psneeTimer) < 90000)
            {
                if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled)
                {
                    goto abort_psnee;
                }
            }
        }

    abort_psnee:
        gpio_put(Pin::SCEX_DATA, 0);
        s_psneeTimer = time_us_64();
        DEBUG_PRINT("-SCEX\n");
    }
}