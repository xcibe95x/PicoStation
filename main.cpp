#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/pwm.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "cmd.h"
#include "disc_image.h"
#include "i2s.h"
#include "logging.h"
#include "main.pio.h"
#include "subq.h"
#include "utils.h"
#include "values.h"

#if DEBUG_MISC
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

uint g_latched = 0;                    // Mechacon command latch
volatile bool g_soctEnabled = false; // Serial Read Out Circuit
uint g_countTrack = 0;
uint g_track = 0;
uint g_originalTrack = 0;

int g_sledMoveDirection = SledMove::STOP;

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

// PWM Config
static pwm_config s_cfgClock;
static pwm_config s_cfgDA15;
static pwm_config s_cfgLRCK;
static uint s_clockSliceNum;
static uint s_da15SliceNum;
static uint s_lrckSliceNum;

volatile uint g_currentSens;
volatile bool g_sensData[16] = {
    0, // $0X - FZC
    0, // $1X - AS
    0, // $2X - TZC
    0, // $3X - Misc.
    0, // $4X - XBUSY
    1, // $5X - FOK
    0, // $6X - 0
    0, // $7X - 0
    0, // $8X - 0
    0, // $9X - 0
    1, // $AX - GFS
    0, // $BX - COMP
    0, // $CX - COUT
    0, // $DX - 0
    0, // $EX - OV64
    0  // $FX - 0
};

picostation::DiscImage g_discImage;

void clampSectorTrackLimits();
void initialize();
void updatePlaybackSpeed();
void maybeReset();
void setSens(uint what, bool new_value);
void __time_critical_func(updateMechSens)();

void clampSectorTrackLimits()
{
    static constexpr uint c_trackMax = 20892;  // 73:59:58
    static constexpr int c_sectorMax = 333000; // 74:00:00

    if (g_track < 0 || g_sector < 0)
    {
        DEBUG_PRINT("Clamping sector/track, below 0: track %d sector %d\n", g_track, g_sector);
        g_track = 0;
        g_sector = 0;
        g_sectorForTrackUpdate = 0;
    }

    if (g_track > c_trackMax || g_sector > c_sectorMax)
    {
        DEBUG_PRINT("Clamping sector/track, above max\n");
        g_track = c_trackMax;
        g_sector = trackToSector(g_track);
        g_sectorForTrackUpdate = g_sector;
    }
}

void initialize()
{
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

    gpio_init(Pin::SCEX_DATA);
    gpio_init(Pin::SENS);
    gpio_init(Pin::LMTSW);
    gpio_init(Pin::XLAT);
    gpio_init(Pin::DOOR);
    gpio_init(Pin::RESET);
    gpio_init(Pin::SQCK);
    gpio_init(Pin::SQSO);
    gpio_init(Pin::CMD_CK);

    gpio_init(Pin::LRCK);
    gpio_init(Pin::DA15);
    gpio_init(Pin::CLK);

    gpio_set_dir(Pin::SCEX_DATA, GPIO_OUT);
    gpio_put(Pin::SCEX_DATA, 1);
    gpio_set_dir(Pin::SENS, GPIO_OUT);
    gpio_set_dir(Pin::LMTSW, GPIO_OUT);
    gpio_set_dir(Pin::XLAT, GPIO_IN);
    gpio_set_dir(Pin::DOOR, GPIO_IN);
    gpio_set_dir(Pin::RESET, GPIO_IN);
    gpio_set_dir(Pin::SQCK, GPIO_IN);
    gpio_set_dir(Pin::SQSO, GPIO_OUT);
    gpio_set_dir(Pin::CMD_CK, GPIO_IN);

    gpio_set_dir(Pin::LRCK, GPIO_OUT);
    gpio_set_dir(Pin::DA15, GPIO_OUT);
    gpio_set_dir(Pin::CLK, GPIO_OUT);

    // Main clock
    gpio_set_function(Pin::CLK, GPIO_FUNC_PWM);
    s_clockSliceNum = pwm_gpio_to_slice_num(Pin::CLK);
    s_cfgClock = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&s_cfgClock, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&s_cfgClock, 1);
    pwm_config_set_clkdiv_int(&s_cfgClock, 2);
    pwm_init(s_clockSliceNum, &s_cfgClock, false);
    pwm_set_both_levels(s_clockSliceNum, 1, 1);

    // Data clock
    gpio_set_function(Pin::DA15, GPIO_FUNC_PWM);
    s_da15SliceNum = pwm_gpio_to_slice_num(Pin::DA15);
    s_cfgDA15 = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&s_cfgDA15, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&s_cfgDA15, (1 * 32) - 1);
    pwm_config_set_clkdiv_int(&s_cfgDA15, 4);
    pwm_config_set_output_polarity(&s_cfgDA15, true, true);
    pwm_init(s_da15SliceNum, &s_cfgDA15, false);
    pwm_set_both_levels(s_da15SliceNum, 16, 16);

    // Left/right clock
    gpio_set_function(Pin::LRCK, GPIO_FUNC_PWM);
    s_lrckSliceNum = pwm_gpio_to_slice_num(Pin::LRCK);
    s_cfgLRCK = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&s_cfgLRCK, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&s_cfgLRCK, (48 * 32) - 1);
    pwm_config_set_clkdiv_int(&s_cfgLRCK, 4);
    pwm_init(s_lrckSliceNum, &s_cfgLRCK, false);
    pwm_set_both_levels(s_lrckSliceNum, (48 * 16), (48 * 16));

    gpio_put(Pin::SQSO, 0);
    gpio_put(Pin::SCOR, 0);

    gpio_set_input_hysteresis_enabled(Pin::RESET, true);
    gpio_set_input_hysteresis_enabled(Pin::SQCK, true);
    gpio_set_input_hysteresis_enabled(Pin::XLAT, true);
    gpio_set_input_hysteresis_enabled(Pin::CMD_CK, true);

    uint i2s_pio_offset = pio_add_program(pio0, &i2s_data_program);
    g_subqOffset = pio_add_program(pio0, &subq_program);
    i2s_data_program_init(pio0, SM::I2SDATA, i2s_pio_offset, Pin::DA15, Pin::DA16);

    uint scor_offset = pio_add_program(pio1, &scor_program);
    s_mechachonOffset = pio_add_program(pio1, &mechacon_program);
    g_soctOffset = pio_add_program(pio1, &soct_program);
    scor_program_init(pio1, SM::SCOR, scor_offset, Pin::SCOR);
    mechacon_program_init(pio1, SM::MECHACON, s_mechachonOffset, Pin::CMD_DATA);

    uint64_t start_time = time_us_64();

    pio_sm_set_enabled(pio0, SM::I2SDATA, true);
    pwm_set_mask_enabled((1 << s_lrckSliceNum) | (1 << s_da15SliceNum) | (1 << s_clockSliceNum));

    gpio_set_dir(Pin::RESET, GPIO_OUT);
    gpio_put(Pin::RESET, 0);
    sleep_ms(300);
    gpio_set_dir(Pin::RESET, GPIO_IN);

    while ((time_us_64() - start_time) < 30000)
    {
        if (gpio_get(Pin::RESET) == 0)
        {
            start_time = time_us_64();
        }
    }

    while ((time_us_64() - start_time) < 30000)
    {
        if (gpio_get(Pin::CMD_CK) == 0)
        {
            start_time = time_us_64();
        }
    }

    DEBUG_PRINT("ON!\n");
    multicore_launch_core1(i2sDataThread);
    gpio_set_irq_enabled_with_callback(Pin::XLAT, GPIO_IRQ_EDGE_FALL, true, &interrupt_xlat);
    pio_enable_sm_mask_in_sync(pio1, (1u << SM::SCOR) | (1u << SM::MECHACON));
}

void updatePlaybackSpeed()
{
    if (s_currentPlaybackSpeed != g_targetPlaybackSpeed)
    {
        s_currentPlaybackSpeed = g_targetPlaybackSpeed;
        const uint clock_div = (s_currentPlaybackSpeed == 1) ? 4 : 2;
        pwm_set_mask_enabled(0);
        pwm_config_set_clkdiv_int(&s_cfgDA15, clock_div);
        pwm_config_set_clkdiv_int(&s_cfgLRCK, clock_div);
        pwm_hw->slice[s_da15SliceNum].div = s_cfgDA15.div;
        pwm_hw->slice[s_lrckSliceNum].div = s_cfgLRCK.div;
        pwm_set_mask_enabled((1 << s_lrckSliceNum) | (1 << s_da15SliceNum) | (1 << s_clockSliceNum));
        DEBUG_PRINT("x%i\n", s_currentPlaybackSpeed);
    }
}

void maybeReset()
{
    if (gpio_get(Pin::RESET) == 0)
    {
        DEBUG_PRINT("RESET!\n");
        pio_sm_set_enabled(pio0, SM::SUBQ, false);
        pio_sm_set_enabled(pio1, SM::SOCT, false);
        pio_sm_set_enabled(pio1, SM::MECHACON, false);
        pio_enable_sm_mask_in_sync(pio1, (1u << SM::SCOR) | (1u << SM::MECHACON));

        mechacon_program_init(pio1, SM::MECHACON, s_mechachonOffset, Pin::CMD_DATA);
        g_subqDelay = false;
        g_soctEnabled = false;

        gpio_init(Pin::SQSO);
        gpio_set_dir(Pin::SQSO, GPIO_OUT);
        gpio_put(Pin::SQSO, 0);

        uint64_t start_time = time_us_64();

        while ((time_us_64() - start_time) < 30000)
        {
            if (gpio_get(Pin::RESET) == 0)
            {
                start_time = time_us_64();
            }
        }

        while ((time_us_64() - start_time) < 30000)
        {
            if (gpio_get(Pin::CMD_CK) == 0)
            {
                start_time = time_us_64();
            }
        }

        pio_sm_set_enabled(pio1, SM::MECHACON, true);
    }
}

void __time_critical_func(setSens)(uint what, bool new_value)
{
    g_sensData[what] = new_value;
    if (what == g_currentSens)
    {
        gpio_put(Pin::SENS, new_value);
    }
}

void __time_critical_func(updateMechSens)()
{
    while (!pio_sm_is_rx_fifo_empty(pio1, SM::MECHACON))
    {
        uint c = pio_sm_get_blocking(pio1, SM::MECHACON) >> 24;
        g_latched >>= 8;
        g_latched |= c << 16;
        g_currentSens = c >> 4;
        gpio_put(Pin::SENS, g_sensData[c >> 4]);
    }
}

int __time_critical_func(main)()
{
    static constexpr uint c_MaxTrackMoveTime = 15;   // uS
    static constexpr uint c_MaxSubqDelayTime = 3333; // uS

    uint64_t sled_timer = 0;
    uint64_t subq_delay_time = 0;

    int sector_per_track = sectorsPerTrack(0);

    set_sys_clock_khz(271200, true);
    sleep_ms(5);

    initialize();
    g_coreReady[0] = true;

    while (!g_coreReady[1])
    {
        sleep_ms(1);
    }

    while (true)
    {
        // Limit Switch
        gpio_put(Pin::LMTSW, g_sector > 3000);

        // Update latching, output SENS
        if (mutex_try_enter(&g_mechaconMutex, 0))
        {
            updateMechSens();
            mutex_exit(&g_mechaconMutex);
        }

        updatePlaybackSpeed();
        clampSectorTrackLimits();

        // Check for reset signal
        maybeReset();

        // Soct/Sled/seek/autoseq
        if (g_soctEnabled)
        {
            uint interrupts = save_and_disable_interrupts();
            // waiting for RX FIFO entry does not work.
            sleep_us(300);
            g_soctEnabled = false;
            pio_sm_set_enabled(pio1, SM::SOCT, false);
            restore_interrupts(interrupts);
        }
        else if (g_sledMoveDirection != SledMove::STOP)
        {
            if ((time_us_64() - sled_timer) > c_MaxTrackMoveTime)
            {
                g_track += g_sledMoveDirection; // +1 or -1
                g_sector = trackToSector(g_track);
                g_sectorForTrackUpdate = g_sector;

                const int tracks_moved = g_track - g_originalTrack;
                if (abs(tracks_moved) >= g_countTrack)
                {
                    g_originalTrack = g_track;
                    setSens(SENS::COUT, !g_sensData[SENS::COUT]);
                }

                sled_timer = time_us_64();
            }
        }
        else if (g_sensData[SENS::GFS])
        {
            if (g_subqDelay)
            {
                if ((time_us_64() - subq_delay_time) > c_MaxSubqDelayTime)
                {
                    setSens(SENS::XBUSY, 0);
                    g_subqDelay = false;
                    start_subq(g_sector);
                }
            }
            else if (g_sectorSending == g_sector)
            {
                g_sector++;
                if ((g_sector - g_sectorForTrackUpdate) >= sector_per_track) // Moved to next track?
                {
                    g_sectorForTrackUpdate = g_sector;
                    g_track++;
                    sector_per_track = sectorsPerTrack(g_track);
                }
                g_subqDelay = true;
                subq_delay_time = time_us_64();
            }
        }
    }
}
