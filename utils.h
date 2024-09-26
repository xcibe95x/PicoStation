#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <math.h>

inline int tobcd(int in)
{
    if (in > 99)
    {
        return 0x99;
    }
    else
    {
        return (in / 10) << 4 | (in % 10);
    }
}

inline int track_to_sector(int track)
{
    return pow(track, 2) * 0.00031499 + track * 9.357516535;
}

inline int sectors_per_track(int track)
{
    return round(track * 0.000616397 + 9);
}
#endif