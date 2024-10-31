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
        FOCUS_CONTROL = 0x0,
        TRACKING_MODE = 0x2,
        AUTO_SEQUENCE = 0x4,
        JUMP_COUNT = 0x7,
        MODE_SPEC = 0x8,
        FUNC_SPEC = 0x9,
        MONITOR_COUNT = 0xb,
        SPINDLE = 0xe,
        CUSTOM = 0xf
    };
}

namespace Spindle
{
    enum : uint
    {
        STOP = 0b0000,  // 0
        KICK = 0b1000,  // 8
        BRAKE = 0b1010, // A
        CLVS = 0b1110,  // E
        CLVH = 0b1100,  // C
        CLVP = 0b1111,  // F
        CLVA = 0b0110,  // 6
    };
}

extern uint g_latched;
extern uint g_countTrack;
extern uint g_track;
extern uint g_originalTrack;

extern int g_sledMoveDirection;

extern volatile int g_sector;
extern int g_sectorForTrackUpdate;

extern int g_targetPlaybackSpeed;

extern volatile bool g_sensData[16];
extern volatile bool g_subqDelay;

extern volatile bool g_soctEnabled;
extern uint g_soctOffset;
extern volatile uint g_imageIndex;

static uint s_jumpTrack = 0;

void setSens(uint what, bool new_value);

inline void autosequence()
{
    const uint sub_command = (g_latched & 0x0F0000) >> 16;
    const bool reverse_jump = sub_command & 0x1;
    // const uint timer_range = (g_latched & 0x8) >> 3;
    // const uint cancel_timer = (g_latched & 0xF) >> 4;

    int tracks_to_move = 0;

    setSens(SENS::XBUSY, (sub_command != 0));

    if (sub_command == 0x7) // Focus-On
    {
        DEBUG_PRINT("Focus-On\n");
        return;
    }

    switch (sub_command & 0xe)
    {
    case 0x0: // Cancel
        DEBUG_PRINT("Cancel\n");
        return;

    case 0x4: // Fine search
        tracks_to_move = s_jumpTrack;
        DEBUG_PRINT("Fine search%d\n", g_track);
        break;

    case 0x8: // 1 Track Jump
        tracks_to_move = 1;
        DEBUG_PRINT("1 Track Jump%d\n", g_track);
        break;

    case 0xA: // 10 Track Jump
        tracks_to_move = 10;
        DEBUG_PRINT("10 Track Jump%d\n", g_track);
        break;

    case 0xC: // 2N Track Jump
        tracks_to_move = (2 * s_jumpTrack);
        DEBUG_PRINT("2N Track Jump%d\n", g_track);
        break;

    case 0xE: // M Track Move
        tracks_to_move = s_jumpTrack;
        DEBUG_PRINT("M Track Move%d\n", g_track);
        break;

    default:
        DEBUG_PRINT("Unsupported command: %x\n", sub_command);
        break;
    }

    if (reverse_jump)
    {
        g_track -= tracks_to_move;
    }
    else
    {
        g_track += tracks_to_move;
    }

    g_sector = trackToSector(g_track);
    g_sectorForTrackUpdate = g_sector;
}

inline void funcSpec()
{
    // const bool flfc = g_latched & (1 << 13);
    // const bool biligl_sub = g_latched & (1 << 14);
    // const bool biligl_main = g_latched & (1 << 15);
    // const bool dpll = g_latched & (1 << 16);
    // const bool aseq = g_latched & (1 << 17);
    const bool dspb = g_latched & (1 << 18);
    // const bool dclv = g_latched & (1 << 19);

    if (!dspb) // DSPB = 0 Normal-speed playback, DSPB = 1 Double-speed playback
    {
        g_targetPlaybackSpeed = 1;
    }
    else
    {
        g_targetPlaybackSpeed = 2;
    }
}

inline void modeSpec()
{
    const bool soct = g_latched & (1 << 13);
    // const bool ashs = g_latched & (1 << 14);
    // const bool vco_sel = g_latched & (1 << 15);
    // const bool wsel = g_latched & (1 << 16);
    // const bool dout_mutef = g_latched & (1 << 17);
    // const bool dout_mute = g_latched & (1 << 18);
    // const bool cdrom = g_latched & (1 << 19);

    if (soct)
    {
        g_soctEnabled = true;
        pio_sm_set_enabled(pio0, SM::SUBQ, false);
        soct_program_init(pio1, SM::SOCT, g_soctOffset, Pin::SQSO, Pin::SQCK);
        pio_sm_set_enabled(pio1, SM::SOCT, true);
        pio_sm_put_blocking(pio1, SM::SOCT, 0xFFFFFFF);
    }
}

inline void sledMove()
{
    const uint subcommand_tracking = (g_latched & 0x0C0000) >> 16;
    switch (subcommand_tracking) // Tracking servo
    {
    case 8: // Forward track jump
        g_track++;
        g_sector = trackToSector(g_track);
        g_sectorForTrackUpdate = g_sector;
        break;
    case 0xC: // Reverse track jump
        g_track--;
        g_sector = trackToSector(g_track);
        g_sectorForTrackUpdate = g_sector;
        break;
    }

    const uint subcommand_sled = (g_latched & 0x030000) >> 16;
    switch (subcommand_sled) // Sled servo
    {

    case 2: // Forward sled move
        g_sledMoveDirection = SledMove::FORWARD;
        g_originalTrack = g_track;
        break;

    case 3: // Reverse sled move
        g_sledMoveDirection = SledMove::REVERSE;
        g_originalTrack = g_track;
        break;

    default: // case 0: case 1: // sled servo off/on
        if (g_sledMoveDirection != SledMove::STOP)
        {
            g_sector = trackToSector(g_track);
            g_sectorForTrackUpdate = g_sector;
        }
        g_sledMoveDirection = SledMove::STOP;
        break;
    }
}

inline void spindle()
{
    const uint sub_command = (g_latched & 0x0F0000) >> 16;

    g_sensData[SENS::GFS] = (sub_command == Spindle::CLVA);
    if (!g_sensData[SENS::GFS])
    {
        g_subqDelay = false;
    }

    /*switch (sub_command)
    {
    case Spindle::STOP: // 0
        DEBUG_PRINT("Stop spindle\n");
        break;

    case Spindle::KICK:                // 8
        DEBUG_PRINT("Kick spindle\n"); // Forward rotation
        break;

    case Spindle::BRAKE:                // A
        DEBUG_PRINT("Brake spindle\n"); // Reverse rotation
        break;

    case Spindle::CLVS:        // E
        DEBUG_PRINT("CLVS\n"); // Rough servo mode
        break;

    case Spindle::CLVH:        // C
        DEBUG_PRINT("CLVH\n"); // ?
        break;

    case Spindle::CLVP:        // F
        DEBUG_PRINT("CLVP\n"); // PLL servo mode
        break;

    case Spindle::CLVA: // 6
        // DEBUG_PRINT("CLVA\n"); // Automatic CLVS/CLVP switching mode
        break;
    }*/
}

void __time_critical_func(interrupt_xlat)(uint gpio, uint32_t events)
{
    const uint command = (g_latched & 0xF00000) >> 20;
    const uint latched = g_latched & 0xFFFFF;

    switch (command)
    {
    case Command::TRACKING_MODE: // $2X commands - Sled motor control
        sledMove();
        break;

    case Command::AUTO_SEQUENCE: // $4X commands
        autosequence();
        break;

    case Command::JUMP_COUNT: // $7X commands - Auto sequence track jump count setting
        s_jumpTrack = (g_latched & 0xFFFF0) >> 4;
        DEBUG_PRINT("jump: %d\n", s_jumpTrack);
        break;

    case Command::MODE_SPEC: // $8X commands - MODE specification
        modeSpec();
        break;
    case Command::FUNC_SPEC: // $9X commands - Function specification
        funcSpec();
        break;

    case Command::MONITOR_COUNT: // $BX commands - This command sets the traverse monitor count.
        g_countTrack = (g_latched & 0xFFFF0) >> 4;
        DEBUG_PRINT("count: %d\n", g_countTrack);
        break;
    case Command::SPINDLE: // $EX commands - Spindle motor control
        spindle();
        break;

        /*case Command::FOCUS_CONTROL: // $0X commands - Focus control
        case 0x1:
        case 0x3:
        case 0x5: // Blind/brake
        case 0x6: // Kick
            break;

        case Command::CUSTOM: // picostation
            switch ((g_latched & 0x0F0000) >> 16)
            {
            case 0x0: // Iamge 0
                DEBUG_PRINT("Image 0 command!\n");
                g_imageIndex = 0;
                break;

            case 0x1: // Previous Image
                DEBUG_PRINT("Previous Image command!\n");
                g_imageIndex = (g_imageIndex - 1) % NUM_IMAGES;
                break;

            case 0x2: // Next Image
                DEBUG_PRINT("Next Image command!\n");
                g_imageIndex = (g_imageIndex + 1) % NUM_IMAGES;
                break;
            }
            break;*/
    }

    g_latched = 0;
}