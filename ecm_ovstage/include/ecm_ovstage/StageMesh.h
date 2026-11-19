#pragma once
#include <ecm/Ecm.h>
#include <ovstage/ovstage.h>
#include <string>

namespace ecm_ovstage
{
struct StageTriangles
{
    ecm::TriangleMesh mesh; // world space, stage units
    bool zUp = false;
    double metersPerUnit = 0.01; // USD's fallback when the stage does not author it
    std::size_t meshes = 0, primitives = 0, instances = 0, hidden = 0;
};

// World-space triangles of every visible, non-guide gprim: meshes (fan-triangulated) and the
// implicit Cube/Sphere/Cylinder/Cone/Capsule shapes, including native instances. Reads world
// matrices, so the hierarchy must be computed at `ordinal`. Prims under `skipRoot` (the host's
// own camera and overlays) are ignored.
StageTriangles extractStageTriangles(ovstage_instance_t *stage, ovstage_ordinal_t ordinal, const std::string &skipRoot);
} // namespace ecm_ovstage
