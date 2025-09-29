#include "firmware_update.h"

extern "C" {
#include "ff.h"
#include "uf2_patched.h"
}

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

namespace picostation {
namespace FirmwareUpdate {
namespace {

constexpr char kUpdateFile[] = "update.uf2";
constexpr char kLogFile[] = "fwupdate.log";
constexpr uint32_t kFlashGuardBytes = 0x1000u;
constexpr uint32_t kFlashBase = XIP_BASE + kFlashGuardBytes;
constexpr uint32_t kFlashLimit = XIP_BASE + PICO_FLASH_SIZE_BYTES;

void log_to_sd(const char *msg) {
    FIL log;
    UINT written = 0;
    if (f_open(&log, kLogFile, FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
        const size_t len = strlen(msg);
        if (len > 0) {
            f_write(&log, msg, static_cast<UINT>(len), &written);
        }
        const char newline[] = "\r\n";
        f_write(&log, newline, sizeof(newline) - 1u, &written);
        f_close(&log);
    }
}

enum class BlockOutcome : uint8_t {
    Written,
    Skipped,
    Invalid
};

static BlockOutcome __not_in_flash_func(process_block)(const uf2_block &block, uint32_t &last_erased_sector_offset) {
    // Controlla magic
    if (block.magic_start0 != UF2_MAGIC_START0 ||
        block.magic_start1 != UF2_MAGIC_START1 ||
        block.magic_end   != UF2_MAGIC_END) {
        return BlockOutcome::Invalid;
    }

    // Controlla dimensione payload
    if (block.payload_size == 0 || block.payload_size > sizeof(block.data)) {
        return BlockOutcome::Invalid;
    }

    // Allinea addr (deve essere multiplo di 256)
    if (block.target_addr % FLASH_PAGE_SIZE != 0) {
        return BlockOutcome::Invalid;
    }

    // 🔥 Permetti scrittura in tutto lo spazio flash (escludi solo 0x00000000)
    if (block.target_addr < XIP_BASE || 
        block.target_addr >= (XIP_BASE + PICO_FLASH_SIZE_BYTES)) {
        return BlockOutcome::Skipped;
    }

    uint32_t offset = block.target_addr - XIP_BASE;
    uint32_t sector_off = offset & ~(FLASH_SECTOR_SIZE - 1);

    // Erase solo se cambiamo settore
    if (sector_off != last_erased_sector_offset) {
        flash_range_erase(sector_off, FLASH_SECTOR_SIZE);
        last_erased_sector_offset = sector_off;
    }

    flash_range_program(offset, block.data, block.payload_size);
    return BlockOutcome::Written;
}

} // namespace

bool __not_in_flash_func(checkAndApplyFromSD)(bool remove_after) {
    log_to_sd("[FWU] start");

    FIL uf2_file;
    FRESULT open_result = f_open(&uf2_file, kUpdateFile, FA_READ);
    if (open_result != FR_OK) {
        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "[FWU] open fail %d", open_result);
        log_to_sd(log_buf);
        return false;
    }
    log_to_sd("[FWU] file opened");

    uint32_t last_erased_sector_offset = 0xFFFFFFFFu;
    uint32_t blocks_written = 0;
    uint32_t blocks_skipped = 0;
    uint32_t blocks_invalid = 0;
    bool truncated_block = false;
    FRESULT read_result = FR_OK;

    multicore_lockout_start_blocking();

    while (true) {
        uf2_block block;
        UINT bytes_read = 0;
        read_result = f_read(&uf2_file, &block, sizeof(block), &bytes_read);

        if (read_result != FR_OK) break;
        if (bytes_read == 0u) break; // EOF
        if (bytes_read != sizeof(block)) {
            truncated_block = true;
            break;
        }

        // 🔥 Dumpa i primi 16 byte grezzi del blocco
char hexbuf[64];
int pos = 0;
for (int i = 0; i < 16; i++) {
    pos += snprintf(hexbuf + pos, sizeof(hexbuf) - pos, "%02X ", ((uint8_t*)&block)[i]);
    if (pos >= (int)sizeof(hexbuf)-4) break; // evita overflow del buffer
}
log_to_sd(hexbuf);

        // 🔥 Logga i magic + indirizzo + size
        char header_log[128];
        snprintf(header_log, sizeof(header_log),
                 "[FWU] s0=%08lX s1=%08lX e=%08lX addr=%08lX size=%u",
                 (unsigned long)block.magic_start0,
                 (unsigned long)block.magic_start1,
                 (unsigned long)block.magic_end,
                 (unsigned long)block.target_addr,
                 block.payload_size);
        log_to_sd(header_log);

        const BlockOutcome outcome = process_block(block, last_erased_sector_offset);
        if (outcome == BlockOutcome::Written) {
            ++blocks_written;
        } else if (outcome == BlockOutcome::Skipped) {
            ++blocks_skipped;
        } else {
            ++blocks_invalid;
            break;
        }
    }

    f_close(&uf2_file);
    multicore_lockout_end_blocking();

    if (read_result != FR_OK) {
        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "[FWU] read fail %d", read_result);
        log_to_sd(log_buf);
    } else if (truncated_block) {
        log_to_sd("[FWU] truncated block");
    }

    if (blocks_invalid > 0u) {
        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "[FWU] invalid block %lu",
                 static_cast<unsigned long>(blocks_invalid));
        log_to_sd(log_buf);
    }

    if (blocks_written == 0u) {
        log_to_sd("[FWU] no blocks written");
        return false;
    }

    if (remove_after) {
        const FRESULT unlink_result = f_unlink(kUpdateFile);
        if (unlink_result == FR_OK) {
            log_to_sd("[FWU] update removed");
        } else {
            char log_buf[64];
            snprintf(log_buf, sizeof(log_buf),
                     "[FWU] remove fail %d", unlink_result);
            log_to_sd(log_buf);
        }
    }

    char summary[96];
    snprintf(summary, sizeof(summary),
             "[FWU] blocks: written=%lu skipped=%lu",
             static_cast<unsigned long>(blocks_written),
             static_cast<unsigned long>(blocks_skipped));
    log_to_sd(summary);
    log_to_sd("[FWU] reboot");

    sleep_ms(50);
    watchdog_reboot(0, 0, 0);
    __builtin_unreachable();
}

} // namespace FirmwareUpdate
} // namespace picostation