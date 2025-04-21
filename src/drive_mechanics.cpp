#include "drive_mechanics.h"

#include <algorithm>
#include <math.h>

#include "cmd.h"
#include "utils.h"
#include "values.h"

picostation::DriveMechanics picostation::g_driveMechanics;

void picostation::DriveMechanics::moveToNextSector() {
    const int nextSector = std::clamp(m_sector.Load() + 1, c_sectorMin, c_sectorMax);
    m_sector = nextSector;
    if ((nextSector - m_sectorForTrackUpdate) >= m_sectorsPerTrack) {
        m_sectorForTrackUpdate = nextSector;
        m_track = std::clamp(m_track + 1, c_trackMin, c_trackMax);
        m_sectorsPerTrack = sectorsPerTrack(m_track);
    }
}

void picostation::DriveMechanics::moveSled(picostation::MechCommand &mechCommand) {
    if ((time_us_64() - m_sledTimer) > c_MaxTrackMoveTime) {
        setTrack(m_track + m_sledMoveDirection);

        const int tracks_moved = m_track - m_originalTrack;
        if (abs(tracks_moved) >= m_countTrack) {
            m_originalTrack = m_track;
            mechCommand.setSens(SENS::COUT, !mechCommand.getSens(SENS::COUT));
        }

        m_sledTimer = time_us_64();
    }
}

void picostation::DriveMechanics::setSledMoveDirection(int sledMoveDirection) {
    if (sledMoveDirection == SledMove::STOP && m_sledMoveDirection != SledMove::STOP) {
        m_sectorForTrackUpdate = trackToSector(m_track);
        m_sector = m_sectorForTrackUpdate;
    }
    m_sledMoveDirection = sledMoveDirection;
    m_originalTrack = m_track;
    m_sledTimer = time_us_64();
}