#include "cmd.h"

#include <stdint.h>
#include <stdio.h>

#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "utils.h"
#include "values.h"

#if DEBUG_CMD
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

void setSens(uint what, bool new_value);

namespace picostation {
namespace mechcommand {
namespace commands {
enum : uint {
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

namespace SpindleCommands {
enum : uint {
    STOP = 0b0000,   // 0
    KICK = 0b1000,   // 8
    BRAKE = 0b1010,  // A
    CLVS = 0b1110,   // E
    CLVH = 0b1100,   // C
    CLVP = 0b1111,   // F
    CLVA = 0b0110,   // 6
};
}

static int s_autoSeqTrack = 0;
static int s_jumpTrack = 0;
static alarm_id_t s_autoSeqAlarmID = 0;

static uint s_currentSens;
static uint s_latched = 0;  // Command latch
static bool s_sensData[16] = {
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

static void audioControl(const uint latched);
static void autoSequence(const uint latched);
static void funcSpec(const uint latched);
static void modeSpec(const uint latched);
static void trackingMode(const uint latched);
static void spindleControl(const uint latched);
}  // namespace mechcommand
}  // namespace picostation

static inline void picostation::mechcommand::audioControl(const uint latched) {
    const uint pct2_bit = (1 << 14);
    const uint pct1_bit = (1 << 15);
    const uint mute_bit = (1 << 17);

    if (latched & mute_bit) {
        // g_audioCtrlMode = 0;
        DEBUG_PRINT("Mute\n");
        return;
    }

    g_audioCtrlMode = (latched & (pct1_bit | pct2_bit)) >> 14;
    /*switch (g_audioCtrlMode) {
        case audioControlModes::NORMAL:
        case audioControlModes::ALTNORMAL:
            g_audioPeak = 0;
            DEBUG_PRINT("NORMAL\n");
            break;

        case audioControlModes::LEVELMETER:
            DEBUG_PRINT("LEVELMETER\n");
            break;

        case audioControlModes::PEAKMETER:
            DEBUG_PRINT("PEAKMETER\n");
            break;
    }*/
}

static inline void picostation::mechcommand::autoSequence(const uint latched)  // $4X
{
    const uint subCommand = (latched & 0x0F0000) >> 16;
    const bool reverseJump = subCommand & 0x1;
    // const uint timer_range = (latched & 0x8) >> 3;
    // const uint cancel_timer = (latched & 0xF) >> 4;

    int tracks_to_move = 0;

    s_sensData[SENS::XBUSY] = (subCommand != 0);

    if (subCommand == 0x7)  // Focus-On
    {
        DEBUG_PRINT("Focus-On\n");
        return;
    }

    switch (subCommand & 0xe) {
        case 0x0:  // Cancel
            DEBUG_PRINT("Cancel\n");
            return;

        case 0x4:  // Fine search
            tracks_to_move = s_jumpTrack;
            DEBUG_PRINT("Fine search%d\n", g_track);
            break;

        case 0x8:  // 1 Track Jump
            tracks_to_move = 1;
            DEBUG_PRINT("1 Track Jump%d\n", g_track);
            break;

        case 0xA:  // 10 Track Jump
            tracks_to_move = 10;
            DEBUG_PRINT("10 Track Jump%d\n", g_track);
            break;

        case 0xC:  // 2N Track Jump
            tracks_to_move = (2 * s_jumpTrack);
            DEBUG_PRINT("2N Track Jump%d\n", g_track);
            break;

        case 0xE:  // M Track Move
            tracks_to_move = s_jumpTrack;
            DEBUG_PRINT("M Track Move%d\n", g_track);
            break;

        default:
            DEBUG_PRINT("Unsupported command: %x\n", subCommand);
            break;
    }

    if (reverseJump) {
        s_autoSeqTrack = g_track - tracks_to_move;
    } else {
        s_autoSeqTrack = g_track + tracks_to_move;
    }

    if (!s_autoSeqAlarmID) {
        s_autoSeqAlarmID = add_alarm_in_ms(
            15,
            [](alarm_id_t id, void *user_data) -> int64_t {
                const int track = *(int *)user_data;
                s_autoSeqAlarmID = 0;
                g_track = clamp(track, c_trackMin, c_trackMax);
                g_sectorForTrackUpdate = trackToSector(g_track);
                g_sector = g_sectorForTrackUpdate;
                s_sensData[SENS::XBUSY] = 0;
                return 0;
            },
            &s_autoSeqTrack, true);
    }
}

static inline void picostation::mechcommand::funcSpec(const uint latched)  // $9X
{
    // const bool flfc = latched & (1 << 13);
    // const bool biligl_sub = latched & (1 << 14);
    // const bool biligl_main = latched & (1 << 15);
    // const bool dpll = latched & (1 << 16);
    // const bool aseq = latched & (1 << 17);
    const bool dspb = latched & (1 << 18);
    // const bool dclv = latched & (1 << 19);

    if (!dspb)  // DSPB = 0 Normal-speed playback, DSPB = 1 Double-speed playback
    {
        g_targetPlaybackSpeed = 1;
    } else {
        g_targetPlaybackSpeed = 2;
    }
}

static inline void picostation::mechcommand::modeSpec(const uint latched)  // $8X
{
    const bool soct = latched & (1 << 13);
    // const bool ashs = latched & (1 << 14);
    // const bool vco_sel = latched & (1 << 15);
    // const bool wsel = latched & (1 << 16);
    // const bool dout_mutef = latched & (1 << 17);
    // const bool dout_mute = latched & (1 << 18);
    // const bool cdrom = latched & (1 << 19);

    if (soct) {
        g_soctEnabled = true;
        pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
        soct_program_init(PIOInstance::SOCT, SM::SOCT, g_soctOffset, Pin::SQSO, Pin::SQCK);
        pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, true);
        pio_sm_put_blocking(PIOInstance::SOCT, SM::SOCT, 0xFFFFFFF);
    }
}

static inline void picostation::mechcommand::trackingMode(const uint latched)  // $2X
{
    const uint subcommand_tracking = (latched & 0x0C0000) >> 16;
    switch (subcommand_tracking)  // Tracking servo
    {
        case 8:  // Forward track jump
            g_track = clamp(g_track + 1, c_trackMin, c_trackMax);
            g_sectorForTrackUpdate = trackToSector(g_track);
            g_sector = g_sectorForTrackUpdate;
            break;
        case 0xC:  // Reverse track jump
            g_track = clamp(g_track - 1, c_trackMin, c_trackMax);
            g_sectorForTrackUpdate = trackToSector(g_track);
            g_sector = g_sectorForTrackUpdate;
            break;
    }

    const uint subcommand_sled = (latched & 0x030000) >> 16;
    switch (subcommand_sled)  // Sled servo
    {
        case 2:  // Forward sled move
            g_sledMoveDirection = SledMove::FORWARD;
            g_originalTrack = g_track;
            break;

        case 3:  // Reverse sled move
            g_sledMoveDirection = SledMove::REVERSE;
            g_originalTrack = g_track;
            break;

        default:  // case 0: case 1: // sled servo off/on
            if (g_sledMoveDirection != SledMove::STOP) {
            g_sectorForTrackUpdate = trackToSector(g_track);
            g_sector = g_sectorForTrackUpdate;
            }
            g_sledMoveDirection = SledMove::STOP;
            return;
    }

    g_sledTimer = time_us_64();
}

static inline void picostation::mechcommand::spindleControl(const uint latched) {
    const uint subCommand = (latched & 0x0F0000) >> 16;

    s_sensData[SENS::GFS] = (subCommand == SpindleCommands::CLVA);
    if (!s_sensData[SENS::GFS]) {
        g_subqDelay = false;
    }

    /*switch (subCommand)
    {
    case SpindleCommands::STOP: // 0
        DEBUG_PRINT("Stop spindleControl\n");
        break;

    case SpindleCommands::KICK:                // 8
        DEBUG_PRINT("Kick spindle\n"); // Forward rotation
        break;

    case SpindleCommands::BRAKE:                // A
        DEBUG_PRINT("Brake spindle\n"); // Reverse rotation
        break;

    case SpindleCommands::CLVS:        // E
        DEBUG_PRINT("CLVS\n"); // Rough servo mode
        break;

    case SpindleCommands::CLVH:        // C
        DEBUG_PRINT("CLVH\n"); // ?
        break;

    case SpindleCommands::CLVP:        // F
        DEBUG_PRINT("CLVP\n"); // PLL servo mode
        break;

    case SpindleCommands::CLVA: // 6
        // DEBUG_PRINT("CLVA\n"); // Automatic CLVS/CLVP switching mode
        break;
    }*/
}

void __time_critical_func(picostation::mechcommand::interrupt_xlat)(uint gpio, uint32_t events) {
    const uint latched = s_latched;
    const uint command = (latched & 0xF00000) >> 20;
    s_latched = 0;

    switch (command) {
        case commands::TRACKING_MODE:  // $2X commands - Tracking and sled servo control
            trackingMode(latched);
            break;

        case commands::AUTO_SEQUENCE:  // $4X commands
            autoSequence(latched);
            break;

        case commands::JUMP_COUNT:  // $7X commands - Auto sequence track jump count setting
            s_jumpTrack = (latched & 0xFFFF0) >> 4;
            DEBUG_PRINT("jump: %d\n", s_jumpTrack);
            break;

        case commands::MODE_SPEC:  // $8X commands - MODE specification
            modeSpec(latched);
            break;
        case commands::FUNC_SPEC:  // $9X commands - Function specification
            funcSpec(latched);
            break;

        case 0xA:  // $AX commands - Audio CTRL
            audioControl(latched);
            break;

        case commands::MONITOR_COUNT:  // $BX commands - This command sets the traverse monitor count.
            g_countTrack = (latched & 0xFFFF0) >> 4;
            DEBUG_PRINT("count: %d\n", g_countTrack);
            break;
        case commands::SPINDLE:  // $EX commands - Spindle motor control
            spindleControl(latched);
            break;

            /*
            case commands::FOCUS_CONTROL: // $0X commands - Focus control
            case 0x1:
            case 0x3:
            case 0x5: // Blind/brake
            case 0x6: // Kick
                break;

            case commands::CUSTOM: // picostation
                switch ((latched & 0x0F0000) >> 16)
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
}

bool __time_critical_func(picostation::mechcommand::getSens)(const uint what) { return s_sensData[what]; }

void __time_critical_func(picostation::mechcommand::setSens)(const uint what, const bool new_value) {
    s_sensData[what] = new_value;
    if (what == s_currentSens) {
        gpio_put(Pin::SENS, new_value);
    }
}

void __time_critical_func(picostation::mechcommand::updateMechSens)() {
    while (!pio_sm_is_rx_fifo_empty(PIOInstance::MECHACON, SM::MECHACON)) {
        uint c = pio_sm_get_blocking(PIOInstance::MECHACON, SM::MECHACON) >> 24;
        s_latched = s_latched >> 8;
        s_latched = s_latched | (c << 16);
        s_currentSens = c >> 4;
        gpio_put(Pin::SENS, s_sensData[c >> 4]);
    }
}