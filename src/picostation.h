#include "hardware/pwm.h"
#include "pico/multicore.h"

namespace picostation {

struct PWMSettings {
    const uint gpio;
    uint sliceNum;
    pwm_config config;
    const uint16_t wrap;
    const uint clkdiv;
    const bool invert;
    const uint16_t level;
};

extern mutex_t g_mechaconMutex;
extern volatile bool g_coreReady[2];

extern uint g_soctOffset;
extern uint g_subqOffset;

extern uint g_countTrack;
extern int g_track;
extern int g_originalTrack;
extern volatile int g_sector;
extern int g_sectorForTrackUpdate;
extern volatile int g_sectorSending;
extern int g_sledMoveDirection;
extern uint64_t g_sledTimer;
extern volatile bool g_soctEnabled;
extern bool g_subqDelay;
extern int g_targetPlaybackSpeed;

[[noreturn]] void core0Entry();  // Reset, playback speed, Sled, soct, subq
[[noreturn]] void core1Entry();  // I2S, sdcard, psnee

void initHW();
void updatePlaybackSpeed();
void maybeReset();

}  // namespace picostation