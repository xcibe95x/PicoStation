#include "uf2_flasher.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "ff.h"
#include "global.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

namespace {
constexpr uint32_t kUf2MagicStart0 = 0x0A324655;
constexpr uint32_t kUf2MagicStart1 = 0x9E5D5157;
constexpr uint32_t kUf2MagicEnd = 0x0AB16F30;
constexpr uint32_t kUf2FlagNotMainFlash = 0x00000001;
constexpr uint32_t kUf2FlagFileContainer = 0x00001000;
constexpr uint32_t kRp2040FamilyId = 0xE48BFF56;

constexpr size_t kFlashSectorCount = PICO_FLASH_SIZE_BYTES / FLASH_SECTOR_SIZE;
static_assert(PICO_FLASH_SIZE_BYTES % FLASH_SECTOR_SIZE == 0, "Unexpected flash layout");

struct UF2Block {
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t familyID;
    uint8_t data[476];
    uint32_t magicEnd;
} __attribute__((packed));

static_assert(sizeof(UF2Block) == 512, "UF2 block size must be 512 bytes");

bool hasUf2Extension(const char *name) {
    const size_t length = strlen(name);
    if (length < 4) {
        return false;
    }
    const char *ext = name + (length - 4);
    if (ext[0] != '.') {
        return false;
    }
    return (std::tolower(static_cast<unsigned char>(ext[1])) == 'u') &&
           (std::tolower(static_cast<unsigned char>(ext[2])) == 'f') &&
           (std::tolower(static_cast<unsigned char>(ext[3])) == '2');
}

bool equalsIgnoreCase(const char *lhs, const char *rhs) {
    while (*lhs && *rhs) {
        if (std::tolower(static_cast<unsigned char>(*lhs)) !=
            std::tolower(static_cast<unsigned char>(*rhs))) {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

bool selectUf2Candidate(char *outPath, size_t outSize) {
    DIR dir;
    FRESULT res = f_opendir(&dir, "");
    if (res != FR_OK) {
        printf("Failed to open root directory (%d)\n", res);
        return false;
    }

    bool found = false;
    char bestName[c_maxFilePathLength + 1] = {0};
    uint32_t bestTimestamp = 0;

    FILINFO entry{};

    while (true) {
        res = f_readdir(&dir, &entry);
        if (res != FR_OK || entry.fname[0] == '\0') {
            break;
        }

        const char *name = entry.fname;

        if ((entry.fattrib & AM_DIR) != 0 || !hasUf2Extension(name)) {
            continue;
        }

        if (equalsIgnoreCase(name, "picostation.uf2")) {
            strncpy(bestName, name, sizeof(bestName) - 1);
            found = true;
            bestTimestamp = 0xFFFFFFFFu;
            break;
        }

        const uint32_t timestamp = (static_cast<uint32_t>(entry.fdate) << 16) | entry.ftime;
        if (!found || timestamp > bestTimestamp) {
            strncpy(bestName, name, sizeof(bestName) - 1);
            bestTimestamp = timestamp;
            found = true;
        }
    }

    f_closedir(&dir);

    if (!found) {
        printf("No UF2 file found on SD card\n");
        return false;
    }

    if (bestName[0] == '\0') {
        return false;
    }

    if (bestName[0] == '/' || bestName[0] == '\\') {
        strncpy(outPath, bestName, outSize - 1);
    } else {
        const int written = snprintf(outPath, outSize, "/%s", bestName);
        if (written <= 0 || static_cast<size_t>(written) >= outSize) {
            return false;
        }
    }

    return true;
}

bool __not_in_flash_func(programUf2Block)(const UF2Block &block, std::array<bool, kFlashSectorCount> &erased) {
    uint32_t currentAddr = block.targetAddr;
    const uint8_t *dataPtr = block.data;
    uint32_t remaining = block.payloadSize;

    while (remaining > 0) {
        const uint32_t flashOffset = currentAddr - XIP_BASE;
        if ((flashOffset % FLASH_PAGE_SIZE) != 0) {
            return false;
        }

        const size_t sectorIndex = flashOffset / FLASH_SECTOR_SIZE;
        if (sectorIndex >= erased.size()) {
            return false;
        }

        uint32_t interruptState = save_and_disable_interrupts();
        if (!erased[sectorIndex]) {
            flash_range_erase(sectorIndex * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
            erased[sectorIndex] = true;
        }
        flash_range_program(flashOffset, dataPtr, FLASH_PAGE_SIZE);
        restore_interrupts(interruptState);

        currentAddr += FLASH_PAGE_SIZE;
        dataPtr += FLASH_PAGE_SIZE;
        remaining -= FLASH_PAGE_SIZE;
    }

    return true;
}
}  // namespace

namespace picostation {

bool uf2UpdateAvailable() {
    char dummyPath[c_maxFilePathLength + 2];
    return selectUf2Candidate(dummyPath, sizeof(dummyPath));
}

bool flashFirmwareFromSD(const char *path) {
    char resolvedPath[c_maxFilePathLength + 2] = {0};
    if (path != nullptr && path[0] != '\0') {
        const size_t length = strnlen(path, sizeof(resolvedPath) - 1);
        strncpy(resolvedPath, path, length);
        resolvedPath[length] = '\0';
    } else {
        if (!selectUf2Candidate(resolvedPath, sizeof(resolvedPath))) {
            return false;
        }
    }

    FIL file;
    FRESULT res = f_open(&file, resolvedPath, FA_READ);
    if (res != FR_OK) {
        printf("Failed to open UF2 file %s (%d)\n", resolvedPath, res);
        return false;
    }

    std::array<bool, kFlashSectorCount> erased = {};
    UF2Block block{};
    UINT bytesRead = 0;
    uint32_t expectedBlocks = 0;
    uint32_t processedBlocks = 0;
    bool ok = true;

    while (true) {
        res = f_read(&file, &block, sizeof(block), &bytesRead);
        if (res != FR_OK) {
            printf("UF2 read error (%d)\n", res);
            ok = false;
            break;
        }

        if (bytesRead == 0) {
            break;
        }

        if (bytesRead != sizeof(block)) {
            printf("Incomplete UF2 block read\n");
            ok = false;
            break;
        }

        if (block.magicStart0 != kUf2MagicStart0 || block.magicStart1 != kUf2MagicStart1 || block.magicEnd != kUf2MagicEnd) {
            printf("Invalid UF2 magic\n");
            ok = false;
            break;
        }

        if ((block.flags & kUf2FlagNotMainFlash) != 0 || (block.flags & kUf2FlagFileContainer) != 0) {
            continue;
        }

        if (block.payloadSize == 0) {
            continue;
        }

        if (block.payloadSize > sizeof(block.data) || (block.payloadSize % FLASH_PAGE_SIZE) != 0) {
            printf("Unsupported UF2 payload size %lu\n", static_cast<unsigned long>(block.payloadSize));
            ok = false;
            break;
        }

        if (block.targetAddr < XIP_BASE || (block.targetAddr + block.payloadSize) > (XIP_BASE + PICO_FLASH_SIZE_BYTES)) {
            printf("UF2 target address out of range: 0x%08lx\n", static_cast<unsigned long>(block.targetAddr));
            ok = false;
            break;
        }

        if (block.familyID != 0 && block.familyID != kRp2040FamilyId) {
            printf("UF2 family mismatch: 0x%08lx\n", static_cast<unsigned long>(block.familyID));
            ok = false;
            break;
        }

        if (expectedBlocks == 0 && block.numBlocks != 0) {
            expectedBlocks = block.numBlocks;
        }

        multicore_lockout_start_blocking();
        const bool blockWritten = programUf2Block(block, erased);
        multicore_lockout_end_blocking();

        if (!blockWritten) {
            printf("Failed to program UF2 block at 0x%08lx\n", static_cast<unsigned long>(block.targetAddr));
            ok = false;
            break;
        }

        ++processedBlocks;
    }

    f_close(&file);

    if (!ok) {
        return false;
    }

    if (processedBlocks == 0) {
        printf("UF2 contained no writable blocks\n");
        return false;
    }

    if (expectedBlocks != 0 && processedBlocks != expectedBlocks) {
        printf("UF2 block count mismatch (expected %lu, wrote %lu)\n",
               static_cast<unsigned long>(expectedBlocks),
               static_cast<unsigned long>(processedBlocks));
        return false;
    }

    printf("Flashed %lu UF2 blocks from %s\n", static_cast<unsigned long>(processedBlocks), resolvedPath);

    const FRESULT unlinkResult = f_unlink(resolvedPath);
    if (unlinkResult != FR_OK) {
        printf("Warning: failed to delete %s after flashing (%d)\n", resolvedPath, unlinkResult);
    }

    return true;
}

}  // namespace picostation

