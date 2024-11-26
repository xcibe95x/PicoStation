#pragma once

#include "pico/stdlib.h"

namespace picostation {
namespace mechcommand {
void __time_critical_func(interrupt_xlat)(uint gpio, uint32_t events);
}  // namespace mechcommand
}  // namespace picostation
