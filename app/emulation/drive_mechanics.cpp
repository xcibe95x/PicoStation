// drive_mechanics.cpp - Simulates PlayStation drive mechanics for sector addressing and sled motion.
#include "drive_mechanics.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include "emulation/i2s.h"
#include "commands/mech_commands.h"
#include "commons/values.h"
#include "commons/logging.h"

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

#define ZONE_CNT 	14
#define ZONE_MAX 	ZONE_CNT-1

extern picostation::I2S m_i2s;
uint32_t zone[ZONE_CNT] = 			{13500, 27000, 45000, 63000, 85500, 103500, 130500, 153000, 175500, 207000, 234000, 265500, 297000, 999999};
uint32_t sect_per_track[ZONE_CNT] = { 	10,	   11,    12,    13,    14,     15,     16,     17,     18,     19,     20,     21,     22,     23};
//const uint32_t zone[ZONE_CNT] = 			{10168, 21616, 34326, 48318, 63570, 80106, 97900, 116980, 137316, 158940, 181818, 205986, 231406, 258118, 286080, 999999};
//const uint32_t sect_per_track[ZONE_CNT] =   {    9,    10,    11,    12,    13,    14,    15,     16,     17,     18,     19,     20,     21,     22,     23,     24};

//inline uint32_t zone[ZONE_CNT] = 			{7805, 24642, 43136, 63300, 85109, 108576, 133716, 160499, 188939, 219056, 250812, 284226, 319319, 333005, 999999};
//inline uint32_t sect_per_track[ZONE_CNT] = {   10,	  11,    12,    13,    14,     15,     16,     17,     18,     19,     20,     21,     22,     23,     24};

picostation::DriveMechanics picostation::g_driveMechanics;

void __time_critical_func(picostation::DriveMechanics::moveToNextSector)()
{
    // Advance the cached sector counter and update zone tracking when thresholds change.
	if(m_sector < c_sectorMax){
		m_sector++;
	}
	
	if (m_sector > zone[cur_zone] && cur_zone < ZONE_MAX)
	{
		cur_zone++;
	}
}

void __time_critical_func(picostation::DriveMechanics::setSector)(uint32_t step, bool rev)
{
    // Step the virtual sled by a number of logical tracks, mimicking zone-based seek limits.
	m_i2s.i2s_set_state(0);

	while(step != 0)
	{
        if(rev == 0)
        {
            uint32_t sbze = zone[cur_zone] - m_sector;
            uint32_t sbreq = step * sect_per_track[cur_zone];
            uint32_t do_steps = ((sbze > sbreq) ? sbreq : sbze) / sect_per_track[cur_zone];
            m_sector += do_steps * sect_per_track[cur_zone];
            
            if(m_sector > c_sectorMax)
            {
                m_sector = c_sectorMax;
                cur_zone = ZONE_MAX;
                break;
            }
            
            step -= do_steps;
            if(step > 0 && cur_zone < ZONE_MAX)
            {
                cur_zone++;
            }
        }
        else
        {
            uint32_t sect_before_ze;
            uint32_t do_steps;
            uint32_t req_sect_step = step * sect_per_track[cur_zone];

            if(cur_zone > 0)
            {
                sect_before_ze = m_sector - zone[cur_zone - 1];
            }
            else
            {
                sect_before_ze = req_sect_step;
            }

            do_steps = ((sect_before_ze > req_sect_step) ? req_sect_step : sect_before_ze) / sect_per_track[cur_zone];
            
            if(m_sector > (do_steps * sect_per_track[cur_zone]))
            {
                m_sector -= do_steps * sect_per_track[cur_zone];
            }
            else
            {
                m_sector = 0;
                cur_zone = 0;
                break;
            }
            
			step -= do_steps;
           
			if((cur_zone > 0) && (step > 0))
			{
                cur_zone--;
            }
        }
	}
	
	DEBUG_PRINT("cur sec %d\n", m_sector);
}

bool __time_critical_func(picostation::DriveMechanics::servo_valid)()
{
	return m_sector < c_sectorMax;

}

void __time_critical_func(picostation::DriveMechanics::moveSled)(picostation::MechCommand &mechCommand)
{
    // Toggle the simulated sled counter to generate pulses for the console during sled motion.
    if ((time_us_64() - m_sledTimer) > c_MaxTrackMoveTime)
    {
		cur_track_counter++;
		
		if (!(cur_track_counter & 255))
		{
			mechCommand.setSens(SENS::COUT, !mechCommand.getSens(SENS::COUT));
		}
		
		m_sledTimer = time_us_64();
    }
}

void __time_critical_func(picostation::DriveMechanics::startSled)()
{
    // Begin a sled move operation and reset related timers.
	sled_work = 1;
	cur_track_counter = 0;
	m_sledTimer = time_us_64();
}
