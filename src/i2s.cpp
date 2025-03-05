#include "i2s.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "cmd.h"
#include "disc_image.h"
#include "drive_mechanics.h"
#include "f_util.h"
#include "ff.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hw_config.h"
#include "logging.h"
#include "main.pio.h"
#include "modchip.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "pseudo_atomics.h"
#include "rtc.h"
#include "subq.h"
#include "values.h"

#if DEBUG_I2S
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

const size_t c_fileNameLength = 255;

const TCHAR target_Cues[NUM_IMAGES][c_fileNameLength] = {
    "UNIROM.cue",
};
pseudoatomic<int> g_imageIndex;  // To-do: Implement a console side menu to select the cue file

static uint64_t s_psneeTimer;

static picostation::ModChip s_modchip;

/*int getNumberofFileEntries(const char *dir) {
    int count = 0;
    DIR dirObj;
    FILINFO fileInfo;
    FRESULT fr = f_opendir(&dirObj, dir);
    if (FR_OK != fr) {
        DEBUG_PRINT("f_opendir error: %s (%d)\n", FRESULT_str(fr), fr);
        return -1;
    }
    while (f_readdir(&dirObj, &fileInfo) == FR_OK && fileInfo.fname[0]) {
        count++;
    }
    f_closedir(&dirObj);
    return count;
}

void readDirectoryToBuffer(void *buffer, const char *path, const size_t offset, const unsigned int bufferSize = 2324) {
    FRESULT res;
    DIR dir;
    FILINFO fno;

    char *buf_ptr = (char *)buffer;
    int remainingSize = bufferSize;

    res = f_opendir(&dir, path); // Open the directory
    if (res == FR_OK) {
        if (offset > 0) {
            for (int i = 0; i < offset; i++) {
                res = f_readdir(&dir, &fno);
                if (res != FR_OK || fno.fname[0] == 0) {
                    break;
                }
            }
        }
        if (res == FR_OK) {
            for (;;) {
                res = f_readdir(&dir, &fno); // Read a directory item
                if (res != FR_OK || fno.fname[0] == 0 || strlen(fno.fname) > remainingSize) {
                    break;
                } // Error or end of dir
                const int written = snprintf(buf_ptr, remainingSize, "%s\n", fno.fname);
                buf_ptr += written;
                remainingSize -= written;
            }
        }
        f_closedir(&dir);
    } else {
        DEBUG_PRINT("Failed to open \"%s\". (%u)\n", path, res);
    }
}*/

void picostation::I2S::generateScramblingKey(uint16_t *cdScramblingKey) {
    int key = 1;

    memset(cdScramblingKey, 0, 1176 * sizeof(uint16_t));

    for (size_t i = 6; i < 1176; i++) {
        char upper = key & 0xFF;
        for (size_t j = 0; j < 8; j++) {
            int bit = ((key & 1) ^ ((key & 2) >> 1)) << 15;
            key = (bit | key) >> 1;
        }

        char lower = key & 0xFF;

        cdScramblingKey[i] = (lower << 8) | upper;

        for (size_t j = 0; j < 8; j++) {
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

int picostation::I2S::initDMA(const volatile void *read_addr, unsigned int transfer_count) {
    int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    const unsigned int i2sDREQ = PIOInstance::I2S_DATA == pio0 ? DREQ_PIO0_TX0 : DREQ_PIO1_TX0;
    channel_config_set_dreq(&c, i2sDREQ);
    dma_channel_configure(channel, &c, &PIOInstance::I2S_DATA->txf[SM::I2S_DATA], read_addr, transfer_count, false);

    return channel;
}

[[noreturn]] void __time_critical_func(picostation::I2S::start)(MechCommand &mechCommand) {
    static constexpr size_t c_sectorCacheSize = 50;

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
    m_sectorSending = -1;
    int loadedImageIndex = -1;
    int filesinDir = 0;

    g_imageIndex = 0;

    generateScramblingKey(cdScramblingKey);

    mountSDCard();

    /*const unsigned int c_userDataSize = 2324;
    uint8_t directoryListing[c_userDataSize] = {0};
    readDirectoryToBuffer(directoryListing, "/", 0, c_userDataSize);*/

    int dmaChannel = initDMA(pioSamples[0], c_cdSamplesSize * 2);

    g_coreReady[1] = true;   // Core 1 is ready
    while (!g_coreReady[0].Load())  // Wait for Core 0 to be ready
    {
        tight_loop_contents();
    }

    // s_psneeTimer = time_us_64();
    s_modchip.init();

    while (true) {
        // Update latching, output SENS
        if (mutex_try_enter(&g_mechaconMutex, 0)) {
            mechCommand.updateMechSens();
            mutex_exit(&g_mechaconMutex);
        }

        // Sector could change during the loop, so we need to keep track of it
        currentSector = g_driveMechanics.getSector();

        // psnee(currentSector, mechCommand);
        s_modchip.injectLicenseString(currentSector, mechCommand);

        // Load the disc image if it has changed
        const int imageIndex = g_imageIndex.Load();
        if (loadedImageIndex != imageIndex) {
            g_discImage.load(target_Cues[g_imageIndex.Load()]);
            loadedImageIndex = imageIndex;

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

        // Data sent via DMA, load the next sector
        if (bufferForDMA != bufferForSDRead) {
            uint64_t sector_change_timer = time_us_64();
            while ((time_us_64() - sector_change_timer) < 100) {
                const int sector = g_driveMechanics.getSector();
                if (currentSector != sector) {
                    currentSector = sector;
                    sector_change_timer = time_us_64();
                }
            }

            // Sector cache lookup/update
            int cache_hit = -1;

            for (size_t i = 0; i < c_sectorCacheSize; i++) {
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

            const int16_t *data = reinterpret_cast<int16_t *>(cdSamples[cache_hit]);
            const unsigned abs_lev_chselect = (currentSector % 2);
            // Copy CD samples to PIO buffer
            for (size_t i = 0; i < c_cdSamplesSize * 2; i++) {
                uint32_t i2s_data;

                if (g_discImage.isCurrentTrackData()) {
                    i2s_data = (cdSamples[cache_hit][i] ^ cdScramblingKey[i]) << 8;
                } else {
                    i2s_data = (cdSamples[cache_hit][i]) << 8;
                    // g_audioPeak = blah;
                    // g_audioLevel = blah;
                }

                if (i2s_data & 0x100) {
                    i2s_data |= 0xFF;
                }

                pioSamples[bufferForSDRead][i] = i2s_data;
            }

            loadedSector[bufferForSDRead] = currentSector;
            bufferForSDRead = (bufferForSDRead + 1) % 2;
        }

        // Start the next transfer if the DMA channel is not busy
        if (!dma_channel_is_busy(dmaChannel)) {
            bufferForDMA = (bufferForDMA + 1) % 2;
            m_sectorSending = loadedSector[bufferForDMA];

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

void picostation::I2S::psnee(const int sector, MechCommand &mechCommand) {
    // License strings, this is just UART, inverted, at 250 baud
    // To-do: review the timing and consider switching to the UART peripheral
    static constexpr char SCEX_DATA[3][44] = {
        // SCEE
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0},

        // SCEA
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0},

        // SCEI
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0},
    };

    // PSNEE conditions:
    // Laser in the lead-in/wobble groove area
    // Disc contains data
    // SOCT disabled
    // GFS set
    // 13.333ms elapsed
    const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
    const bool isDataDisc = g_discImage.hasData();
    const bool soctDisabled = !mechCommand.getSoct();
    const bool gfsSet = mechCommand.getSens(SENS::GFS);
    const uint64_t timeElapsed = time_us_64() - s_psneeTimer;

    static int psnee_hysteresis = 0;

    // Check for conditions to trigger PSNEE, increase hysteresis counter, and reset timer for next state
    if (inWobbleGroove && gfsSet && soctDisabled && isDataDisc) {
        if (timeElapsed > 13333) {
            psnee_hysteresis++;
            s_psneeTimer = time_us_64();

            // if hyteresis counter is over 100, begin psnee loop
            if (psnee_hysteresis > 100) {
                psnee_hysteresis = 0;
                DEBUG_PRINT("+SCEX\n");
                gpio_put(Pin::SCEX_DATA, 0);
                s_psneeTimer = time_us_64();

                // Returns false if the PSNEE loop should be aborted, true if the timer has elapsed
                auto psneeWaitBlockingWithAbort = [&mechCommand](const uint64_t waitTime) {
                    while ((time_us_64() - s_psneeTimer) < waitTime) {
                        const int sector = g_driveMechanics.getSector();
                        const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
                        const bool soctDisabled = !mechCommand.getSoct();
                        const bool gfsSet = mechCommand.getSens(SENS::GFS);

                        if (!soctDisabled || !gfsSet || !inWobbleGroove) {
                            return false;
                        }
                    }

                    return true;
                };

                // Wait 90ms
                if (!psneeWaitBlockingWithAbort(90000U)) {
                    goto abort_psnee;
                }

                // Send the 3 license strings, twice each
                for (int i = 0; i < 6; i++) {
                    for (int j = 0; j < 44; j++) {
                        gpio_put(Pin::SCEX_DATA, SCEX_DATA[i % 3][j]);
                        s_psneeTimer = time_us_64();

                        // Wait 4ms between bits
                        if (!psneeWaitBlockingWithAbort(4000U)) {
                            goto abort_psnee;
                        }
                    }

                    gpio_put(Pin::SCEX_DATA, 0);
                    s_psneeTimer = time_us_64();

                    // Wait 90ms between strings
                    if (!psneeWaitBlockingWithAbort(90000U)) {
                        goto abort_psnee;
                    }
                }

            abort_psnee:
                gpio_put(Pin::SCEX_DATA, 0);
                s_psneeTimer = time_us_64();
                DEBUG_PRINT("-SCEX\n");
            }
        }
    } else {
        psnee_hysteresis = 0;
        s_psneeTimer = time_us_64();
    }
}