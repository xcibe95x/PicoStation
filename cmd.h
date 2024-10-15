#pragma once

#include "pico/stdlib.h"

void __time_critical_func(interrupt_xlat)(uint gpio, uint32_t events);
