#pragma once
#include <ecm/Ecm.h>
#include <string>

namespace viewer
{
struct EcmDisplay
{
    bool medialAxis = true;
    bool clearanceColors = true; // otherwise one color
    bool narrow = false;         // also draw parts with clearance below the agent radius
    bool spokes = false;         // closest obstacle points at every Voronoi vertex
    bool boundary = true;        // free-space boundary
    float lineWidth = 0.1f;      // meters
};

// USDA layer drawing the map as linear BasisCurves under `root` (an absolute prim path whose
// parent the stage already defines), lifted slightly above the floor.
std::string ecmOverlayLayer(const ecm::Map &map, const EcmDisplay &display, const std::string &root);
} // namespace viewer
