#pragma once

#include <stdint.h>

#include "pico/stdlib.h"

namespace picostation
{
    class I2S
    {
    public:
        I2S() {};

        void start();

    private:
        void generateScramblingKey(uint16_t *cd_scrambling_key);
        void initDMA();
        void mountSDCard();
    };
} // namespace picostation