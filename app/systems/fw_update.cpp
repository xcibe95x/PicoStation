#include "firmware_update.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "ff.h"

#include <string.h>
#include <stdio.h>

namespace picostation {

// --------------------------------------------------
// Configuration
// --------------------------------------------------
static constexpr uint32_t FW_UPDATE_MAGIC = 0xC0FFEE;
static constexpr uint32_t BOOTLOADER_SIZE = 0x10000;  // 64 KB reserved
static constexpr uint32_t APP_OFFSET      = BOOTLOADER_SIZE;
static constexpr uint32_t SECTOR_SIZE     = 4096;
static constexpr uint32_t BUF_SIZE        = SECTOR_SIZE;

// --------------------------------------------------
// Flash helpers
// --------------------------------------------------
static void __not_in_flash_func(write_sector)(uint32_t offset, const uint8_t* data) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, SECTOR_SIZE);
    flash_range_program(offset, data, SECTOR_SIZE);
    restore_interrupts(ints);
}

// --------------------------------------------------
// Core updater (read fw.bin → flash app region)
// --------------------------------------------------
static bool flash_from_sd(const char* path, uint32_t base_offs) {
    FATFS fs;
    FIL f;
    FRESULT fr;

    if ((fr = f_mount(&fs, "", 1)) != FR_OK) {
        printf("[FWU] mount fail %d\n", fr);
        return false;
    }
    if ((fr = f_open(&f, path, FA_READ)) != FR_OK) {
        printf("[FWU] fw.bin not found (%d)\n", fr);
        return false;
    }

    const uint32_t max_app = (PICO_FLASH_SIZE_BYTES > base_offs) ?
                             (PICO_FLASH_SIZE_BYTES - base_offs) : 0;
    const uint32_t fsize   = f_size(&f);
    if (fsize == 0 || fsize > max_app) {
        printf("[FWU] bad size %lu > %lu\n", (unsigned long)fsize, (unsigned long)max_app);
        f_close(&f);
        return false;
    }

    static uint8_t __attribute__((aligned(256))) buf[BUF_SIZE];
    UINT br = 0;
    uint32_t offs = base_offs;
    uint32_t remain = fsize;

    multicore_lockout_start_blocking();

    while (remain) {
        const UINT to_read = remain >= BUF_SIZE ? BUF_SIZE : remain;
        fr = f_read(&f, buf, to_read, &br);
        if (fr != FR_OK || br != to_read) {
            printf("[FWU] read err %d, br=%u\n", fr, br);
            break;
        }

        if (br < BUF_SIZE) memset(buf + br, 0xFF, BUF_SIZE - br);

        printf("[FWU] write 0x%08lx\n", (unsigned long)offs);
        write_sector(offs, buf);
        offs   += BUF_SIZE;
        remain -= br;
    }

    f_close(&f);
    multicore_lockout_end_blocking();

    return remain == 0;
}

// --------------------------------------------------
// Command handler (menu → update flag + reboot)
// --------------------------------------------------
void handleFirmwareUpdate(uint32_t arg) {
    if (arg != FW_UPDATE_MAGIC) {
        printf("[FWU] Ignored: bad magic 0x%08x\n", arg);
        return;
    }

    printf("[FWU] Triggering update, rebooting into bootloader\n");

    // simplest trigger: just reset, bootloader_main() will run on restart
    watchdog_hw->ctrl = WATCHDOG_CTRL_ENABLE_BITS | WATCHDOG_CTRL_TRIGGER_BITS;
    while (true) tight_loop_contents();
}

// --------------------------------------------------
// Bootloader entry point
// --------------------------------------------------
void bootloader_main() {
    stdio_init_all();
    sleep_ms(200);

    printf("[BOOT] PicoStation bootloader\n");

    bool ok = flash_from_sd("fw.bin", APP_OFFSET);
    if (ok) {
        printf("[BOOT] Update complete. Rebooting to app...\n");
    } else {
        printf("[BOOT] No update found, or failed. Jumping to app...\n");
    }

    // Jump to user app
    typedef void (*app_entry_t)(void);
    const uint32_t app_start_addr = XIP_BASE + APP_OFFSET;
    const app_entry_t app_entry = (app_entry_t)(*(uint32_t*)(app_start_addr + 4));
    __set_MSP(*(uint32_t*)app_start_addr);
    app_entry();
}

} // namespace picostation
