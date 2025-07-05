#pragma once

#include <stdint.h>

#include <array>

#include "pseudo_atomics.h"
#include "hardware/dma.h"

namespace picostation {
class MechCommand;

class I2S {
  public:
    I2S() {};
    int dmaChannel;
    bool menu_active;
    
    int getSectorSending() { return m_sectorSending.Load(); }
    uint64_t getLastSectorTime() { return m_lastSectorTime.Load(); }
	//bool dma_bsy() { return dma_channel_is_busy(dmaChannel); }

    [[noreturn]] void start(MechCommand &mechCommand);
	
  private:
    int initDMA(const volatile void *read_addr, unsigned int transfer_count);  // Returns DMA channel number
    void mountSDCard();
	
    pseudoatomic<int> m_sectorSending;
    pseudoatomic<uint64_t> m_lastSectorTime;
};
}  // namespace picostation
