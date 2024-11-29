#pragma once

#include "pico/stdlib.h"

namespace picostation {
namespace mechcommand {

extern volatile bool g_sensData[16];

void interrupt_xlat(uint gpio, uint32_t events);
void setSens(uint what, bool new_value);
void updateMechSens();
}  // namespace mechcommand
}  // namespace picostation
