#pragma once

#include <stddef.h>

namespace picostation {
// Attempts to flash the Pico firmware from a UF2 image located on the SD card.
// If path is null, the updater searches the card root for a candidate UF2 file.
// Returns true on success, false on failure.
bool flashFirmwareFromSD(const char *path = nullptr);
bool uf2UpdateAvailable();
}  // namespace picostation

