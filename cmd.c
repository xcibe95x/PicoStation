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

extern volatile uint latched;
extern volatile uint count_track;
extern volatile uint sled_move_direction;
extern volatile uint track;
extern volatile uint original_track;
extern volatile uint sector;
extern volatile uint sector_for_track_update;
extern volatile int mode;
extern volatile bool SENS_data[16];

extern volatile bool soct;
extern volatile uint soct_offset;

volatile uint jump_track = 0;

void set_sens(uint what, bool new_value);

void autosequence()
{
    int subcommand = (latched & 0x0F0000) >> 16;
    uint timer_range = (latched & 0x8) >> 3;
    uint cancel_timer = (latched & 0xF) >> 4;

    set_sens(SENS_XBUSY, (subcommand != 0));

    switch (subcommand)
    {
    case 0x0: // Cancel
              /*switch (timer_range)
              {
              case 0:
                  // cancel_timer_value = 1000;
                  break;
      
              case 1:
                  // cancel_timer_value = 11000;
                  break;
              }*/
        DEBUG_PRINT("Cancel\n");
        // DEBUG_PRINT("Cancel timer_range: %d cancel_timer: %d\n", timer_range, cancel_timer);
        return;

    case 0x4: // Fine search - forward
        track = track + jump_track;
        DEBUG_PRINT("Fine search - forward %d\n", track);
        break;
    case 0x5: // Fine search - reverse
        track = track - jump_track;
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
        track = track + (2 * jump_track);
        DEBUG_PRINT("2N Track Jump - forward %d\n", track);
        break;
    case 0xD: // 2N Track Jump - reverse
        track = track - (2 * jump_track);
        DEBUG_PRINT("2N Track Jump - reverse %d\n", track);
        break;

    case 0xE: // M Track Move - forward
        track = track + jump_track;
        DEBUG_PRINT("M Track Move - forward %d\n", track);
        break;
    case 0xF: // M Track Move - reverse
        track = track - jump_track;
        DEBUG_PRINT("M Track Move - reverse %d\n", track);
        break;

    default:
        DEBUG_PRINT("Unsupported command: %x\n", subcommand);
        break;
    }

    sector = track_to_sector(track);
    sector_for_track_update = sector;
}

void sled_move()
{
    int subcommand_move = (latched & 0x030000) >> 16;
    int subcommand_track = (latched & 0x0C0000) >> 16;
    switch (subcommand_move)
    {
    case 2:
        sled_move_direction = SLED_MOVE_FORWARD;
        original_track = track;
        break;

    case 3:
        sled_move_direction = SLED_MOVE_REVERSE;
        original_track = track;
        break;

    default:
        if (sled_move_direction != SLED_MOVE_STOP)
        {
            sector = track_to_sector(track);
            sector_for_track_update = sector;
        }
        sled_move_direction = SLED_MOVE_STOP;
        break;
    }

    switch (subcommand_track)
    {
    case 8:
        track++;
        sector = track_to_sector(track);
        sector_for_track_update = sector;
        break;
    case 0xC:
        track--;
        sector = track_to_sector(track);
        sector_for_track_update = sector;
        break;
    }
}

void spindle()
{
    int subcommand = (latched & 0x0F0000) >> 16;

    SENS_data[SENS_GFS] = (subcommand == 6);
}

void interrupt_xlat(uint gpio, uint32_t events)
{
    int command = (latched & 0xF00000) >> 20;

    switch (command)
    {
    case CMD_SLED: // $2X commands - Sled motor control
        sled_move();
        break;

    case CMD_AUTOSEQ: // $4X commands
        autosequence();
        break;

    case CMD_JUMP_TRACK: // $7X commands - Auto sequence track jump count setting
        jump_track = (latched & 0xFFFF0) >> 4;
        DEBUG_PRINT("jump: %d\n", jump_track);
        break;

    /*case CMD_SOCT: // $8X commands - MODE specification
        soct = 1;
        pio_sm_set_enabled(pio1, SUBQ_SM, false);
        soct_program_init(pio1, SOCT_SM, soct_offset, SQSO, SQCK);
        pio_sm_set_enabled(pio1, SOCT_SM, true);
        pio_sm_put_blocking(pio1, SOCT_SM, 0xFFFFFFF);
        break;*/
    case CMD_SPEED: // $9X commands - 1x/2x speed setting
        if ((latched & 0xF40000) == 0x940000 && mode == 1)
        {
            mode = 2;
        }
        else if ((latched & 0xF40000) == 0x900000 && mode == 2)
        {
            mode = 1;
        }
        break;

    case CMD_COUNT_TRACK: // $BX commands - This command sets the traverse monitor count.
        count_track = (latched & 0xFFFF0) >> 4;
        DEBUG_PRINT("count: %d\n", count_track);
        break;
    case CMD_SPINDLE: // $EX commands - Spindle motor control
        spindle();
        break;
    }
    /*
    if (command == CMD_SLED || command == CMD_AUTOSEQ ||
        command == CMD_JUMP_TRACK || command == CMD_SPEED ||
        command == CMD_COUNT_TRACK || command == CMD_SPINDLE
    ) {
        DEBUG_PRINT("%06X\n", latched);
    }
    */

    latched = 0;
}