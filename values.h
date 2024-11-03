#pragma once

#include "pico/stdlib.h"

// GPIO pinouts
namespace Pin
{
    enum : uint
    {
        XLAT = 0,
        SQCK = 1,
        LMTSW = 2,
        SCEX_DATA = 4,
        DOOR = 6,
        RESET = 7,
        SENS = 14,
        DA15 = 15,
        DA16 = 16,
        LRCK = 17,
        SCOR = 18,
        SQSO = 19,
        CLK = 21,
        CMD_DATA = 26,
        CMD_CK = 27
    };
};
// C2PO, WFCK is always GND

namespace SENS
{
    enum : uint
    {
        FZC = 0x0,
        AS = 0x1,
        TZC = 0x2,
        XBUSY = 0x4,
        FOK = 0x5,
        GFS = 0xa,
        COMP = 0xb,
        COUT = 0xc,
        OV64 = 0xe
    };
}

namespace SledMove
{
    enum : int
    {
        REVERSE = -1,
        STOP = 0,
        FORWARD = 1
    };
}

namespace SM
{
    // PIO0
    constexpr uint I2SDATA = 0;
    constexpr uint SUBQ = 1;

    // PIO1
    constexpr uint SCOR = 0;
    constexpr uint MECHACON = 1;
    constexpr uint SOCT = 2;
}

constexpr int NUM_IMAGES = 1;
constexpr int c_leadIn = 4500;
constexpr int c_preGap = 150;

static constexpr size_t c_cdSamples = 588;
static constexpr size_t c_cdSamplesBytes = c_cdSamples * 2 * 2; // 2352
