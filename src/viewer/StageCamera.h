#pragma once
#include "Camera.h"
#include <ecm_ovstage/StageData.h>
#include <optional>

namespace viewer
{
namespace stage = ecm_ovstage::stage;

inline constexpr const char *cameraAttributeNames[] = {"projection",
                                                       "focalLength",
                                                       "horizontalAperture",
                                                       "verticalAperture",
                                                       "horizontalApertureOffset",
                                                       "verticalApertureOffset",
                                                       "clippingRange"};

struct StageCamera
{
    std::string path;
    Camera camera;
};
// Called before the viewer adds its own camera. First usable camera by USD path wins.
std::optional<StageCamera> readStageCamera(ovstage_instance_t *stage, ovstage_ordinal_t ordinal);
std::optional<StageCamera> readStageCamera(const stage::Snapshot &snapshot);
} // namespace viewer
