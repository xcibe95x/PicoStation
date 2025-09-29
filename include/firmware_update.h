#pragma once

namespace picostation {
namespace FirmwareUpdate {
    // Attempt to apply firmware update from UF2 file on the mounted SD card.
    // On success this call does not return because it triggers a watchdog reboot.
    bool checkAndApplyFromSD(bool remove_after = true);
}
}