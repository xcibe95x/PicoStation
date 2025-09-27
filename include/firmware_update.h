#pragma once

namespace picostation {

// Checks for staged firmware updates stored in flash and applies them if present.
// If an update package named PICOSTATION.UF2 exists on the SD card it will be
// staged and applied automatically. The function reboots the Pico when an
// update is successfully installed.
void checkForFirmwareUpdate();

}  // namespace picostation
