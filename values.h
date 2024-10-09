#ifndef VALUES_H_INCLUDED
#define VALUES_H_INCLUDED

// GPIO pinouts
#define XLAT 0
#define SQCK 1
#define LMTSW 2
#define SCEX_DATA 4
#define DOOR 6
#define RESET 7
#define SENS 14
#define DA15 15 // next pin is DA16
#define LRCK 17
#define SCOR 18
#define SQSO 19
#define CLK 21
#define CMD_DATA 26
#define CMD_CK 27

// C2PO, WFCK is always GND

// PIO0
#define I2S_DATA_SM 0

// PIO1
#define SCOR_SM 0
#define MECHACON_SM 1
#define SOCT_SM 2
#define SUBQ_SM 3

// Commands
#define CMD_SLED 0x2
#define CMD_AUTOSEQ 0x4
#define CMD_JUMP_TRACK 0x7
#define CMD_SOCT 0x8
#define CMD_SPEED 0x9
#define CMD_COUNT_TRACK 0xB
#define CMD_SPINDLE 0xE
#define CMD_CUSTOM 0xF

// SENS
#define SENS_FZC 0x0
#define SENS_AS 0x1
#define SENS_TZC 0x2
// #define SENS_SELECT 0x3 // Several registers, need more research
#define SENS_XBUSY 0x4
#define SENS_FOK 0x5
#define SENS_GFS 0xA
#define SENS_COMP 0xB
#define SENS_COUT 0xC
#define SENS_OV64 0xE

// SLED
#define SLED_MOVE_STOP 0
#define SLED_MOVE_REVERSE 0x11
#define SLED_MOVE_FORWARD 0x22

//
#define CD_SAMPLES 588
#define CD_SAMPLES_BYTES CD_SAMPLES * 2 * 2

//
#define TRACK_MOVE_TIME_US 15

//
#define PSNEE_SECTOR_LIMIT 4500
#define SECTOR_CACHE 50

#define NUM_IMAGES 4

#endif
