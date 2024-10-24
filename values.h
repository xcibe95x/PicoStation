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

namespace SM
{
    // PIO0
    constexpr uint c_i2sData = 0;
    constexpr uint c_subq = 1;

    // PIO1
    constexpr uint c_scor = 0;
    constexpr uint c_mechacon = 1;
    constexpr uint c_soct = 2;
}

constexpr int c_numImages = 1;

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
