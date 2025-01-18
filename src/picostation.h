#include "hardware/pwm.h"
#include "pico/multicore.h"
#include "pseudo_atomics.h"

namespace picostation {

/*class ODE {
  public:
  private:
    MechCommand m_mechCommand;
};*/

namespace audioControlModes {
enum : unsigned int {
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

extern pseudoatomic<FileListingStates> g_fileListingState;

struct PWMSettings {
    const unsigned int gpio;
    unsigned int sliceNum;
    pwm_config config;
    const uint16_t wrap;
    const unsigned int clkdiv;
    const bool invert;
    const uint16_t level;
};

extern mutex_t g_mechaconMutex;
extern bool g_coreReady[2];

extern unsigned int g_soctOffset;
extern unsigned int g_subqOffset;

extern bool g_subqDelay;
extern int g_targetPlaybackSpeed;
extern unsigned int g_audioCtrlMode;
// extern volatile int32_t g_audioPeak;
// extern volatile int32_t g_audioLevel;

[[noreturn]] void core0Entry();  // Reset, playback speed, Sled, soct, subq
[[noreturn]] void core1Entry();  // I2S, sdcard, psnee

void initHW();
void updatePlaybackSpeed();
void maybeReset();
}  // namespace picostation