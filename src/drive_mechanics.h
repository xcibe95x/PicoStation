#pragma once

#include <stdint.h>

#include "pseudo_atomics.h"
#include "utils.h"
#include "values.h"

namespace picostation {
class MechCommand;

class DriveMechanics {
  public:
    void moveToNextSector();
    int getSector() { return m_sector.Load(); }
    uint32_t getTrack() const { return m_track; }
    void moveSled(MechCommand &mechCommand);
    void moveTrack(int tracks) { setTrack(m_track + tracks); }
    void setCountTrack(uint32_t countTrack) { m_countTrack = countTrack; }
    void setSectorForTrackUpdate(int sectorForTrackUpdate) { m_sectorForTrackUpdate = sectorForTrackUpdate; }
    void setSledMoveDirection(int sledMoveDirection);
    void setTrack(uint32_t track) {
        m_track = clamp(track, c_trackMin, c_trackMax);
        m_sectorForTrackUpdate = trackToSector(m_track);
        m_sector = m_sectorForTrackUpdate;
    }

    bool isSledStopped() { return m_sledMoveDirection == SledMove::STOP; }

  private:
    uint32_t m_countTrack = 0;
    uint32_t m_originalTrack = 0;
    uint32_t m_track = 0;

    pseudoatomic<int> m_sector;

    int m_sectorForTrackUpdate = 0;
    int m_sectorsPerTrack = sectorsPerTrack(0);

    int m_sledMoveDirection = SledMove::STOP;
    uint64_t m_sledTimer = 0;

    static constexpr unsigned int c_MaxTrackMoveTime = 15;    // uS
    static constexpr unsigned int c_MaxSubqDelayTime = 3333;  // uS
};

extern DriveMechanics g_driveMechanics;
}  // namespace picostation