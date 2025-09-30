// drive_mechanics.h - Models sled and spindle behaviour for virtual disc access.
#pragma once

#include <algorithm>
#include <stdint.h>

#include "pseudo_atomics.h"
#include "values.h"

namespace picostation {
class MechCommand;

class DriveMechanics {
  public:
    void moveToNextSector();
    void setSector(uint32_t step, bool rev);
    int getSector() { return m_sector; }
    void moveSled(MechCommand &mechCommand);
	bool servo_valid();
	void startSled();
	void stopSled() { sled_work = 0; }
	uint32_t get_track_count() { return cur_track_counter;}
	
    void resetDrive()
    {
		sled_work = 0;
		m_sector = 0;
		cur_track_counter = 0;
		cur_zone = 0;
	}

    bool isSledStopped() { return !sled_work; }
    
  private:
	uint32_t cur_track_counter = 0;
    uint64_t m_sledTimer = 0;
    uint32_t m_sector = 0;
    uint8_t cur_zone = 0;
	uint32_t c_MaxTrackMoveTime = 29;
    bool sled_work = false;
};

extern DriveMechanics g_driveMechanics;
}  // namespace picostation
