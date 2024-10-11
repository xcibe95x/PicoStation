#pragma once

#include "ff.h"

namespace picostation
{
    class DiscImage
    {
    public:
        DiscImage() {};
        ~DiscImage() {};

        FRESULT load(FIL *fil, const TCHAR *targetCue, const TCHAR *targetBin); // Abstract file access, remove bin once cue properly parsed

        // getData
        int numLogicalTracks() { return m_numLogicalTracks; }
        int logicalTrackToSector(int logicalTrack) { return m_logicalTrackToSector[logicalTrack]; };
        bool hasData() { return m_hasData; };
        bool isDataTrack(int logicalTrack) { return m_isDataTrack[logicalTrack]; };

    private:
        static constexpr int c_maxTracks = 100;

        bool m_hasData = false;
        bool m_isDataTrack[c_maxTracks];
        int m_logicalTrackToSector[c_maxTracks];
        int m_numLogicalTracks = 0;

        //FIL *m_fil;
    };
} // namespace picostation