#pragma once

#include <math.h>
#include <stdint.h>

#include <numbers>

// For calculating sector at a position in the spiral track/groove
static constexpr double c_tracksPerSecond = 9.341456;
inline int trackToSector(const int track) {
    return pow(track, 2) * (std::numbers::pi / 10000) + track * c_tracksPerSecond;
}

// For calculating the number of sectors per track
static constexpr double c_sectorsPerTrack = 0.00067306685;
inline int sectorsPerTrack(const int track) { return round(track * c_sectorsPerTrack + 9); }
