#pragma once

#include <stdint.h>

#include "ff.h"
#include "third_party/cueparser/cueparser.h"
#include "third_party/cueparser/disc.h"
#include "third_party/cueparser/scheduler.h"
#include "third_party/posix_file.h"

#include "subq.h"

namespace picostation
{
    class DiscImage
    {
    public:
        DiscImage() {};
        ~DiscImage() {};

        FRESULT load(const TCHAR *targetCue);

        CueDisc *getCueDisc() { return &m_cueDisc; };
        SubQ::Data generateSubQ(const int sector);
        int numLogicalTracks() { return m_cueDisc.trackCount; }
        int logicalTrackToSector(const int logicalTrack) { return m_cueDisc.tracks[logicalTrack].fileOffset; };
        bool hasData() { return m_hasData; };
        bool isCurrentTrackData() { return m_cueDisc.tracks[m_currentLogicalTrack].trackType == CueTrackType::TRACK_TYPE_DATA; };
        void readData(void *buffer, const int sector);

    private:
        CueDisc m_cueDisc;
        bool m_hasData = false;
        int m_currentLogicalTrack = 0;
    };

    extern DiscImage g_discImage;
} // namespace picostation