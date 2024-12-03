#pragma once

#include "pico/stdlib.h"

namespace picostation {
namespace mechcommand {

bool getSens(const uint what);
void interrupt_xlat(uint gpio, uint32_t events);
void setSens(const uint what, const bool new_value);
void updateMechSens();
}  // namespace mechcommand
}  // namespace picostation
