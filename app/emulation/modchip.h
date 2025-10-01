// modchip.h - Interface for the modchip emulation responsible for SCEX licensing signals.
#pragma once

#include <stdint.h>

#include "commands/cmd.h"

namespace picostation {
class MechCommand;

class ModChip {
  public:
    void init();
    void sendLicenseString(const int sector, MechCommand &mechCommand);

  private:
    void endLicenseSequence();
    uint64_t m_modchipTimer;
};
}  // namespace picostation
