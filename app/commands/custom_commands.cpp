
#include "commands/mech_commands.h"         
#include "commands/custom_commands.h"                     // for MechCommand::CUSTOM_CMD
#include "systems/directory_listing.h"
#include "hardware/watchdog.h"
#include "commons/pseudo_atomics.h"
#include "picostation.h"
#include "pico/bootrom.h"
#include <stdio.h>

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

extern pseudoatomic<uint32_t> g_fileArg;
extern pseudoatomic<picostation::FileListingStates> needFileCheckAction;
extern pseudoatomic<int> listReadyState;

// ---------------- Custom command handlers ----------------

static void handleNone(uint32_t) {
    needFileCheckAction = picostation::FileListingStates::IDLE;
}

static void handleGotoRoot(uint32_t) {
    DEBUG_PRINT("GOTO_ROOT\n");
    needFileCheckAction = picostation::FileListingStates::GOTO_ROOT;
    listReadyState = 0;
}

static void handleGotoParent(uint32_t) {
    DEBUG_PRINT("GOTO_PARENT\n");
    needFileCheckAction = picostation::FileListingStates::GOTO_PARENT;
    listReadyState = 0;
}

static void handleGotoDirectory(uint32_t) {
    DEBUG_PRINT("GOTO_DIRECTORY\n");
    needFileCheckAction = picostation::FileListingStates::GOTO_DIRECTORY;
    listReadyState = 0;
}

static void handleGetNextContents(uint32_t) {
    DEBUG_PRINT("GET_NEXT_CONTENTS\n");
    needFileCheckAction = picostation::FileListingStates::GET_NEXT_CONTENTS;
    listReadyState = 0;
}

static void handleMountFile(uint32_t) {
    DEBUG_PRINT("MOUNT_FILE\n");
    needFileCheckAction = picostation::FileListingStates::MOUNT_FILE;
}

static void handleIoCommand(uint32_t arg) {
    DEBUG_PRINT("COMMAND_IO_COMMAND %x\n", arg);
}

static void handleIoData(uint32_t arg) {
    DEBUG_PRINT("COMMAND_IO_DATA %x\n", arg);
}

static void handleBootloader(uint32_t arg) {
    if (arg == 0xBEEF) {
        rom_reset_usb_boot_extra(Pin::LED, 0, false);
    }
}

static void handleFirmwareUpdate(uint32_t arg) {
    constexpr uint32_t FW_UPDATE_MAGIC = 0xC0FFEE;

    if (arg != FW_UPDATE_MAGIC) {
        DEBUG_PRINT("COMMAND_FW_UPDATE ignored (bad magic %08x)\n", arg);
        return;
    }

    DEBUG_PRINT("COMMAND_FW_UPDATE: rebooting into bootloader for SD update\n");

    // Give a tiny delay so the message/last frame flushes
    sleep_ms(200);

    // Full chip reset: on restart, your dedicated bootloader runs and checks fw.bin
    watchdog_reboot(0, 0, 0);

    // Never return
    while (true) {
        tight_loop_contents();
    }
}


// ---------------- Handler table ----------------
struct CustomCommandHandler {
    int id;
    const char *name;
    const char *description;
    void (*handler)(uint32_t arg);
};

static const CustomCommandHandler kCustomHandlers[] = {
    { picostation::COMMAND_NONE,          "COMMAND_NONE",          "Clear pending menu work", handleNone },
    { picostation::COMMAND_GOTO_ROOT,     "COMMAND_GOTO_ROOT",     "Jump to the SD card root directory", handleGotoRoot },
    { picostation::COMMAND_GOTO_PARENT,   "COMMAND_GOTO_PARENT",   "Enter the parent directory", handleGotoParent },
    { picostation::COMMAND_GOTO_DIRECTORY,"COMMAND_GOTO_DIRECTORY","Enter the directory indexed by the provided argument", handleGotoDirectory },
    { picostation::COMMAND_GET_NEXT_CONTENTS,"COMMAND_GET_NEXT_CONTENTS","Request the next page of directory entries", handleGetNextContents },
    { picostation::COMMAND_MOUNT_FILE,    "COMMAND_MOUNT_FILE",    "Mount the file indexed by the provided argument", handleMountFile },
    { picostation::COMMAND_IO_COMMAND,    "COMMAND_IO_COMMAND",    "Begin a metadata IO transaction (e.g. game ID transfer)", handleIoCommand },
    { picostation::COMMAND_IO_DATA,       "COMMAND_IO_DATA",       "Send a 16-bit payload for the active metadata IO transaction", handleIoData },
    { picostation::COMMAND_BOOTLOADER,    "COMMAND_BOOTLOADER",    "Reboot the RP2040 into the USB bootloader when armed", handleBootloader },
    { picostation::COMMAND_FW_UPDATE,    "COMMAND_FW_UPDATE",    "Reboot the RP2040 into the USB bootloader when armed", handleFirmwareUpdate },
};

// ---------------- Dispatcher ----------------
bool picostation::dispatchCustomCommand(uint32_t cmd, uint32_t arg)
{
    for (auto &entry : kCustomHandlers) {
        if (entry.id == static_cast<int>(cmd)) {
            DEBUG_PRINT("%s: %s (arg=%u)\n", entry.name, entry.description, arg);
            entry.handler(arg);
            return true;
        }
    }
    DEBUG_PRINT("Unknown custom command: %u\n", cmd);
    return false;
}
