#include "utils.h"

extern inline int trackToSector(int track);
extern inline int sectorsPerTrack(int track);

int clamp(const int value, const int min, const int max)
{
    if (value < 0)
    {
        return 0;
    }
    else if (value > max)
    {
        return max;
    }
    else
    {
        return value;
    }
}