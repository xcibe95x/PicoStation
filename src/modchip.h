#pragma once

#include <stdint.h>

#include "cmd.h"

namespace picostation {
class MechCommand;

class ModChip {
  public:
    void init();
    void injectLicenseString(const int sector, MechCommand &mechCommand);

  private:
    uint64_t m_psneeTimer;
};
}  // namespace picostation