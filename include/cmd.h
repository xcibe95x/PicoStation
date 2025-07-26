#pragma once

#include <stddef.h>
#include <stdint.h>

#include "drive_mechanics.h"
#include "pseudo_atomics.h"

namespace picostation {

class MechCommand {
  public:
    bool getSens(const size_t what) const;
    void setSens(const size_t what, const bool new_value);
    void setcoutsens() { m_currentSens = 12;}
    bool getSoct();
    void setSoct(const bool new_value);
    void processLatchedCommand();
    void updateMech();
	void updateSens();

    void updateAutoSeqTrack();

  private:
	enum MECH_COMMAND
	{
		MECH_CMD_FOCUS_CONTROL = 0x0,
		MECH_CMD_TRACKING_CONTROL = 0x1,
		MECH_CMD_TRACKING_MODE = 0x2,
		MECH_CMD_SELECT = 0x3,
		MECH_CMD_AUTO_SEQUENCE = 0x4,
		MECH_CMD_BLIND_BRAKE_OVERFLOW = 0x5,
		MECH_CMD_SLED_KICK = 0x6,
		MECH_CMD_ASEQ_TRACK_COUNT = 0x7,
		MECH_CMD_MODE_SPECIFICATION = 0x8,
		MECH_CMD_FUNCTION_SPECIFICATION = 0x9,
		MECH_CMD_AUDIO_CONTROL = 0xA,
		MECH_CMD_TRAVERS_MONITOR_COUNTER = 0xB,
		MECH_CMD_SPINDLE_SERVO_SETTING = 0xC,
		MECH_CMD_CLV_CTRL = 0xD,
		MECH_CMD_CLV_MODE = 0xE,
		MECH_CMD_CUSTOM = 0xF
	};

	enum TRACKING_MODE
	{
		SLED_OFF     = 0x0,
		SLED_ON      = 0x1,
		SLED_FORWARD = 0x2,
		SLED_REVERSE = 0x3,
	};

	enum ASEQ_CMD
	{
		ASEQ_CMD_CANCEL     = 0x0,
		ASEQ_CMD_FINE_SEARCH = 0x2,
		ASEQ_CMD_FOCUS_ON    = 0x3,
		ASEQ_CMD_1TRK_JUMP  = 0x4,
		ASEQ_CMD_10TRK_JUMP = 0x5,
		ASEQ_CMD_2NTRK_JUMP = 0x6,
		ASEQ_CMD_MTRK_JUMP  = 0x7,
	};

	enum CLV_MODE
	{
		CLV_MODE_STOP  = 0x0,
		CLV_MODE_KICK  = 0x8,
		CLV_MODE_BRAKE = 0xA,
		CLV_MODE_CLVS  = 0xE,
		CLV_MODE_CLVH  = 0xC,
		CLV_MODE_CLVP  = 0xF,
		CLV_MODE_CLVA  = 0x6,
	};
	
	enum CUSTOM_CMD
	{
		COMMAND_NONE = 0x0,
		COMMAND_GOTO_ROOT = 0x1,
		COMMAND_GOTO_PARENT = 0x2,
		COMMAND_GOTO_DIRECTORY = 0x3,
		COMMAND_GET_NEXT_CONTENTS = 0x4,
		COMMAND_MOUNT_FILE = 0x5,
		COMMAND_IO_COMMAND = 0x6,
		COMMAND_IO_DATA = 0x7,
		COMMAND_BOOTLOADER = 0xA
	};

	typedef union mech_cmd_t
	{
		struct
		{
			uint32_t :20;
			uint32_t id:4;
			uint32_t :8;
		} cmd;

		struct
		{
			uint32_t :16;
			uint32_t sled:2;
			uint32_t tracking:2;
			uint32_t :12;
		} tracking_mode;

		struct
		{
			uint32_t :11;
			uint32_t LSSL:1;
			uint32_t MT:4;
			uint32_t dir:1;
			uint32_t cmd:3;
			uint32_t :12;
		} aseq_cmd;

		struct
		{
			uint32_t :4;
			uint32_t count:16;
			uint32_t :12;
		} aseq_track_count;

		struct
		{
			uint32_t :13;
			uint32_t SOCT:1;
			uint32_t ASHS:1;
			uint32_t VCOSEL:1;
			uint32_t WSEL:1;
			uint32_t DOUT_MuteF:1;
			uint32_t DOUT_Mute:1;
			uint32_t CDROM:1;
			uint32_t :12;
		} mode_specification;

		struct
		{
			uint32_t :13;
			uint32_t FLFC:1;
			uint32_t BiliGL_SUB:1;
			uint32_t BiliGL_Main:1;
			uint32_t DPLL:1;
			uint32_t ASEQ:1;
			uint32_t DSPB:1;
			uint32_t DCLV:1;
			uint32_t :12;
		} function_specification;

		struct
		{
			uint32_t :16;
			uint32_t mode:4;
			uint32_t :12;
		} clv_mode;
		
		struct
		{
			uint32_t arg:16;
			uint32_t cmd:4;
			uint32_t :12;
		} custom_cmd;

		uint32_t raw;
	} mech_cmd;
	/*
    enum TopLevelCommands : uint32_t {
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

    enum SpindleCommands : uint32_t {
        STOP = 0b0000,   // 0
        KICK = 0b1000,   // 8
        BRAKE = 0b1010,  // A
        CLVS = 0b1110,   // E
        CLVH = 0b1100,   // C
        CLVP = 0b1111,   // F
        CLVA = 0b0110,   // 6
    };*/

    /*void audioControl(const uint32_t latched);
    void autoSequence(const uint32_t latched);
    void customCommand(const uint32_t latched);
    void funcSpec(const uint32_t latched);
    void modeSpec(const uint32_t latched);
    void trackingMode(const uint32_t latched);
    void spindleControl(const uint32_t latched);*/

    int m_jumpTrack = 0;
    uint32_t m_latched = 0;  // Command latch

    size_t m_currentSens = 0;
    bool m_sensData[16] =
    {
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
    pseudoatomic<bool> m_soctEnabled;
};
}  // namespace picostation
