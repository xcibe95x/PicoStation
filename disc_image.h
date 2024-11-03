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
        DiscImage() {
            //CueParser_construct(&m_cueParser, &m_cueDisc);
        };
        ~DiscImage() {};

        FRESULT load(const TCHAR *targetCue, const TCHAR *targetBin); // Abstract file access, remove bin once cue properly parsed
        FRESULT loadv2(const TCHAR *targetCue);

        // getData
        void generateSubQ(SubQ *subqdata, int sector);
        int numLogicalTracks() { return m_cueDisc.trackCount; }
        int logicalTrackToSector(int logicalTrack) { return m_cueDisc.tracks[logicalTrack].fileOffset; };
        bool hasData() { return m_hasData; };
        bool isCurrentTrackData() { return m_cueDisc.tracks[m_currentLogicalTrack].trackType == CueTrackType::TRACK_TYPE_DATA; };
        void readData(void *buffer, int sector, int count);

    private:
        CueDisc m_cueDisc;
        //CueFile m_cueFile;
        //CueParser m_cueParser;
        CueScheduler m_cueScheduler;

        bool m_hasData = false;
        int m_currentLogicalTrack = 0;

        //FIL m_fil = {0};
    };
} // namespace picostation