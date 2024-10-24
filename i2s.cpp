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

const TCHAR target_Bins[c_numImages][128] = {
    "UNIROM.bin",
};

const TCHAR target_Cues[c_numImages][128] = {
    "UNIROM.cue",
};

volatile int imageIndex = 0;

extern volatile int sector;
extern volatile int sectorSending;
extern volatile bool sensData[16];
extern volatile bool soct;
extern mutex_t mechaconMutex;
extern volatile bool coreReady[2];

static uint64_t psneeTimer;

extern picostation::DiscImage discImage;

void generateScramblingKey(uint16_t *cdScramblingKey);
void i2sDataThread();
void psnee(int sector);
void __time_critical_func(updateMechSens)();

void generateScramblingKey(uint16_t *cdScramblingKey)
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

        cdScramblingKey[i] = (lower << 8) | upper;

        for (int j = 0; j < 8; j++)
        {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }
    }
}

void i2sDataThread()
{
    static constexpr size_t c_cdSamples = 588;
    static constexpr size_t c_cdSamplesBytes = c_cdSamples * 2 * 2; // 2352
    static constexpr int c_sectorCache = 50;

    // TODO: separate PSNEE, cue parse, and i2s functions
    uint bytesRead;
    uint32_t pio_samples[2][(c_cdSamplesBytes * 2) / sizeof(uint32_t)] = {0, 0};
    psneeTimer = time_us_64();
    uint64_t sectorChangeTimer = 0;
    int bufferForDMA = 1;
    int bufferForSDRead = 0;
    int cachedSectors[c_sectorCache] = {-1};
    int sectorLoaded[2] = {-1};
    int roundRobinCacheIndex = 0;
    sd_card_t *pSD;
    int bytes;
    uint16_t cdSamples[c_sectorCache][c_cdSamplesBytes / sizeof(uint16_t)] = {0};
    uint16_t cdScramblingKey[1176] = {0};
    int sectorTemp = -1;
    int loadedImageIndex = -1;

    FRESULT fr;
    FIL fil = {0};

    // Generate CD scrambling key
    generateScramblingKey(cdScramblingKey);

    // Mount SD card
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
    dma_channel_configure(channel, &c, &pio0->txf[SM::c_i2sData], pio_samples[0], c_cdSamples * 2, false);

    coreReady[1] = true;

    while (!coreReady[0])
    {
        sleep_ms(1);
    }

    while (true)
    {
        sectorTemp = sector;

        // Update latching, output SENS
        if (mutex_try_enter(&mechaconMutex, 0))
        {
            updateMechSens();
            mutex_exit(&mechaconMutex);
        }

        psnee(sectorTemp);

        if (loadedImageIndex != imageIndex)
        {
            discImage.load(&fil, target_Cues[imageIndex], target_Bins[imageIndex]);
            //discImage.loadv2(target_Cues[imageIndex]);

            loadedImageIndex = imageIndex;
            memset(cachedSectors, -1, sizeof(cachedSectors));
            sectorLoaded[0] = -1;
            sectorLoaded[1] = -1;
            roundRobinCacheIndex = 0;
            bufferForDMA = 1;
            bufferForSDRead = 0;
            memset(pio_samples[0], 0, c_cdSamplesBytes * 2);
            memset(pio_samples[1], 0, c_cdSamplesBytes * 2);
        }

        if (bufferForDMA != bufferForSDRead)
        {
            sectorChangeTimer = time_us_64();
            while ((time_us_64() - sectorChangeTimer) < 100)
            {
                if (sectorTemp != sector)
                {
                    sectorTemp = sector;
                    sectorChangeTimer = time_us_64();
                }
            }

            // Sector cache lookup/update
            int cacheHit = -1;
            int sectorToSearch = sectorTemp < 4650 ? (sectorTemp % c_sectorCache) + 4650 : sectorTemp;
            for (int i = 0; i < c_sectorCache; i++)
            {
                if (cachedSectors[i] == sectorToSearch)
                {
                    cacheHit = i;
                    break;
                }
            }

            if (cacheHit == -1)
            {
                uint64_t seekBytes = (sectorToSearch - 4650) * 2352LL;
                if (seekBytes >= 0)
                {
                    fr = f_lseek(&fil, seekBytes);
                    if (FR_OK != fr)
                    {
                        f_rewind(&fil);
                    }
                }

                fr = f_read(&fil, cdSamples[roundRobinCacheIndex], c_cdSamplesBytes, &bytesRead);
                if (FR_OK != fr)
                {
                    panic("f_read(%s) error: (%d)\n", FRESULT_str(fr), fr);
                }

                cachedSectors[roundRobinCacheIndex] = sectorToSearch;
                cacheHit = roundRobinCacheIndex;
                roundRobinCacheIndex = (roundRobinCacheIndex + 1) % c_sectorCache;
            }

            // Copy CD samples to PIO buffer
            if (sectorTemp >= 4650)
            {
                for (int i = 0; i < c_cdSamples * 2; i++)
                {
                    uint32_t i2s_data;
                    if (discImage.isCurrentTrackData())
                    {
                        i2s_data = (cdSamples[cacheHit][i] ^ cdScramblingKey[i]) << 8;
                    }
                    else
                    {
                        i2s_data = (cdSamples[cacheHit][i]) << 8;
                    }

                    if (i2s_data & 0x100)
                    {
                        i2s_data |= 0xFF;
                    }

                    pio_samples[bufferForSDRead][i] = i2s_data;
                }
            }
            else
            {
                memset(pio_samples[bufferForSDRead], 0, c_cdSamplesBytes * 2);
            }

            sectorLoaded[bufferForSDRead] = sectorTemp;
            bufferForSDRead = (bufferForSDRead + 1) % 2;
        }

        if (!dma_channel_is_busy(channel))
        {
            bufferForDMA = (bufferForDMA + 1) % 2;
            sectorSending = sectorLoaded[bufferForDMA];

            dma_hw->ch[channel].read_addr = (uint32_t)pio_samples[bufferForDMA];

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
    static constexpr int c_psneeSectorLimit = 4500;
    static constexpr char SCExData[][44] = {
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0},
    };

    static int psnee_hysteresis = 0;

    if (sector > 0 && sector < c_psneeSectorLimit &&
        sensData[SENS::GFS] && !soct && discImage.hasData() &&
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
            if (sector >= c_psneeSectorLimit || soct)
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
                    if (sector >= c_psneeSectorLimit || soct)
                    {
                        goto abort_psnee;
                    }
                }
            }
            gpio_put(Pin::SCEX_DATA, 0);
            psneeTimer = time_us_64();
            while ((time_us_64() - psneeTimer) < 90000)
            {
                if (sector >= c_psneeSectorLimit || soct)
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