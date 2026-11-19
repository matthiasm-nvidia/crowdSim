#pragma once
#include <cstdint>
#include <vector>

namespace ecm::detail
{
// Grid-corner coordinates: corner (x, y) is the lower-left corner of cell (x, y).
struct IPoint
{
    int32_t x = 0, y = 0;
    bool operator==(const IPoint &o) const
    {
        return x == o.x && y == o.y;
    }
};
// Closed polygon without a repeated end point; free space lies to the left of every edge,
// so outer boundaries are counter-clockwise and holes (obstacles) clockwise.
using Loop = std::vector<IPoint>;

struct BoundaryResult
{
    std::vector<Loop> loops;
    std::size_t freeCells = 0; // after cleanup
};

// `mask` (width * height, nonzero = free) is cleaned in place: diagonal-only contacts are
// removed so no two boundary loops touch, and only the largest 4-connected component is kept.
// Loops are simplified with Douglas-Peucker (tolerance in cells) without introducing any
// intersection between boundary segments.
BoundaryResult extractBoundary(std::vector<uint8_t> &mask, int width, int height, double tolerance);
} // namespace ecm::detail
