#include "i2s.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "disc_image.h"
#include "f_util.h"
#include "ff.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hw_config.h"
#include "logging.h"
#include "main.pio.h"
#include "pico/stdlib.h"
#include "rtc.h"
#include "subq.h"
#include "utils.h"
#include "values.h"

#if DEBUG_I2S
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

// To-do:
// This was a placeholder for multi-cue support
// but need a console side menu to select the cue file still
const TCHAR target_Cues[NUM_IMAGES][11] = {
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

void psnee(const int sector);
void __time_critical_func(updateMechSens)();

inline void picostation::I2S::generateScramblingKey(uint16_t *cdScramblingKey) {
    int key = 1;

    memset(cdScramblingKey, 0, 1176 * sizeof(uint16_t));

    for (int i = 6; i < 1176; i++) {
        char upper = key & 0xFF;
        for (int j = 0; j < 8; j++) {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }

        char lower = key & 0xFF;

        cdScramblingKey[i] = (lower << 8) | upper;

        for (int j = 0; j < 8; j++) {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }
    }
}

void picostation::I2S::mountSDCard() {
    sd_card_t *pSD = sd_get_by_num(0);
    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr) {
        panic("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    }
}

inline int picostation::I2S::initDMA(const volatile void *read_addr, uint transfer_count) {
    int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    const uint i2sDREQ = PIOInstance::I2S_DATA == pio0 ? DREQ_PIO0_TX0 : DREQ_PIO1_TX0;
    channel_config_set_dreq(&c, i2sDREQ);
    dma_channel_configure(channel, &c, &PIOInstance::I2S_DATA->txf[SM::I2S_DATA], read_addr, transfer_count, false);

    return channel;
}

[[noreturn]] void __time_critical_func(picostation::I2S::start)() {
    static constexpr int c_sectorCacheSize = 50;

    // TODO: separate PSNEE, cue parse, and i2s functions
    uint32_t pioSamples[2][(c_cdSamplesBytes * 2) / sizeof(uint32_t)] = {0};
    int bufferForDMA = 1;
    int bufferForSDRead = 0;
    int loadedSector[2];

    int cachedSectors[c_sectorCacheSize];
    int roundRobinCacheIndex = 0;
    uint16_t cdSamples[c_sectorCacheSize][c_cdSamplesBytes / sizeof(uint16_t)];

    uint16_t cdScramblingKey[1176];

    int currentSector = -1;

    int loadedImageIndex = -1;

    generateScramblingKey(cdScramblingKey);

    mountSDCard();

    int dmaChannel = initDMA(pioSamples[0], c_cdSamplesSize * 2);

    g_coreReady[1] = true;   // Core 1 is ready
    while (!g_coreReady[0])  // Wait for Core 0 to be ready
    {
        tight_loop_contents();
    }

    s_psneeTimer = time_us_64();

    while (true) {
        // Update latching, output SENS
        if (mutex_try_enter(&g_mechaconMutex, 0)) {
            updateMechSens();
            mutex_exit(&g_mechaconMutex);
        }

        // Sector could change during the loop, so we need to keep track of it
        currentSector = g_sector;

        psnee(currentSector);

        if (loadedImageIndex != g_imageIndex) {
            g_discImage.load(target_Cues[g_imageIndex]);
            loadedImageIndex = g_imageIndex;

            // Reset cache and loaded sectors
            loadedSector[0] = -1;
            loadedSector[1] = -1;
            roundRobinCacheIndex = 0;
            bufferForDMA = 1;
            bufferForSDRead = 0;
            memset(cachedSectors, -1, sizeof(cachedSectors));
            memset(cdSamples, 0, sizeof(cdSamples));
            memset(pioSamples, 0, sizeof(pioSamples));
        }

        if (bufferForDMA != bufferForSDRead) {
            uint64_t sector_change_timer = time_us_64();
            while ((time_us_64() - sector_change_timer) < 100) {
                if (currentSector != g_sector) {
                    currentSector = g_sector;
                    sector_change_timer = time_us_64();
                }
            }

            // Sector cache lookup/update
            int cache_hit = -1;
            // Need to take a different path if sector is in the lead-in/pregap
            for (int i = 0; i < c_sectorCacheSize; i++) {
                if (cachedSectors[i] == currentSector) {
                    cache_hit = i;
                    break;
                }
            }

            if (cache_hit == -1) {
                g_discImage.readData(cdSamples[roundRobinCacheIndex], currentSector - c_leadIn - c_preGap);

                cachedSectors[roundRobinCacheIndex] = currentSector;
                cache_hit = roundRobinCacheIndex;
                roundRobinCacheIndex = (roundRobinCacheIndex + 1) % c_sectorCacheSize;
            }

            // Copy CD samples to PIO buffer
            for (int i = 0; i < c_cdSamplesSize * 2; i++) {
                uint32_t i2s_data;
                if (g_discImage.isCurrentTrackData()) {
                    i2s_data = (cdSamples[cache_hit][i] ^ cdScramblingKey[i]) << 8;
                } else {
                    i2s_data = (cdSamples[cache_hit][i]) << 8;
                }

                if (i2s_data & 0x100) {
                    i2s_data |= 0xFF;
                }

                pioSamples[bufferForSDRead][i] = i2s_data;
            }

            loadedSector[bufferForSDRead] = currentSector;
            bufferForSDRead = (bufferForSDRead + 1) % 2;
        }

        if (!dma_channel_is_busy(dmaChannel)) {
            bufferForDMA = (bufferForDMA + 1) % 2;
            g_sectorSending = loadedSector[bufferForDMA];

            dma_hw->ch[dmaChannel].read_addr = (uint32_t)pioSamples[bufferForDMA];

            // Sync with the I2S clock
            while (gpio_get(Pin::LRCK) == 1) {
                tight_loop_contents();
            }
            while (gpio_get(Pin::LRCK) == 0) {
                tight_loop_contents();
            }

            dma_channel_start(dmaChannel);
        }
    }
    __builtin_unreachable();
}

void psnee(const int sector) {
    static constexpr int PSNEE_SECTOR_LIMIT = c_leadIn;
    static constexpr char SCEX_DATA[][44] = {
        // To-do: Change psnee to UART(250 baud)
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0},
    };

    static int psnee_hysteresis = 0;

    if (sector > 0 && sector < PSNEE_SECTOR_LIMIT && g_sensData[SENS::GFS] && !g_soctEnabled &&
        picostation::g_discImage.hasData() && ((time_us_64() - s_psneeTimer) > 13333)) {
        psnee_hysteresis++;
        s_psneeTimer = time_us_64();
    }

    if (psnee_hysteresis > 100) {
        psnee_hysteresis = 0;
        DEBUG_PRINT("+SCEX\n");
        gpio_put(Pin::SCEX_DATA, 0);
        s_psneeTimer = time_us_64();
        while ((time_us_64() - s_psneeTimer) < 90000) {
            if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled) {
                goto abort_psnee;
            }
        }
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 44; j++) {
                gpio_put(Pin::SCEX_DATA, SCEX_DATA[i % 3][j]);
                s_psneeTimer = time_us_64();
                while ((time_us_64() - s_psneeTimer) < 4000) {
                    if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled) {
                        goto abort_psnee;
                    }
                }
            }
            gpio_put(Pin::SCEX_DATA, 0);
            s_psneeTimer = time_us_64();
            while ((time_us_64() - s_psneeTimer) < 90000) {
                if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled) {
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