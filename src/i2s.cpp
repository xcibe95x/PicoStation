#include "i2s.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

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
#include "picostation.h"
#include "rtc.h"
#include "subq.h"
#include "utils.h"
#include "values.h"

#if DEBUG_I2S
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

const TCHAR target_Cues[NUM_IMAGES][11] = {
    "UNIROM.cue",
};
const int g_imageIndex = 0;  // To-do: Implement a console side menu to select the cue file

static uint64_t s_psneeTimer;

int getNumberofFileEntries(const char *dir) {
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

void ls(const char *dir) {
    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr; /* Return value */
    char const *directory;
    if (dir[0]) {
        directory = dir;
    } else {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr) {
            DEBUG_PRINT("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
            return;
        }
        directory = cwdbuf;
    }
    DEBUG_PRINT("Directory Listing: %s\n", directory);
    DIR dirObj;       /* Directory object */
    FILINFO fileInfo; /* File information */
    memset(&dirObj, 0, sizeof dirObj);
    memset(&fileInfo, 0, sizeof fileInfo);
    fr = f_findfirst(&dirObj, &fileInfo, directory, "*");
    if (FR_OK != fr) {
        DEBUG_PRINT("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    while (fr == FR_OK && fileInfo.fname[0]) { /* Repeat while an item is found */
        /* Create a string that includes the file name, the file size and the
         attributes string. */
        const char *pcWritableFile = "writable file", *pcReadOnlyFile = "read only file", *pcDirectory = "directory";
        const char *pcAttrib;
        /* Point pcAttrib to a string that describes the file. */
        if (fileInfo.fattrib & AM_DIR) {
            pcAttrib = pcDirectory;
        } else if (fileInfo.fattrib & AM_RDO) {
            pcAttrib = pcReadOnlyFile;
        } else {
            pcAttrib = pcWritableFile;
        }
        /* Create a string that includes the file name, the file size and the
         attributes string. */
        DEBUG_PRINT("%s [%s] [size=%llu]\n", fileInfo.fname, pcAttrib, fileInfo.fsize);

        fr = f_findnext(&dirObj, &fileInfo); /* Search for next item */
    }
    f_closedir(&dirObj);
}

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

void readDirectoryToBuffer(void *buffer, const char *dir) {
    // Put the directory listing into the buffer until full or no more files
    const uint buffer_size = 2336;
    char *buf_ptr = (char *)buffer;
    uint remaining_size = buffer_size;

    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr; /* Return value */
    char const *directory;
    if (dir[0]) {
        directory = dir;
    } else {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr) {
            snprintf(buf_ptr, remaining_size, "f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
            return;
        }
        directory = cwdbuf;
    }
    int written = 0;

    DIR dirObj;       /* Directory object */
    FILINFO fileInfo; /* File information */
    memset(&dirObj, 0, sizeof dirObj);
    memset(&fileInfo, 0, sizeof fileInfo);
    fr = f_findfirst(&dirObj, &fileInfo, directory, "*");
    if (FR_OK != fr) {
        snprintf(buf_ptr, remaining_size, "f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    while (fr == FR_OK && fileInfo.fname[0] &&
           remaining_size > 0) { /* Repeat while an item is found and buffer has space */
        /* Create a string that includes the file name, the file size and the
         attributes string. */
        const char *pcFile = "F", *pcDirectory = "D";
        const char *pcAttrib;
        /* Point pcAttrib to a string that describes the file. */
        if (fileInfo.fattrib & AM_DIR) {
            pcAttrib = pcDirectory;
        } else {
            pcAttrib = pcFile;
        }
        /* Create a string that includes the file name, the file size and the
         attributes string. */
        written = snprintf(buf_ptr, remaining_size, "%s%s\n", pcAttrib, fileInfo.fname);
        buf_ptr += written;
        remaining_size -= written;

        fr = f_findnext(&dirObj, &fileInfo); /* Search for next item */
    }
    f_closedir(&dirObj);
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

    auto currentSector = -1;
    g_sectorSending = -1;
    int loadedImageIndex = -1;
    int filesinDir = 0;

    generateScramblingKey(cdScramblingKey);

    mountSDCard();

    // For testing only
    uint8_t directoryListing[2336];
    readDirectoryToBuffer(directoryListing, "/");

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
            mechcommand::updateMechSens();
            mutex_exit(&g_mechaconMutex);
        }

        // Sector could change during the loop, so we need to keep track of it
        currentSector = g_sector.Load();

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
                if (currentSector != g_sector.Load()) {
                    currentSector = g_sector.Load();
                    sector_change_timer = time_us_64();
                }
            }

            // Sector cache lookup/update
            int cache_hit = -1;

            for (int i = 0; i < c_sectorCacheSize; i++) {
                if (cachedSectors[i] == currentSector) {
                    cache_hit = i;
                    break;
                }
            }

            if (cache_hit == -1) {
                const FileListingStates fileListingState = g_fileListingState.Load();

                uint8_t *buffer = reinterpret_cast<uint8_t *>(cdSamples[roundRobinCacheIndex]);

                switch (fileListingState) {
                    case FileListingStates::GETDIRECTORY:
                        g_discImage.buildSector(currentSector - c_leadIn, buffer, directoryListing);
                        break;

                    case FileListingStates::IDLE:
                        g_discImage.readData(cdSamples[roundRobinCacheIndex], currentSector - c_leadIn - c_preGap);
                        break;
                }

                cachedSectors[roundRobinCacheIndex] = currentSector;
                cache_hit = roundRobinCacheIndex;
                roundRobinCacheIndex = (roundRobinCacheIndex + 1) % c_sectorCacheSize;
            }

            const int16_t *data = reinterpret_cast<int16_t *>(cdSamples[cache_hit]);
            const unsigned abs_lev_chselect = (currentSector % 2);
            // Copy CD samples to PIO buffer
            for (int i = 0; i < c_cdSamplesSize * 2; i++) {
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

void picostation::I2S::psnee(const int sector) {
    static constexpr int PSNEE_SECTOR_LIMIT = c_leadIn;
    static constexpr char SCEX_DATA[][44] = {
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0},
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,
         1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0},
    };

    static int psnee_hysteresis = 0;

    if (sector > 0 && sector < PSNEE_SECTOR_LIMIT && mechcommand::getSens(SENS::GFS) && !g_soctEnabled.Load() &&
        g_discImage.hasData() && ((time_us_64() - s_psneeTimer) > 13333)) {
        psnee_hysteresis++;
        s_psneeTimer = time_us_64();
    }

    if (psnee_hysteresis > 100) {
        psnee_hysteresis = 0;
        DEBUG_PRINT("+SCEX\n");
        gpio_put(Pin::SCEX_DATA, 0);
        s_psneeTimer = time_us_64();
        while ((time_us_64() - s_psneeTimer) < 90000) {
            if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled.Load()) {
                goto abort_psnee;
            }
        }
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 44; j++) {
                gpio_put(Pin::SCEX_DATA, SCEX_DATA[i % 3][j]);
                s_psneeTimer = time_us_64();
                while ((time_us_64() - s_psneeTimer) < 4000) {
                    if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled.Load()) {
                        goto abort_psnee;
                    }
                }
            }
            gpio_put(Pin::SCEX_DATA, 0);
            s_psneeTimer = time_us_64();
            while ((time_us_64() - s_psneeTimer) < 90000) {
                if (sector >= PSNEE_SECTOR_LIMIT || g_soctEnabled.Load()) {
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