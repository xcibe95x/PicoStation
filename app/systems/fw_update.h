#pragma once
#include <stdint.h>

namespace picostation {
    // Called from the command dispatcher
    void handleFirmwareUpdate(uint32_t arg);

    // Entry point the bootloader calls at startup
    void bootloader_main();
}
