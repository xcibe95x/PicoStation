#pragma once
#include <stdint.h>

namespace picostation {

// All available custom command IDs
enum CUSTOM_CMD {
    COMMAND_NONE           = 0x0,
    COMMAND_GOTO_ROOT      = 0x1,
    COMMAND_GOTO_PARENT    = 0x2,
    COMMAND_GOTO_DIRECTORY = 0x3,
    COMMAND_GET_NEXT_CONTENTS = 0x4,
    COMMAND_MOUNT_FILE     = 0x5,
    COMMAND_IO_COMMAND     = 0x6,
    COMMAND_IO_DATA        = 0x7,
    COMMAND_BOOTLOADER     = 0xA
};

// Dispatch a custom command (COMMAND_* + arg).
// Returns true if handled, false if unknown.
bool dispatchCustomCommand(uint32_t cmd, uint32_t arg);

} // namespace picostation
