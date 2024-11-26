#pragma once

#include <stdint.h>

#include "pico/stdlib.h"

namespace picostation {
class I2S {
  public:
    I2S() {};

    [[noreturn]] void start();

  private:
    void generateScramblingKey(uint16_t *cdScramblingKey);
    int initDMA(const volatile void *read_addr, uint transfer_count);  // Returns DMA channel number
    void mountSDCard();
    void reset();
};
}  // namespace picostation