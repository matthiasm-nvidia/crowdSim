#pragma once
#include "Contours.h"
#include <vector>

namespace ecm::detail
{
// All values in grid-corner units.
struct AxisSample
{
    double x = 0, y = 0;
    double clearance = 0;
    double leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    bool event = false;
};
struct AxisVertex
{
    double x = 0, y = 0, clearance = 0;
};
struct AxisEdge
{
    uint32_t from = 0, to = 0;
    std::vector<AxisSample> samples;
};
struct MedialAxis
{
    std::vector<AxisVertex> vertices;
    std::vector<AxisEdge> edges;
    std::size_t voronoiEdges = 0;
};

// Medial axis of the free space bounded by `loops` (free space to the left of each loop),
// from the segment Voronoi diagram. Degree-two chains are merged, so every output vertex is a
// junction or a dead end (or one arbitrary point on a junction-free cycle). Curved edges are
// sampled at most `spacing` apart.
MedialAxis computeMedialAxis(const std::vector<Loop> &loops, double spacing);
} // namespace ecm::detail
