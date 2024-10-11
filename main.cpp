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

// globals
volatile uint latched = 0;
volatile bool soct = 0;

volatile uint count_track = 0;
volatile uint track = 0;
volatile uint original_track = 0;
volatile uint64_t sled_timer = 0;
volatile uint sled_move_direction = SledMove::STOP;

volatile uint sector = 0;
volatile uint sector_for_track_update = 0;
volatile uint sector_sending = !0;

uint64_t subq_start_time = 0;
int subq_delay = 0;

volatile int current_logical_track = 0;

int prevMode = 1;
volatile int mode = 1;

mutex_t mechacon_mutex;
volatile bool core_ready[2] = {false, false};

uint i2s_pio_offset;
uint mechachon_sm_offset;
uint soct_offset;
volatile uint subq_offset;

// PWM Config
pwm_config cfg_CLOCK;
pwm_config cfg_LRCK;
pwm_config cfg_DA15;
uint slice_num_CLOCK;
uint slice_num_DA15;
uint slice_num_LRCK;

volatile uint current_sens;
volatile bool SENS_data[16] = {
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

constexpr uint c_TrackMoveTime = 15; // uS

picostation::DiscImage discImage;

void clampSectorTrackLimits();
void initialize();
void maybeChangeMode();
void maybeReset();
void select_sens(uint new_sens);
void set_sens(uint what, bool new_value);
void updateMechSens();

void clampSectorTrackLimits()
{
    if (track < 0 || sector < 0)
    {
        track = 0;
        sector = 0;
        sector_for_track_update = 0;
    }

    if (track > 24000 || sector > 440000)
    {
        track = 24000;
        sector = track_to_sector(track);
        sector_for_track_update = sector;
    }
}

void initialize()
{
    srand(time(NULL));
    mutex_init(&mechacon_mutex);

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

    gpio_set_function(Pin::CLK, GPIO_FUNC_PWM);
    slice_num_CLOCK = pwm_gpio_to_slice_num(Pin::CLK);
    cfg_CLOCK = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg_CLOCK, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&cfg_CLOCK, 1);
    pwm_config_set_clkdiv_int(&cfg_CLOCK, 2);
    pwm_init(slice_num_CLOCK, &cfg_CLOCK, false);
    pwm_set_both_levels(slice_num_CLOCK, 1, 1);

    gpio_set_function(Pin::DA15, GPIO_FUNC_PWM);
    slice_num_DA15 = pwm_gpio_to_slice_num(Pin::DA15);
    cfg_DA15 = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg_DA15, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&cfg_DA15, (1 * 32) - 1);
    pwm_config_set_clkdiv_int(&cfg_DA15, 4);
    pwm_config_set_output_polarity(&cfg_DA15, true, true);
    pwm_init(slice_num_DA15, &cfg_DA15, false);
    pwm_set_both_levels(slice_num_DA15, 16, 16);

    gpio_set_function(Pin::LRCK, GPIO_FUNC_PWM);
    slice_num_LRCK = pwm_gpio_to_slice_num(Pin::LRCK);
    cfg_LRCK = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg_LRCK, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&cfg_LRCK, (48 * 32) - 1);
    pwm_config_set_clkdiv_int(&cfg_LRCK, 4);
    pwm_init(slice_num_LRCK, &cfg_LRCK, false);
    pwm_set_both_levels(slice_num_LRCK, (48 * 16), (48 * 16));

    gpio_put(Pin::SQSO, 0);
    gpio_put(Pin::SCOR, 0);

    gpio_set_input_hysteresis_enabled(Pin::RESET, true);
    gpio_set_input_hysteresis_enabled(Pin::SQCK, true);
    gpio_set_input_hysteresis_enabled(Pin::XLAT, true);
    gpio_set_input_hysteresis_enabled(Pin::CMD_CK, true);

    uint i2s_pio_offset = pio_add_program(pio0, &i2s_data_program);
    subq_offset = pio_add_program(pio0, &subq_program);
    i2s_data_program_init(pio0, SM::c_i2sData, i2s_pio_offset, Pin::DA15, Pin::DA16);

    uint scor_offset = pio_add_program(pio1, &scor_program);
    mechachon_sm_offset = pio_add_program(pio1, &mechacon_program);
    soct_offset = pio_add_program(pio1, &soct_program);
    scor_program_init(pio1, SM::c_scor, scor_offset, Pin::SCOR);
    mechacon_program_init(pio1, SM::c_mechacon, mechachon_sm_offset, Pin::CMD_DATA);

    uint64_t startTime = time_us_64();

    pio_sm_set_enabled(pio0, SM::c_i2sData, true);
    pwm_set_mask_enabled((1 << slice_num_LRCK) | (1 << slice_num_DA15) | (1 << slice_num_CLOCK));

    gpio_set_dir(Pin::RESET, GPIO_OUT);
    gpio_put(Pin::RESET, 0);
    sleep_ms(300);
    gpio_set_dir(Pin::RESET, GPIO_IN);

    while ((time_us_64() - startTime) < 30000)
    {
        if (gpio_get(Pin::RESET) == 0)
        {
            startTime = time_us_64();
        }
    }

    while ((time_us_64() - startTime) < 30000)
    {
        if (gpio_get(Pin::CMD_CK) == 0)
        {
            startTime = time_us_64();
        }
    }

    DEBUG_PRINT("ON!\n");
    multicore_launch_core1(i2s_data_thread);
    gpio_set_irq_enabled_with_callback(Pin::XLAT, GPIO_IRQ_EDGE_FALL, true, &interrupt_xlat);
    pio_enable_sm_mask_in_sync(pio1, (1u << SM::c_scor) | (1u << SM::c_mechacon));
}

void maybeChangeMode()
{
    if (prevMode == 1 && mode == 2)
    {
        pwm_set_mask_enabled(0);
        pwm_config_set_clkdiv_int(&cfg_DA15, 2);
        pwm_config_set_clkdiv_int(&cfg_LRCK, 2);
        pwm_hw->slice[slice_num_DA15].div = cfg_DA15.div;
        pwm_hw->slice[slice_num_LRCK].div = cfg_LRCK.div;
        pwm_set_mask_enabled((1 << slice_num_LRCK) | (1 << slice_num_DA15) | (1 << slice_num_CLOCK));
        prevMode = 2;
        DEBUG_PRINT("x2\n");
    }
    else if (prevMode == 2 && mode == 1)
    {
        pwm_set_mask_enabled(0);
        pwm_config_set_clkdiv_int(&cfg_DA15, 4);
        pwm_config_set_clkdiv_int(&cfg_LRCK, 4);
        pwm_hw->slice[slice_num_DA15].div = cfg_DA15.div;
        pwm_hw->slice[slice_num_LRCK].div = cfg_LRCK.div;
        pwm_set_mask_enabled((1 << slice_num_LRCK) | (1 << slice_num_DA15) | (1 << slice_num_CLOCK));
        prevMode = 1;
        DEBUG_PRINT("x1\n");
    }
}

void maybeReset()
{
    if (gpio_get(Pin::RESET) == 0)
    {
        DEBUG_PRINT("RESET!\n");
        pio_sm_set_enabled(pio0, SM::c_subq, false);
        pio_sm_set_enabled(pio1, SM::c_soct, false);
        pio_sm_set_enabled(pio1, SM::c_mechacon, false);
        pio_enable_sm_mask_in_sync(pio1, (1u << SM::c_scor) | (1u << SM::c_mechacon));

        mechacon_program_init(pio1, SM::c_mechacon, mechachon_sm_offset, Pin::CMD_DATA);
        subq_delay = 0;
        soct = 0;

        gpio_init(Pin::SQSO);
        gpio_set_dir(Pin::SQSO, GPIO_OUT);
        gpio_put(Pin::SQSO, 0);

        uint64_t startTime = time_us_64();

        while ((time_us_64() - startTime) < 30000)
        {
            if (gpio_get(Pin::RESET) == 0)
            {
                startTime = time_us_64();
            }
        }

        while ((time_us_64() - startTime) < 30000)
        {
            if (gpio_get(Pin::CMD_CK) == 0)
            {
                startTime = time_us_64();
            }
        }

        pio_sm_set_enabled(pio1, SM::c_mechacon, true);
    }
}

void select_sens(uint new_sens)
{
    current_sens = new_sens;
}

void set_sens(uint what, bool new_value)
{
    SENS_data[what] = new_value;
    if (what == current_sens)
    {
        gpio_put(Pin::SENS, new_value);
    }
}

void updateMechSens()
{
    while (!pio_sm_is_rx_fifo_empty(pio1, SM::c_mechacon))
    {
        uint c = pio_sm_get_blocking(pio1, SM::c_mechacon) >> 24;
        latched >>= 8;
        latched |= c << 16;
        select_sens(latched >> 20);
        gpio_put(Pin::SENS, SENS_data[latched >> 20]);
    }
}

int main()
{
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(100);

#if DEBUG_LOGGING_ENABLED
    stdio_init_all();
    stdio_set_chars_available_callback(NULL, NULL);
    sleep_ms(1250);
#endif

    set_sys_clock_khz(271200, true);
    sleep_ms(5);

    DEBUG_PRINT("Initializing...\n");
    initialize();
    int sectors_per_track_i = sectors_per_track(0);
    uint64_t subq_delay_time = 0;

    core_ready[0] = true;

    while (!core_ready[1])
    {
        sleep_ms(1);
    }

    while (true)
    {
        // Limit Switch
        gpio_put(Pin::LMTSW, sector > 3000);

        // Update latching, output SENS
        if (mutex_try_enter(&mechacon_mutex, 0))
        {

            updateMechSens();
            mutex_exit(&mechacon_mutex);
        }

        // X1/X2 mode/speed
        maybeChangeMode();

        // Track between 0 and 24000, sector between 0 and 440000
        clampSectorTrackLimits();

        // Check for reset signal
        maybeReset();

        // Soct/Sled/seek/autoseq
        if (soct)
        {
            uint interrupts = save_and_disable_interrupts();
            // waiting for RX FIFO entry does not work.
            sleep_us(300);
            soct = 0;
            pio_sm_set_enabled(pio1, SM::c_soct, false);
            subq_start_time = time_us_64();
            restore_interrupts(interrupts);
        }
        else if (sled_move_direction == SledMove::FORWARD)
        {
            if ((time_us_64() - sled_timer) > c_TrackMoveTime)
            {
                sled_timer = time_us_64();
                track++;
                sector = track_to_sector(track);
                sector_for_track_update = sector;

                if ((track - original_track) >= count_track)
                {
                    original_track = track;
                    set_sens(SENS::COUT, !SENS_data[SENS::COUT]);
                }
            }
        }
        else if (sled_move_direction == SledMove::REVERSE)
        {
            if ((time_us_64() - sled_timer) > c_TrackMoveTime)
            {
                sled_timer = time_us_64();
                track--;
                sector = track_to_sector(track);
                sector_for_track_update = sector;
                if ((original_track - track) >= count_track)
                {
                    original_track = track;
                    set_sens(SENS::COUT, !SENS_data[SENS::COUT]);
                }
            }
        }
        else if (SENS_data[SENS::GFS])
        {
            if (sector < 4650 && (time_us_64() - subq_start_time) > 6333)
            {
                subq_start_time = time_us_64();
                start_subq();
                sector++;
                if ((sector - sector_for_track_update) >= sectors_per_track_i)
                {
                    sector_for_track_update = sector;
                    track++;
                    sectors_per_track_i = sectors_per_track(track);
                }
            }
            else
            {
                if (sector_sending == sector)
                {
                    if (!subq_delay)
                    {
                        sector++;
                        if ((sector - sector_for_track_update) >= sectors_per_track_i)
                        {
                            sector_for_track_update = sector;
                            track++;
                            sectors_per_track_i = sectors_per_track(track);
                        }
                        subq_delay = 1;
                        subq_delay_time = time_us_64();
                    }
                }

                if (subq_delay && (sector >= 4650 && (time_us_64() - subq_delay_time) > 3333))
                {
                    set_sens(SENS::XBUSY, 0);
                    subq_delay = 0;
                    start_subq();
                }
            }
        }
        else
        {
            subq_delay = 0;
        }
    }
}
