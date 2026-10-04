#pragma once
#include <stdint.h>

// One road segment record in roads.bin (20 bytes, little-endian, lat/lon in degrees * 1e6)
struct Seg { int32_t lat1, lon1, lat2, lon2; uint8_t limit, cls; uint16_t reserved; };

// Distance (m) from a point to a segment, and the segment's direction (degrees)
struct Hit { double d, dir; };
