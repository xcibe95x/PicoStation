#pragma once

#include "ff.h"

namespace picostation
{
    namespace cueparser
    {
        FRESULT loadImage(FIL *fil, const TCHAR *targetCue, const TCHAR *targetBin);
    }

    class DiscImage
    {
    public:
        DiscImage();
        ~DiscImage();

        void load(const TCHAR *targetCue, const TCHAR *targetBin); // Remove bin once cue properly parsing

        // getData
        int getNumLogicalTracks() const;
        int getLogicalTrackToSector(int logicalTrack) const;
        bool isDataTrack(int logicalTrack) const;
    };
}