#include "picostation.h"

#include "i2s.h"

[[noreturn]] void picostation::core1Entry() {
    picostation::I2S g_i2s;

    g_i2s.start();
    while (1) asm("");
    __builtin_unreachable();
}