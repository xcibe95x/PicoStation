// logging.h - Simple logging macros shared across the firmware.
#pragma once

#define DEBUG_CMD 0
#define DEBUG_CUE 0
#define DEBUG_I2S 0
#define DEBUG_FILEIO 0
#define DEBUG_MAIN 0
#define DEBUG_MODCHIP 0
#define DEBUG_SUBQ 0

#define DEBUG_LOGGING_ENABLED (DEBUG_CMD || DEBUG_CUE || DEBUG_I2S || DEBUG_FILEIO || DEBUG_MAIN || DEBUG_MODCHIP || DEBUG_SUBQ)
