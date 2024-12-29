#pragma once

#include <stddef.h>
#include <stdint.h>

namespace picostation {
namespace mechcommand {

bool getSens(const size_t what);
void interrupt_xlat(unsigned int gpio, uint32_t events);
void setSens(const size_t, const bool new_value);
void updateMechSens();
}  // namespace mechcommand
}  // namespace picostation
