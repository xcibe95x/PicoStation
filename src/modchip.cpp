#include "modchip.h"

#include <stdio.h>

#include "cmd.h"
#include "disc_image.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "values.h"

#define DEBUG_PRINT(...) printf(__VA_ARGS__)

void picostation::ModChip::init() {
    uart_init(uart1, 250);
    gpio_set_function(Pin::SCEX_DATA, GPIO_FUNC_UART);
    gpio_set_outover(Pin::SCEX_DATA, GPIO_OVERRIDE_INVERT);
    uart_set_hw_flow(uart1, false, false);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, false);

    m_psneeTimer = time_us_64();
}

void picostation::ModChip::injectLicenseString(const int sector, MechCommand &mechCommand) {
    static constexpr char s_licenseData[3][5] = {"SCEA", "SCEE", "SCEI"};

    const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
    const bool isDataDisc = g_discImage.hasData();
    const bool soctDisabled = !mechCommand.getSoct();
    const bool gfsSet = mechCommand.getSens(SENS::GFS);
    const uint64_t timeElapsed = time_us_64() - m_psneeTimer;

    static int psnee_hysteresis = 0;

    // Returns false if the PSNEE loop should be aborted, true if the timer has elapsed
    auto psneeWaitBlockingWithAbort = [&mechCommand, this](const uint64_t waitTime) {
        while ((time_us_64() - m_psneeTimer) < waitTime) {
            const int sector = g_driveMechanics.getSector();
            const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
            const bool soctDisabled = !mechCommand.getSoct();
            const bool gfsSet = mechCommand.getSens(SENS::GFS);

            if (!soctDisabled || !gfsSet || !inWobbleGroove) {
                return false;
            }
        }
        return true;
    };

    // Check for conditions to trigger PSNEE, increase hysteresis counter, and reset timer for next state
    if (inWobbleGroove && gfsSet && soctDisabled && isDataDisc) {
        if (timeElapsed > 13333) {
            psnee_hysteresis++;
            m_psneeTimer = time_us_64();

            // if hyteresis counter is over 100, begin psnee loop
            if (psnee_hysteresis > 100) {
                psnee_hysteresis = 0;
                DEBUG_PRINT("+SCEX\n");
                m_psneeTimer = time_us_64();

                // Wait 90ms
                if (!psneeWaitBlockingWithAbort(90000U)) {
                    goto abort_psnee;
                }

                // Send the 3 license strings, twice each
                for (int i = 0; i < 6; i++) {
                    uart_puts(uart1, s_licenseData[i % 3]);
                    uart_tx_wait_blocking(uart1);

                    m_psneeTimer = time_us_64();

                    // Wait 90ms between strings
                    if (!psneeWaitBlockingWithAbort(90000U)) {
                        goto abort_psnee;
                    }
                }

            abort_psnee:
                gpio_put(Pin::SCEX_DATA, 0);
                m_psneeTimer = time_us_64();
                DEBUG_PRINT("-SCEX\n");
            }
        }
    } else {
        psnee_hysteresis = 0;
        m_psneeTimer = time_us_64();
    }

    /*uart_putc_raw(uart1, 'S');
    uart_putc_raw(uart1, 'C');
    uart_putc_raw(uart1, 'E');
    uart_putc_raw(uart1, 'A');*/
};
