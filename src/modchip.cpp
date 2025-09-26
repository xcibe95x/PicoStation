#include "modchip.h"

#include <stdio.h>
#include <string.h>

#include <array>
#include <cctype>
#include <algorithm>

#include "cmd.h"
#include "disc_image.h"
#include "ff.h"
#include "hardware/uart.h"
#include "logging.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "values.h"

#if DEBUG_MODCHIP
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

namespace {

enum class ConsoleRegion { AUTO, NTSC_U, PAL, NTSC_J };

struct LicenseConfig {
    std::array<uint8_t, 3> order = {0, 1, 2};
    uint8_t activeCount = 3;
};

LicenseConfig g_licenseConfig;

constexpr std::array<const char *, 3> g_licenseStrings = {"SCEA", "SCEE", "SCEI"};

void applyRegionPreference(const ConsoleRegion region) {
    switch (region) {
        case ConsoleRegion::PAL:
            g_licenseConfig.order = {1, 0, 2};
            break;
        case ConsoleRegion::NTSC_J:
            g_licenseConfig.order = {2, 0, 1};
            break;
        case ConsoleRegion::NTSC_U:
        case ConsoleRegion::AUTO:
        default:
            g_licenseConfig.order = {0, 1, 2};
            break;
    }
}

bool parseBool(const char *value, bool &result) {
    if (value == nullptr) {
        return false;
    }

    char lowered[16] = {0};
    size_t length = std::min(sizeof(lowered) - 1, strlen(value));
    for (size_t i = 0; i < length; ++i) {
        lowered[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }

    if (!strcmp(lowered, "1") || !strcmp(lowered, "true") || !strcmp(lowered, "yes") || !strcmp(lowered, "on")) {
        result = true;
        return true;
    }

    if (!strcmp(lowered, "0") || !strcmp(lowered, "false") || !strcmp(lowered, "no") || !strcmp(lowered, "off")) {
        result = false;
        return true;
    }

    return false;
}

bool parseRegion(const char *value, ConsoleRegion &region) {
    if (value == nullptr) {
        return false;
    }

    char lowered[16] = {0};
    size_t length = std::min(sizeof(lowered) - 1, strlen(value));
    for (size_t i = 0; i < length; ++i) {
        lowered[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }

    if (!strcmp(lowered, "pal")) {
        region = ConsoleRegion::PAL;
        return true;
    }

    if (!strcmp(lowered, "ntsc-j") || !strcmp(lowered, "ntscj") || !strcmp(lowered, "ntscjp") ||
        !strcmp(lowered, "ntscjpn") || !strcmp(lowered, "japan")) {
        region = ConsoleRegion::NTSC_J;
        return true;
    }

    if (!strcmp(lowered, "ntsc-u") || !strcmp(lowered, "ntscu") || !strcmp(lowered, "ntscus") ||
        !strcmp(lowered, "ntsc")) {
        region = ConsoleRegion::NTSC_U;
        return true;
    }

    if (!strcmp(lowered, "auto") || !strcmp(lowered, "default")) {
        region = ConsoleRegion::AUTO;
        return true;
    }

    return false;
}

void trim(char *value) {
    if (value == nullptr) {
        return;
    }

    size_t length = strlen(value);
    while (length > 0 && std::isspace(static_cast<unsigned char>(value[length - 1]))) {
        value[--length] = '\0';
    }

    size_t start = 0;
    while (value[start] && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    if (start > 0) {
        memmove(value, value + start, strlen(value + start) + 1);
    }
}

void toLowerInPlace(char *value) {
    if (value == nullptr) {
        return;
    }

    for (size_t i = 0; value[i] != '\0'; ++i) {
        value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }
}

}  // namespace

void picostation::ModChip::endLicenseSequence() {
    gpio_put(Pin::SCEX_DATA, 0);
    m_modchipTimer = time_us_64();
    DEBUG_PRINT("-SCEX\n");
}

void picostation::ModChip::init() {
    uart_init(uart1, 250);
    gpio_set_function(Pin::SCEX_DATA, GPIO_FUNC_UART);
    gpio_set_outover(Pin::SCEX_DATA, GPIO_OVERRIDE_INVERT);
    uart_set_hw_flow(uart1, false, false);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, false);

    m_modchipTimer = time_us_64();
}

void picostation::ModChip::sendLicenseString(const int sector, MechCommand &mechCommand) {
    static int modchip_hysteresis = 0;

    // Gather all conditions in one place
    const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
    const bool isDataDisc = g_discImage.hasData();
    const bool soctDisabled = !mechCommand.getSoct();
    const bool gfsSet = mechCommand.getSens(SENS::GFS);
    const bool shouldActivateModchip = inWobbleGroove && gfsSet && soctDisabled && isDataDisc;
    const uint64_t timeElapsed = time_us_64() - m_modchipTimer;

    // Helper function to wait with abort conditions
    auto waitWithAbort = [&mechCommand, this](const uint64_t waitTimeUS) -> bool {
        const uint64_t startTime = time_us_64();
        while ((time_us_64() - startTime) < waitTimeUS) {
            const int sector = g_driveMechanics.getSector();
            const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
            const bool soctDisabled = !mechCommand.getSoct();
            const bool gfsSet = mechCommand.getSens(SENS::GFS);

            if (!soctDisabled || !gfsSet || !inWobbleGroove) {
                return false;
            }
            sleep_us(50);
        }
        return true;
    };

    if (shouldActivateModchip) {
        if (timeElapsed > 13333) {
            modchip_hysteresis++;
            m_modchipTimer = time_us_64();

            if (modchip_hysteresis > 100) {
                modchip_hysteresis = 0;
                DEBUG_PRINT("+SCEX\n");

                const uint8_t licenseCount = std::max<uint8_t>(1, g_licenseConfig.activeCount);
                const uint8_t iterations = licenseCount * 2;
                for (uint8_t i = 0; i < iterations && waitWithAbort(90000U); i++) {
                    const uint8_t sequenceIndex = g_licenseConfig.order[i % licenseCount];
                    const char *license = g_licenseStrings[sequenceIndex];
                    DEBUG_PRINT("%s\n", license);
                    uart_puts(uart1, license);
                    uart_tx_wait_blocking(uart1);
                }

                endLicenseSequence();
            }
        }
    } else {
        modchip_hysteresis = 0;
        m_modchipTimer = time_us_64();
    }
};

void picostation::ModChip::loadConfiguration() {
    FIL file;
    FRESULT result = f_open(&file, "PICOSTATION.CFG", FA_READ | FA_OPEN_EXISTING);

    if (result != FR_OK) {
        result = f_open(&file, "picostation.cfg", FA_READ | FA_OPEN_EXISTING);
    }

    if (result != FR_OK) {
        applyRegionPreference(ConsoleRegion::AUTO);
        g_licenseConfig.activeCount = 3;
        return;
    }

    ConsoleRegion selectedRegion = ConsoleRegion::AUTO;
    bool regionSet = false;
    bool lockRegion = false;
    bool lockSet = false;

    char line[128];
    while (f_gets(line, sizeof(line), &file)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        char *equalsPosition = strchr(line, '=');
        if (!equalsPosition) {
            continue;
        }

        *equalsPosition = '\0';
        char *value = equalsPosition + 1;
        trim(line);
        trim(value);
        toLowerInPlace(line);
        toLowerInPlace(value);

        if (!*line) {
            continue;
        }

        if (!strcmp(line, "scex_region")) {
            ConsoleRegion regionCandidate;
            if (parseRegion(value, regionCandidate)) {
                selectedRegion = regionCandidate;
                regionSet = true;
            }
        } else if (!strcmp(line, "scex_lock_region")) {
            bool lockCandidate;
            if (parseBool(value, lockCandidate)) {
                lockRegion = lockCandidate;
                lockSet = true;
            }
        }
    }

    f_close(&file);

    if (!regionSet) {
        selectedRegion = ConsoleRegion::AUTO;
    }

    applyRegionPreference(selectedRegion);
    g_licenseConfig.activeCount = lockSet && lockRegion ? 1 : 3;
}
