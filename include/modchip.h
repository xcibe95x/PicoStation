#pragma once

#include <stdint.h>

#include "cmd.h"

namespace picostation {
class MechCommand;

class ModChip {
  public:
    void init();
    void loadConfiguration();
    void sendLicenseString(const int sector, MechCommand &mechCommand);

  private:
    void endLicenseSequence();
    uint64_t m_modchipTimer;
};
}  // namespace picostation
