#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "third_party/RP2040_Pseudo_Atomic/Inc/RP2040Atomic.hpp"

int main() {
    set_sys_clock_khz(271200, true);
    sleep_ms(5);

    patom::PseudoAtomicInit();

    multicore_launch_core1(picostation::core1Entry); // I2S Thread

    picostation::initHW();

    picostation::core0Entry(); // Reset, playback speed, Sled, soct, subq
    __builtin_unreachable();
}
