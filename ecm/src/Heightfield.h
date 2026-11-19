#pragma once
#include "ecm/Ecm.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace ecm::detail
{
// Triangle vertices in plane coordinates: x = u, y = v (horizontal), z = h (up).
struct FloorSettings
{
    double cellSize = 0;    // stage units
    double agentHeight = 0; // stage units
    double maxStep = 0;     // stage units
    double cosMaxSlope = 0;
    std::size_t maxCells = 0;
};

// The selected walkable floor on a regular grid. Cell (x, y) covers
// [originU + x * cellSize, originU + (x + 1) * cellSize) and likewise in v.
struct FloorGrid
{
    int width = 0, height = 0;
    double originU = 0, originV = 0, cellSize = 0;
    std::vector<float> floor; // floor height per cell, NaN where the cell is not part of the floor
    bool walkable(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < width && y < height && !std::isnan(floor[std::size_t(y) * width + x]);
    }
    // Floor height near a point given in cell units, searching a few rings outward. NaN if none.
    double heightAt(double x, double y) const;
};

FloorGrid buildFloor(const std::vector<Vec3> &planeVertices, const std::vector<uint32_t> &indices,
                     const FloorSettings &settings, BuildStats &stats);
} // namespace ecm::detail
