#include "cmd.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "emulation/i2s.h"
#include "emulation/drive_mechanics.h"
#include "hardware/pio.h"
#include "commons/logging.h"
#include "main.pio.h"
#include "pico/bootrom.h"
#include "picostation.h"
#include "commons/pseudo_atomics.h"
#include "commons/values.h"
#include "systems/directory_listing.h"

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

// External globals defined elsewhere in the project
extern picostation::I2S m_i2s;
extern pseudoatomic<uint32_t> g_fileArg;
extern pseudoatomic<picostation::FileListingStates> needFileCheckAction;
extern pseudoatomic<int> listReadyState;

// Local state variables for sled/tracking mechanics
static bool dir = 0;
static bool trk_dir = 0;
static bool prev_dir = 0;
static bool sled_break = 0;
static uint16_t m_jumpTrack = 0;

// -----------------------------------------------------------------------------
// CUSTOM COMMAND HANDLING SYSTEM
// -----------------------------------------------------------------------------

struct CustomCommandHandler {
    int id;
    const char *name;
    const char *description;
    void (*handler)(uint32_t arg);
};

// ---------------- Individual handler implementations ----------------

static void handleNone(uint32_t) {
    needFileCheckAction = picostation::FileListingStates::IDLE;
}

static void handleGotoRoot(uint32_t) {
    DEBUG_PRINT("GOTO_ROOT\n");
    needFileCheckAction = picostation::FileListingStates::GOTO_ROOT;
    listReadyState = 0;
}

static void handleGotoParent(uint32_t) {
    DEBUG_PRINT("GOTO_PARENT\n");
    needFileCheckAction = picostation::FileListingStates::GOTO_PARENT;
    listReadyState = 0;
}

static void handleGotoDirectory(uint32_t) {
    DEBUG_PRINT("GOTO_DIRECTORY\n");
    needFileCheckAction = picostation::FileListingStates::GOTO_DIRECTORY;
    listReadyState = 0;
}

static void handleGetNextContents(uint32_t) {
    DEBUG_PRINT("GET_NEXT_CONTENTS\n");
    needFileCheckAction = picostation::FileListingStates::GET_NEXT_CONTENTS;
    listReadyState = 0;
}

static void handleMountFile(uint32_t) {
    DEBUG_PRINT("MOUNT_FILE\n");
    needFileCheckAction = picostation::FileListingStates::MOUNT_FILE;
}

static void handleIoCommand(uint32_t arg) {
    DEBUG_PRINT("COMMAND_IO_COMMAND %x\n", arg);
}

static void handleIoData(uint32_t arg) {
    DEBUG_PRINT("COMMAND_IO_DATA %x\n", arg);
}

static void handleBootloader(uint32_t arg) {
    if (arg == 0xBEEF) {
        // Restart into bootloader
        rom_reset_usb_boot_extra(Pin::LED, 0, false);
    }
}

// ---------------- Handler lookup table ----------------
static const CustomCommandHandler kCustomHandlers[] = {
    { picostation::MechCommand::COMMAND_NONE,          "COMMAND_NONE",          "Clear pending menu work", handleNone },
    { picostation::MechCommand::COMMAND_GOTO_ROOT,     "COMMAND_GOTO_ROOT",     "Jump to the SD card root directory", handleGotoRoot },
    { picostation::MechCommand::COMMAND_GOTO_PARENT,   "COMMAND_GOTO_PARENT",   "Enter the parent directory", handleGotoParent },
    { picostation::MechCommand::COMMAND_GOTO_DIRECTORY,"COMMAND_GOTO_DIRECTORY","Enter the directory indexed by the provided argument", handleGotoDirectory },
    { picostation::MechCommand::COMMAND_GET_NEXT_CONTENTS,"COMMAND_GET_NEXT_CONTENTS","Request the next page of directory entries", handleGetNextContents },
    { picostation::MechCommand::COMMAND_MOUNT_FILE,    "COMMAND_MOUNT_FILE",    "Mount the file indexed by the provided argument", handleMountFile },
    { picostation::MechCommand::COMMAND_IO_COMMAND,    "COMMAND_IO_COMMAND",    "Begin a metadata IO transaction (e.g. game ID transfer)", handleIoCommand },
    { picostation::MechCommand::COMMAND_IO_DATA,       "COMMAND_IO_DATA",       "Send a 16-bit payload for the active metadata IO transaction", handleIoData },
    { picostation::MechCommand::COMMAND_BOOTLOADER,    "COMMAND_BOOTLOADER",    "Reboot the RP2040 into the USB bootloader when armed", handleBootloader },
};

// -----------------------------------------------------------------------------
// MAIN COMMAND PROCESSOR
// -----------------------------------------------------------------------------

void __time_critical_func(picostation::MechCommand::processLatchedCommand)()
{
    static picostation::MechCommand::mech_cmd command;
    command.raw = m_latched;
    m_latched = 0;
    
	switch (command.cmd.id)
    {
        // Tracking-related commands
		case  picostation::MechCommand::MECH_CMD_TRACKING_MODE:
		{
			if(!g_driveMechanics.isSledStopped())
			{
				DEBUG_PRINT("%c\n", (dir == 0) ? '+' : '-');

				if((sled_break == 0) || (prev_dir == dir))
				{
					g_driveMechanics.setSector(g_driveMechanics.get_track_count(), dir);
				}
				
				g_driveMechanics.stopSled();
				sled_break = 1;
				prev_dir = dir;
			}

			switch(command.tracking_mode.sled)
			{		
				case picostation::MechCommand::SLED_FORWARD:
				{
					DEBUG_PRINT("SLED FORWARD\n");
					dir = 0;
					m_i2s.i2s_set_state(0);
					g_driveMechanics.startSled();
					break;
				}

				case picostation::MechCommand::SLED_REVERSE:
				{
					DEBUG_PRINT("SLED REVERSE\n");
					dir = 1;
					m_i2s.i2s_set_state(0);
					g_driveMechanics.startSled();
					break;
				}
			}
			break;
		}
		
        // Auto-sequence commands
		case picostation::MechCommand::MECH_CMD_AUTO_SEQUENCE:
		{
	        switch(command.aseq_cmd.cmd)
	        {
	            case picostation::MechCommand::ASEQ_CMD_CANCEL:
					setSens(SENS::XBUSY, false);
	            	m_i2s.i2s_set_state(1);
					return;
				
	            case picostation::MechCommand::ASEQ_CMD_FOCUS_ON:
					DEBUG_PRINT("ASEQ FOCUS ON\n");
					setSens(SENS::FOK, true);
					m_i2s.i2s_set_state(0);
					setSens(SENS::XBUSY, true);
					add_alarm_in_ms(15, [](alarm_id_t id, void *user_data) -> int64_t 
					{
						picostation::MechCommand *mechCommand = static_cast<picostation::MechCommand *>(user_data);
						mechCommand->resetXBUSY();
						return 0;
					}, this, true);
					break;

				case picostation::MechCommand::ASEQ_CMD_1TRK_JUMP:
					g_driveMechanics.setSector(1, command.aseq_cmd.dir);
					break;

	            case picostation::MechCommand::ASEQ_CMD_2NTRK_JUMP:
	            	g_driveMechanics.setSector(m_jumpTrack << 1, command.aseq_cmd.dir);
					break;
			}
			break;
		}
		
        // Track count command
		case picostation::MechCommand::MECH_CMD_ASEQ_TRACK_COUNT:
		{
			m_jumpTrack = command.aseq_track_count.count;
			break;
		}
		
        // Mode specification
		case picostation::MechCommand::MECH_CMD_MODE_SPECIFICATION:
		{
			setSoct(command.mode_specification.SOCT);
			
			if (command.mode_specification.SOCT)
			{
				pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
				soct_program_init(PIOInstance::SOCT, SM::SOCT, g_soctOffset, Pin::SQSO, Pin::SQCK);
				pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, true);
				pio_sm_put_blocking(PIOInstance::SOCT, SM::SOCT, 0xFFFFFFF);
			}
			break;
		}
		
        // Function specification
		case picostation::MechCommand::MECH_CMD_FUNCTION_SPECIFICATION:
		{
			g_targetPlaybackSpeed = command.function_specification.DSPB + 1;
			break;
		}
		
        // CLV control
		case picostation::MechCommand::MECH_CMD_CLV_MODE:
		{
			sled_break = 0;
			switch(command.clv_mode.mode)
			{
				case picostation::MechCommand::CLV_MODE_STOP:
					setSens(SENS::GFS, false);
					m_i2s.i2s_set_state(0);
					DEBUG_PRINT("T\n");
					break;
				
				case picostation::MechCommand::CLV_MODE_BRAKE:
					DEBUG_PRINT("B\n");
					break;
					
				case picostation::MechCommand::CLV_MODE_KICK:
					DEBUG_PRINT("K\n");
					break;

				case picostation::MechCommand::CLV_MODE_CLVS:
				case picostation::MechCommand::CLV_MODE_CLVH:
				case picostation::MechCommand::CLV_MODE_CLVP:
				case picostation::MechCommand::CLV_MODE_CLVA:
					setSens(SENS::GFS, true);
					m_i2s.i2s_set_state(1);
					setSens(SENS::XBUSY, false);
					break;
			}
			break;
		}
		
        // Custom command dispatch
		case picostation::MechCommand::MECH_CMD_CUSTOM:
		{
			g_fileArg = command.custom_cmd.arg;

			bool handled = false;
			for (auto &entry : kCustomHandlers) {
				if (entry.id == command.custom_cmd.cmd) {
					DEBUG_PRINT("%s: %s (arg=%u)\n", entry.name, entry.description, command.custom_cmd.arg);
					entry.handler(command.custom_cmd.arg);
					handled = true;
					break;
				}
			}

			if (!handled) {
				DEBUG_PRINT("Unknown custom command: %d\n", command.custom_cmd.cmd);
			}
			break;
		}
		
		default:
			break;
	}
}

// -----------------------------------------------------------------------------
// SENSOR + STATE UTILITIES
// -----------------------------------------------------------------------------

bool __time_critical_func(picostation::MechCommand::getSens)(const size_t what) const { return m_sensData[what]; }

void __time_critical_func(picostation::MechCommand::setSens)(const size_t what, const bool new_value)
{
    m_sensData[what] = new_value;
    if (what == m_currentSens)
    {
        gpio_put(Pin::SENS, new_value);
    }
}

bool picostation::MechCommand::getSoct() { return m_soctEnabled.Load(); }
void picostation::MechCommand::setSoct(const bool new_value) { m_soctEnabled = new_value; }

void __time_critical_func(picostation::MechCommand::resetXBUSY)()
{
	m_i2s.i2s_set_state(1);
	setSens(SENS::XBUSY, false);
}

void __time_critical_func(picostation::MechCommand::updateMech)() 
{
    while (pio_sm_get_rx_fifo_level(PIOInstance::MECHACON, SM::MECHACON))
    {
        const uint32_t c = pio_sm_get_blocking(PIOInstance::MECHACON, SM::MECHACON) >> 24;
        m_latched = m_latched >> 8;
        m_latched = m_latched | (c << 16);
        m_currentSens = c >> 4;
    }
    gpio_put(Pin::SENS, m_sensData[m_currentSens]);
}
