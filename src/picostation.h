#include "hardware/pwm.h"
#include "pico/multicore.h"
#include "third_party/RP2040_Pseudo_Atomic/Inc/RP2040Atomic.hpp"

namespace picostation {

namespace audioControlModes {
enum : uint {
    NORMAL = 0b00,
    LEVELMETER = 0b01,
    PEAKMETER = 0b10,
    ALTNORMAL = 0b11,
};
}

enum class FileListingStates {
    IDLE,
    GETTINGDIRFILECOUNT,
    DIRREADY,
    GETDIRECTORY,
};

extern patom::PseudoAtomic<FileListingStates> g_fileListingState;

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
extern bool g_coreReady[2];

extern uint g_soctOffset;
extern uint g_subqOffset;

extern uint g_countTrack;
extern int g_track;
extern int g_originalTrack;
extern patom::types::patomic_int g_sector;
extern int g_sectorForTrackUpdate;
extern patom::types::patomic_int g_sectorSending;
extern int g_sledMoveDirection;
extern uint64_t g_sledTimer;
extern patom::types::patomic_bool g_soctEnabled;
extern bool g_subqDelay;
extern int g_targetPlaybackSpeed;
extern uint g_audioCtrlMode;
// extern volatile int32_t g_audioPeak;
// extern volatile int32_t g_audioLevel;

[[noreturn]] void core0Entry();  // Reset, playback speed, Sled, soct, subq
[[noreturn]] void core1Entry();  // I2S, sdcard, psnee

void initHW();
void updatePlaybackSpeed();
void maybeReset();
}  // namespace picostation