#include "drive_mechanics.h"

#include <algorithm>
#include <math.h>

#include "i2s.h"
#include "cmd.h"
#include "values.h"

extern picostation::I2S m_i2s;
uint32_t zone[] = 			{13500, 27000, 45000, 63000, 85500, 103500, 130500, 153000, 175500, 207000, 234000, 265500, 297000, 999999};
uint32_t sect_per_track[] = { 	10,	   11,    12,    13,    14,     15,     16,     17,     18,     19,     20,     21,     22,     23};

picostation::DriveMechanics picostation::g_driveMechanics;

void picostation::DriveMechanics::moveToNextSector()
{
	if(m_sector < c_sectorMax){
		m_sector++;
	} 
}

void picostation::DriveMechanics::setSector(uint32_t step, bool rev)
{
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
                break;
            }
            
            step -= do_steps;
            if(step > 0)
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
                break;
            }
            
			step -= do_steps;
           
			if((cur_zone > 0) && (step > 0))
			{
                cur_zone--;
            }
        }
	}
}

bool picostation::DriveMechanics::servo_valid()
{
	return m_sector < c_sectorMax;

}

void picostation::DriveMechanics::moveSled(picostation::MechCommand &mechCommand){
    if ((time_us_64() - m_sledTimer) > c_MaxTrackMoveTime)
    {
		cur_track_counter++;
		//if (!(cur_track_counter & (m_trk_cnt - 1)))
		{
			mechCommand.setSens(SENS::COUT, !mechCommand.getSens(SENS::COUT));
		}
		
		m_sledTimer = time_us_64();
    }
}

void picostation::DriveMechanics::startSled()
{
	sled_work = 1;
	cur_track_counter = 0;
	m_sledTimer = time_us_64();
}

void picostation::DriveMechanics::setCountTrack(uint16_t track_cnt)
{
	m_trk_cnt = track_cnt;
}

