#pragma once

#include <stdint.h>

#include "pseudo_atomics.h"

namespace picostation {
class MechCommand;

class I2S {
  public:
    I2S() {};
    int getSectorSending() { return m_sectorSending.Load(); }

    [[noreturn]] void start(MechCommand &mechCommand);

  private:
    void generateScramblingKey(uint16_t *cdScramblingKey);
    int initDMA(const volatile void *read_addr, unsigned int transfer_count);  // Returns DMA channel number
    void mountSDCard();
    void psnee(const int sector, MechCommand &mechCommand);
    void reset();

    pseudoatomic<int> m_sectorSending;
};
}  // namespace picostation