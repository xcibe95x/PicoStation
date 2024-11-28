#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmath>

#include "cmd.h"
#include "disc_image.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/pwm.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/vreg.h"
#include "i2s.h"
#include "logging.h"
#include "main.pio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "subq.h"
#include "utils.h"
#include "values.h"

#if DEBUG_MISC
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

volatile bool g_soctEnabled = false;  // Serial Read Out Circuit
uint g_countTrack = 0;
int g_track = 0;
int g_originalTrack = 0;

int g_sledMoveDirection = SledMove::STOP;
uint64_t g_sledTimer = 0;

volatile int g_sector = 0;
int g_sectorForTrackUpdate = 0;
volatile int g_sectorSending = -1;

volatile bool g_subqDelay = false;

static int s_currentPlaybackSpeed = 1;
int g_targetPlaybackSpeed = 1;

mutex_t g_mechaconMutex;
volatile bool g_coreReady[2] = {false, false};

static uint s_mechachonOffset;
uint g_soctOffset;
uint g_subqOffset;

struct PWMSettings {
    const uint gpio;
    uint sliceNum;
    pwm_config config;
    const uint16_t wrap;
    const uint clkdiv;
    const bool invert;
    const uint16_t level;
};

static PWMSettings pwmMainClock = {.gpio = Pin::CLK, .wrap = 1, .clkdiv = 2, .invert = false, .level = 1};

static PWMSettings pwmDataClock = {
    .gpio = Pin::DA15, .wrap = (1 * 32) - 1, .clkdiv = 4, .invert = true, .level = (32 / 2)};

static PWMSettings pwmLRClock = {
    .gpio = Pin::LRCK, .wrap = (48 * 32) - 1, .clkdiv = 4, .invert = false, .level = (48 * (32 / 2))};

volatile bool g_sensData[16] = {
    0,  // $0X - FZC
    0,  // $1X - AS
    0,  // $2X - TZC
    0,  // $3X - Misc.
    0,  // $4X - XBUSY
    1,  // $5X - FOK
    0,  // $6X - 0
    0,  // $7X - 0
    0,  // $8X - 0
    0,  // $9X - 0
    1,  // $AX - GFS
    0,  // $BX - COMP
    0,  // $CX - COUT
    0,  // $DX - 0
    0,  // $EX - OV64
    0   // $FX - 0
};

void initialize();
void updatePlaybackSpeed();
void maybeReset();
void setSens(uint what, bool new_value);
void __time_critical_func(updateMechSens)();

void initPWM(PWMSettings *settings) {
    gpio_set_function(settings->gpio, GPIO_FUNC_PWM);
    settings->sliceNum = pwm_gpio_to_slice_num(settings->gpio);
    settings->config = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&settings->config, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&settings->config, settings->wrap);
    pwm_config_set_clkdiv(&settings->config, settings->clkdiv);
    pwm_config_set_output_polarity(&settings->config, settings->invert, settings->invert);
    pwm_init(settings->sliceNum, &settings->config, false);
    pwm_set_both_levels(settings->sliceNum, settings->level, settings->level);
}

void initialize() {
#if DEBUG_LOGGING_ENABLED
    stdio_init_all();
    stdio_set_chars_available_callback(NULL, NULL);
    sleep_ms(1250);
#endif
    DEBUG_PRINT("Initializing...\n");

    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(100);

    srand(time(NULL));
    mutex_init(&g_mechaconMutex);

    for (const auto pin : Pin::allPins) {
        gpio_init(pin);
    }

    gpio_set_dir(Pin::XLAT, GPIO_IN);
    gpio_set_dir(Pin::SQCK, GPIO_IN);
    gpio_set_dir(Pin::LMTSW, GPIO_OUT);
    gpio_set_dir(Pin::SCEX_DATA, GPIO_OUT);
    gpio_put(Pin::SCEX_DATA, 1);
    gpio_set_dir(Pin::DOOR, GPIO_IN);
    gpio_set_dir(Pin::RESET, GPIO_IN);
    gpio_set_dir(Pin::SENS, GPIO_OUT);
    gpio_set_dir(Pin::DA15, GPIO_OUT);
    gpio_set_dir(Pin::DA16, GPIO_OUT);
    gpio_set_dir(Pin::LRCK, GPIO_OUT);
    gpio_set_dir(Pin::SCOR, GPIO_OUT);
    gpio_put(Pin::SCOR, 0);
    gpio_set_dir(Pin::SQSO, GPIO_OUT);
    gpio_put(Pin::SQSO, 0);
    gpio_set_dir(Pin::CLK, GPIO_OUT);
    gpio_set_dir(Pin::CMD_CK, GPIO_IN);
    gpio_set_dir(Pin::CMD_DATA, GPIO_IN);

    gpio_set_input_hysteresis_enabled(Pin::XLAT, true);
    gpio_set_input_hysteresis_enabled(Pin::SQCK, true);
    gpio_set_input_hysteresis_enabled(Pin::RESET, true);
    gpio_set_input_hysteresis_enabled(Pin::CMD_CK, true);

    initPWM(&pwmMainClock);
    initPWM(&pwmDataClock);
    initPWM(&pwmLRClock);

    uint i2s_pio_offset = pio_add_program(PIOInstance::I2S_DATA, &i2s_data_program);
    i2s_data_program_init(PIOInstance::I2S_DATA, SM::I2S_DATA, i2s_pio_offset, Pin::DA15, Pin::DA16);

    s_mechachonOffset = pio_add_program(PIOInstance::MECHACON, &mechacon_program);
    mechacon_program_init(PIOInstance::MECHACON, SM::MECHACON, s_mechachonOffset, Pin::CMD_DATA);

    g_soctOffset = pio_add_program(PIOInstance::SOCT, &soct_program);
    g_subqOffset = pio_add_program(PIOInstance::SUBQ, &subq_program);

    pio_sm_set_enabled(PIOInstance::I2S_DATA, SM::I2S_DATA, true);
    pwm_set_mask_enabled((1 << pwmLRClock.sliceNum) | (1 << pwmDataClock.sliceNum) | (1 << pwmMainClock.sliceNum));

    uint64_t start_time = time_us_64();
    gpio_set_dir(Pin::RESET, GPIO_OUT);
    gpio_put(Pin::RESET, 0);
    sleep_ms(300);
    gpio_set_dir(Pin::RESET, GPIO_IN);

    while ((time_us_64() - start_time) < 30000) {
        if (gpio_get(Pin::RESET) == 0) {
            start_time = time_us_64();
        }
    }

    while ((time_us_64() - start_time) < 30000) {
        if (gpio_get(Pin::CMD_CK) == 0) {
            start_time = time_us_64();
        }
    }

    gpio_set_irq_enabled_with_callback(Pin::XLAT, GPIO_IRQ_EDGE_FALL, true, &picostation::mechcommand::interrupt_xlat);
    pio_sm_set_enabled(PIOInstance::MECHACON, SM::MECHACON, true);
    DEBUG_PRINT("ON!\n");
}

void updatePlaybackSpeed() {
    if (s_currentPlaybackSpeed != g_targetPlaybackSpeed) {
        s_currentPlaybackSpeed = g_targetPlaybackSpeed;
        const uint clock_div = (s_currentPlaybackSpeed == 1) ? 4 : 2;
        pwm_set_mask_enabled(0);
        pwm_config_set_clkdiv_int(&pwmDataClock.config, clock_div);
        pwm_config_set_clkdiv_int(&pwmLRClock.config, clock_div);
        pwm_hw->slice[pwmDataClock.sliceNum].div = pwmDataClock.config.div;
        pwm_hw->slice[pwmLRClock.sliceNum].div = pwmLRClock.config.div;
        pwm_set_mask_enabled((1 << pwmLRClock.sliceNum) | (1 << pwmDataClock.sliceNum) | (1 << pwmMainClock.sliceNum));
        DEBUG_PRINT("x%i\n", s_currentPlaybackSpeed);
    }
}

void maybeReset() {
    if (gpio_get(Pin::RESET) == 0) {
        DEBUG_PRINT("RESET!\n");
        pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
        pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, false);
        pio_sm_restart(PIOInstance::MECHACON, SM::MECHACON);

        mechacon_program_init(PIOInstance::MECHACON, SM::MECHACON, s_mechachonOffset, Pin::CMD_DATA);
        g_subqDelay = false;
        g_soctEnabled = false;

        gpio_put(Pin::SCOR, 0);
        gpio_put(Pin::SQSO, 0);

        uint64_t start_time = time_us_64();

        while ((time_us_64() - start_time) < 30000) {
            if (gpio_get(Pin::RESET) == 0) {
                start_time = time_us_64();
            }
        }

        while ((time_us_64() - start_time) < 30000) {
            if (gpio_get(Pin::CMD_CK) == 0) {
                start_time = time_us_64();
            }
        }

        pio_sm_set_enabled(PIOInstance::MECHACON, SM::MECHACON, true);
    }
}

[[noreturn]] void __time_critical_func(core0Entry)() {
    static constexpr uint c_MaxTrackMoveTime = 15;    // uS
    static constexpr uint c_MaxSubqDelayTime = 3333;  // uS

    picostation::SubQ subq(&picostation::g_discImage);
    uint64_t subqDelayTime = 0;

    int sector_per_track = sectorsPerTrack(0);

    g_coreReady[0] = true;
    while (!g_coreReady[1]) {
        tight_loop_contents();
    }

    while (true) {
        // Limit Switch
        gpio_put(Pin::LMTSW, g_sector > 3000);

        // Update latching, output SENS
        if (mutex_try_enter(&g_mechaconMutex, 0)) {
            picostation::mechcommand::updateMechSens();
            mutex_exit(&g_mechaconMutex);
        }

        updatePlaybackSpeed();

        // Check for reset signal
        maybeReset();

        // Soct/Sled/seek
        if (g_soctEnabled) {
            uint interrupts = save_and_disable_interrupts();
            // waiting for RX FIFO entry does not work.
            sleep_us(300);
            g_soctEnabled = false;
            pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, false);
            restore_interrupts(interrupts);
        } else if (g_sledMoveDirection != SledMove::STOP) {
            if ((time_us_64() - g_sledTimer) > c_MaxTrackMoveTime) {
                g_track = clamp(g_track + g_sledMoveDirection, c_trackMin, c_trackMax);  // +1 or -1
                g_sector = trackToSector(g_track);
                g_sectorForTrackUpdate = g_sector;

                const int tracks_moved = g_track - g_originalTrack;
                if (abs(tracks_moved) >= g_countTrack) {
                    g_originalTrack = g_track;
                    picostation::mechcommand::setSens(SENS::COUT, !g_sensData[SENS::COUT]);
                }

                g_sledTimer = time_us_64();
            }
        } else if (g_sensData[SENS::GFS]) {
            if (g_subqDelay) {
                if ((time_us_64() - subqDelayTime) > c_MaxSubqDelayTime) {
                    g_subqDelay = false;
                    subq.start_subq(g_sector);

                    gpio_put(Pin::SCOR, 1);
                    add_alarm_in_us(
                        135,
                        [](alarm_id_t id, void *user_data) -> int64_t {
                            gpio_put(Pin::SCOR, 0);
                            return 0;
                        },
                        NULL, true);
                }
            } else if (g_sectorSending == g_sector) {
                g_sector = clamp(g_sector + 1, c_sectorMin, c_sectorMax);
                if ((g_sector - g_sectorForTrackUpdate) >= sector_per_track)  // Moved to next track?
                {
                    g_sectorForTrackUpdate = g_sector;
                    g_track = clamp(g_track + 1, c_trackMin, c_trackMax);
                    sector_per_track = sectorsPerTrack(g_track);
                }
                g_subqDelay = true;
                subqDelayTime = time_us_64();
            }
        }
    }
}

int main() {
    set_sys_clock_khz(271200, true);
    sleep_ms(5);

    multicore_launch_core1(picostation::core1Entry);

    initialize();

    core0Entry();
    __builtin_unreachable();
}
