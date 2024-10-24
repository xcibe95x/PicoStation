#include "cmd.h"

#include <stdio.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "logging.h"
#include "main.pio.h"
#include "utils.h"
#include "values.h"

#if DEBUG_CMD
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

namespace Command
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
}

extern uint latched;
extern uint countTrack;
extern uint track;
extern uint originalTrack;

extern uint sledMoveDirection;

extern volatile int sector;
extern int sectorForTrackUpdate;

extern int mode;

extern volatile bool sensData[16];
extern volatile bool subqDelay;

extern volatile bool soct;
extern uint soctOffset;
extern volatile uint imageIndex;

static uint jumpTrack = 0;

void setSens(uint what, bool new_value);

inline void autosequence()
{
    const uint subcommand = (latched & 0x0F0000) >> 16;
    //const uint timerRange = (latched & 0x8) >> 3;
    //const uint cancelTimer = (latched & 0xF) >> 4;

    setSens(SENS::XBUSY, (subcommand != 0));

    switch (subcommand)
    {
    case 0x0: // Cancel
        DEBUG_PRINT("Cancel\n");
        return;

    case 0x4: // Fine search - forward
        track = track + jumpTrack;
        DEBUG_PRINT("Fine search - forward %d\n", track);
        break;
    case 0x5: // Fine search - reverse
        track = track - jumpTrack;
        DEBUG_PRINT("Fine search - reverse %d\n", track);
        break;

    case 0x7: // Focus-On
        DEBUG_PRINT("Focus-On\n");
        return;

    case 0x8: // 1 Track Jump - forward
        track = track + 1;
        DEBUG_PRINT("1 Track Jump - forward %d\n", track);
        break;
    case 0x9: // 1 Track Jump - reverse
        track = track - 1;
        DEBUG_PRINT("1 Track Jump - reverse %d\n", track);
        break;

    case 0xA: // 10 Track Jump - forward
        track = track + 10;
        DEBUG_PRINT("10 Track Jump - forward %d\n", track);
        break;
    case 0xB: // 10 Track Jump - reverse
        track = track - 10;
        DEBUG_PRINT("10 Track Jump - reverse %d\n", track);
        break;

    case 0xC: // 2N Track Jump - forward
        track = track + (2 * jumpTrack);
        DEBUG_PRINT("2N Track Jump - forward %d\n", track);
        break;
    case 0xD: // 2N Track Jump - reverse
        track = track - (2 * jumpTrack);
        DEBUG_PRINT("2N Track Jump - reverse %d\n", track);
        break;

    case 0xE: // M Track Move - forward
        track = track + jumpTrack;
        DEBUG_PRINT("M Track Move - forward %d\n", track);
        break;
    case 0xF: // M Track Move - reverse
        track = track - jumpTrack;
        DEBUG_PRINT("M Track Move - reverse %d\n", track);
        break;

    default:
        DEBUG_PRINT("Unsupported command: %x\n", subcommand);
        break;
    }

    sector = trackToSector(track);
    sectorForTrackUpdate = sector;
}

inline void sledMove()
{
    const uint subcommand_move = (latched & 0x030000) >> 16;
    const uint subcommand_track = (latched & 0x0C0000) >> 16;
    switch (subcommand_move)
    {
    case 2:
        sledMoveDirection = SledMove::FORWARD;
        originalTrack = track;
        break;

    case 3:
        sledMoveDirection = SledMove::REVERSE;
        originalTrack = track;
        break;

    default:
        if (sledMoveDirection != SledMove::STOP)
        {
            sector = trackToSector(track);
            sectorForTrackUpdate = sector;
        }
        sledMoveDirection = SledMove::STOP;
        break;
    }

    switch (subcommand_track)
    {
    case 8:
        track++;
        sector = trackToSector(track);
        sectorForTrackUpdate = sector;
        break;
    case 0xC:
        track--;
        sector = trackToSector(track);
        sectorForTrackUpdate = sector;
        break;
    }
}

inline void spindle()
{
    const uint subcommand = (latched & 0x0F0000) >> 16;

    sensData[SENS::GFS] = (subcommand == 6);
    if(!sensData[SENS::GFS])
    {
        subqDelay = false;
    }
}

void __time_critical_func(interrupt_xlat)(uint gpio, uint32_t events)
{
    const uint command = (latched & 0xF00000) >> 20;

    switch (command)
    {
    case Command::SLED: // $2X commands - Sled motor control
        sledMove();
        break;

    case Command::AUTOSEQ: // $4X commands
        autosequence();
        break;

    case Command::JUMP_TRACK: // $7X commands - Auto sequence track jump count setting
        jumpTrack = (latched & 0xFFFF0) >> 4;
        DEBUG_PRINT("jump: %d\n", jumpTrack);
        break;

    case Command::SOCT: // $8X commands - MODE specification
        soct = true;
        pio_sm_set_enabled(pio0, SM::c_subq, false);
        soct_program_init(pio1, SM::c_soct, soctOffset, Pin::SQSO, Pin::SQCK);
        pio_sm_set_enabled(pio1, SM::c_soct, true);
        pio_sm_put_blocking(pio1, SM::c_soct, 0xFFFFFFF);
        break;
    case Command::SPEED: // $9X commands - 1x/2x speed setting
        if ((latched & 0xF40000) == 0x940000)
        {
            mode = 2;
        }
        else if ((latched & 0xF40000) == 0x900000)
        {
            mode = 1;
        }
        break;

    case Command::COUNT_TRACK: // $BX commands - This command sets the traverse monitor count.
        countTrack = (latched & 0xFFFF0) >> 4;
        DEBUG_PRINT("count: %d\n", countTrack);
        break;
    case Command::SPINDLE: // $EX commands - Spindle motor control
        spindle();
        break;

    /*case 0x0: // nop
        break;

    case Command::CUSTOM: // picostation
        switch ((latched & 0x0F0000) >> 16)
        {
        case 0x0: // Iamge 0
            DEBUG_PRINT("Image 0 command!\n");
            imageIndex = 0;
            break;

        case 0x1: // Previous Image
            DEBUG_PRINT("Previous Image command!\n");
            imageIndex = (imageIndex - 1) % c_numImages;
            break;

        case 0x2: // Next Image
            DEBUG_PRINT("Next Image command!\n");
            imageIndex = (imageIndex + 1) % c_numImages;
            break;
        }
        break;*/
    }

    latched = 0;
}