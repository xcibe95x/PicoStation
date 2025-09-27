#include "firmware_update.h"

#include <stddef.h>
#include <stdint.h>

#include "ff.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/m0plus.h"

namespace {

constexpr char kUpdateFileName[] = "PICOSTATION.UF2";
constexpr uint32_t kUpdateMagic = 0x5049434f;  // "PICO"
constexpr uint32_t kUf2MagicStart0 = 0x0a324655;
constexpr uint32_t kUf2MagicStart1 = 0x9e5d5157;
constexpr uint32_t kUf2MagicEnd = 0x0ab16f30;
constexpr uint32_t kUf2BlockSize = 512;
constexpr uint32_t kUpdateRegionSize = 1024 * 1024;  // 1MB reserved for staged updates
constexpr uint32_t kHeaderSize = FLASH_PAGE_SIZE;
constexpr uint32_t kUpdateRegionOffset = PICO_FLASH_SIZE_BYTES - kUpdateRegionSize;
constexpr uint32_t kUf2FlagNotMainFlash = 0x00000001;
constexpr uint32_t kUf2FlagFamilyIdPresent = 0x00002000;
constexpr uint32_t kRp2040FamilyId = 0xe48bff56;

static_assert(kUpdateRegionSize < PICO_FLASH_SIZE_BYTES, "Update storage must leave application flash available");

struct UpdateHeader {
    uint32_t magic;
    uint32_t file_size;
    uint32_t stored_size;
    uint32_t reserved;
};

struct Uf2Block {
    uint32_t magic_start0;
    uint32_t magic_start1;
    uint32_t flags;
    uint32_t target_address;
    uint32_t payload_size;
    uint32_t block_number;
    uint32_t block_count;
    uint32_t family_id;
    uint8_t data[476];
    uint32_t magic_end;
};

static_assert(sizeof(Uf2Block) == kUf2BlockSize, "UF2 block layout mismatch");
static_assert(kUpdateRegionOffset % FLASH_SECTOR_SIZE == 0, "Update storage must be sector aligned");

extern "C" const uint8_t __flash_binary_end;

FATFS g_updateFatFs;

const UpdateHeader *getHeader() {
    return reinterpret_cast<const UpdateHeader *>(XIP_BASE + kUpdateRegionOffset);
}

uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool stageUpdateFile(FIL &file, uint32_t file_size);
bool applyStagedUpdateIfPresent();
bool stageUpdateFromSD();
bool __not_in_flash_func(processStagedUpdate)(const UpdateHeader *header, bool program);
[[noreturn]] void __not_in_flash_func(applyUpdateAndReboot)(const UpdateHeader *header);
void invalidateHeader();
bool hasUpdateRegionSpace();

}  // namespace

void picostation::checkForFirmwareUpdate() {
    // Apply a staged update from a previous boot if present.
    if (applyStagedUpdateIfPresent()) {
        return;
    }

    FRESULT fr = f_mount(&g_updateFatFs, "", 1);
    if (fr != FR_OK) {
        return;
    }

    stageUpdateFromSD();

    f_unmount("");
}

namespace {

bool applyStagedUpdateIfPresent() {
    if (!hasUpdateRegionSpace()) {
        return false;
    }

    const UpdateHeader *header = getHeader();
    if (header->magic != kUpdateMagic) {
        return false;
    }

    if (header->file_size == 0 || header->file_size > (kUpdateRegionSize - kHeaderSize)) {
        invalidateHeader();
        return false;
    }

    if ((header->file_size % kUf2BlockSize) != 0) {
        invalidateHeader();
        return false;
    }

    // Validate the staged image before attempting to apply it.
    if (!processStagedUpdate(header, false)) {
        invalidateHeader();
        return false;
    }

    applyUpdateAndReboot(header);
    __builtin_unreachable();
}

bool stageUpdateFromSD() {
    if (!hasUpdateRegionSpace()) {
        return false;
    }

    FILINFO file_info;
    FRESULT fr = f_stat(kUpdateFileName, &file_info);
    if (fr != FR_OK) {
        return false;
    }

    if (file_info.fsize == 0 || (file_info.fsize % kUf2BlockSize) != 0) {
        return false;
    }

    if (file_info.fsize > (kUpdateRegionSize - kHeaderSize)) {
        return false;
    }

    FIL file;
    fr = f_open(&file, kUpdateFileName, FA_READ);
    if (fr != FR_OK) {
        return false;
    }

    bool staged = stageUpdateFile(file, static_cast<uint32_t>(file_info.fsize));

    f_close(&file);

    if (!staged) {
        return false;
    }

    f_unlink(kUpdateFileName);

    // Applying will reboot the system on success.
    applyStagedUpdateIfPresent();
    return true;
}

bool stageUpdateFile(FIL &file, uint32_t file_size) {
    const uint32_t padded_size = align_up(file_size, FLASH_PAGE_SIZE);
    const uint32_t total_size = align_up(kHeaderSize + padded_size, FLASH_SECTOR_SIZE);
    if (total_size > kUpdateRegionSize) {
        return false;
    }

    flash_range_erase(kUpdateRegionOffset, total_size);

    uint8_t page_buffer[FLASH_PAGE_SIZE];
    uint32_t written = 0;
    uint32_t dest_offset = kUpdateRegionOffset + kHeaderSize;

    while (written < file_size) {
        const uint32_t chunk = (file_size - written) < FLASH_PAGE_SIZE ? (file_size - written) : FLASH_PAGE_SIZE;
        UINT bytes_read = 0;
        FRESULT fr = f_read(&file, page_buffer, chunk, &bytes_read);
        if (fr != FR_OK || bytes_read != chunk) {
            return false;
        }

        if (chunk < FLASH_PAGE_SIZE) {
            for (uint32_t i = chunk; i < FLASH_PAGE_SIZE; ++i) {
                page_buffer[i] = 0xFF;
            }
        }

        flash_range_program(dest_offset, page_buffer, FLASH_PAGE_SIZE);
        dest_offset += FLASH_PAGE_SIZE;
        written += chunk;
    }

    UpdateHeader header = {kUpdateMagic, file_size, padded_size, 0};
    uint8_t header_buffer[FLASH_PAGE_SIZE];
    for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i) {
        header_buffer[i] = 0xFF;
    }

    const uint8_t *header_bytes = reinterpret_cast<const uint8_t *>(&header);
    for (size_t i = 0; i < sizeof(header); ++i) {
        header_buffer[i] = header_bytes[i];
    }

    flash_range_program(kUpdateRegionOffset, header_buffer, FLASH_PAGE_SIZE);
    return true;
}

void invalidateHeader() {
    rom_flash_range_erase(kUpdateRegionOffset, FLASH_SECTOR_SIZE);
}

bool hasUpdateRegionSpace() {
    const uint32_t used_flash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE);
    return used_flash <= kUpdateRegionOffset;
}

bool __not_in_flash_func(processStagedUpdate)(const UpdateHeader *header, bool program) {
    const uint8_t *cursor = reinterpret_cast<const uint8_t *>(XIP_BASE + kUpdateRegionOffset + kHeaderSize);
    const uint8_t *end = cursor + header->file_size;

    static bool erased[kUpdateRegionOffset / FLASH_SECTOR_SIZE];

    if (program) {
        for (uint32_t i = 0; i < (kUpdateRegionOffset / FLASH_SECTOR_SIZE); ++i) {
            erased[i] = false;
        }
    }

    while (cursor < end) {
        const Uf2Block *block = reinterpret_cast<const Uf2Block *>(cursor);
        cursor += kUf2BlockSize;
        if (block->magic_start0 != kUf2MagicStart0 || block->magic_start1 != kUf2MagicStart1 || block->magic_end != kUf2MagicEnd) {
            return false;
        }

        if (block->payload_size == 0 || block->payload_size > sizeof(block->data)) {
            return false;
        }

        if ((block->payload_size % FLASH_PAGE_SIZE) != 0) {
            return false;
        }

        if (block->target_address < XIP_BASE) {
            return false;
        }

        const uint32_t target_offset = block->target_address - XIP_BASE;
        if (target_offset + block->payload_size > kUpdateRegionOffset) {
            return false;
        }

        if ((block->target_address & (FLASH_PAGE_SIZE - 1u)) != 0) {
            return false;
        }

        if ((block->flags & kUf2FlagFamilyIdPresent) != 0 && block->family_id != kRp2040FamilyId) {
            return false;
        }

        if ((block->flags & kUf2FlagNotMainFlash) != 0) {
            continue;
        }

        if (program) {
            const uint32_t first_sector = target_offset / FLASH_SECTOR_SIZE;
            const uint32_t last_sector = (target_offset + block->payload_size - 1u) / FLASH_SECTOR_SIZE;

            for (uint32_t sector = first_sector; sector <= last_sector; ++sector) {
                if (!erased[sector]) {
                    rom_flash_range_erase(sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
                    erased[sector] = true;
                }
            }

            rom_flash_range_program(target_offset, block->data, block->payload_size);
        }

    }

    return true;
}

[[noreturn]] void __not_in_flash_func(applyUpdateAndReboot)(const UpdateHeader *header) {
    uint32_t irq_state = save_and_disable_interrupts();

    const bool programmed = processStagedUpdate(header, true);

    if (programmed) {
        // Clear the header so the update is not re-applied on the next boot.
        rom_flash_range_erase(kUpdateRegionOffset, FLASH_SECTOR_SIZE);

        restore_interrupts(irq_state);
        watchdog_reboot(0, 0, 0);
    }

    while (true) {
        __wfi();
    }
}

}  // namespace
