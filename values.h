#pragma once

#include "pico/stdlib.h"

// GPIO pinouts
struct Pin
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
        DA15 = 15, // next pin is DA16
        LRCK = 17,
        SCOR = 18,
        SQSO = 19,
        CLK = 21,
        CMD_DATA = 26,
        CMD_CK = 27
    };
};
// C2PO, WFCK is always GND

// PIO0
constexpr uint I2S_DATA_SM = 0;

// PIO1
constexpr uint SCOR_SM = 0;
constexpr uint MECHACON_SM = 1;
constexpr uint SOCT_SM = 2;
constexpr uint SUBQ_SM = 3;

//
constexpr size_t CD_SAMPLES = 588;
constexpr size_t CD_SAMPLES_BYTES = CD_SAMPLES * 2 * 2;

//
constexpr uint TRACK_MOVE_TIME_US = 15;

//
constexpr int PSNEE_SECTOR_LIMIT = 4500;
constexpr int SECTOR_CACHE = 50;

constexpr int NUM_IMAGES = 4;

struct Command
{
    enum : uint
    {
        SLED = 0x2,
        AUTOSEQ = 0x4,
        JUMP_TRACK = 0x7,
        SOCT = 0x8,
        SPEED = 0x9,
        COUNT_TRACK = 0xb,
        SPINDLE = 0xe,
        CUSTOM = 0xf
    };
};

struct SENS
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
};

struct SledMove
{
    enum : uint
    {
        STOP = 0x00,
        REVERSE = 0x11,
        FORWARD = 0x22
    };
};
