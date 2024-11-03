#pragma once

#include <math.h>

// For calculating sector at a position in the spiral track/groove
inline int trackToSector(int track)
{
    return pow(track, 2) * 0.00031499 + track * 9.357516535;
}

inline int sectorsPerTrack(int track)
{
    return round(track * 0.000616397 + 9);
}
