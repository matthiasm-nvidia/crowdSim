#pragma once
#include "Camera.h"
#include <cstdint>
#include <ecm_ovstage/StageMesh.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace viewer
{
class RtxView
{
  public:
    enum class LoadStage
    {
        Initialize,
        ReadUsd,
        ComputeHierarchy,
        ReadScene,
        SetupViewer,
        PrepareRenderer,
        FirstFrame,
        Complete
    };
    // Phase boundaries, not a byte count or an estimate of time remaining.
    using LoadProgress = std::function<void(LoadStage)>;
    // Progress of the current RTX operation; a negative fraction is indeterminate.
    using RenderProgress = std::function<void(double, const std::string &)>;
    RtxView();
    ~RtxView();
    RtxView(const RtxView &) = delete;
    RtxView &operator=(const RtxView &) = delete;
    void initialize();
    // Failed loads retain the previous stage whenever the SDK permits recovery.
    void load(const std::string &path, int width, int height, const LoadProgress &progress = {});
    void render(const Camera &camera, int width, int height, const RenderProgress &progress = {});
    bool loaded() const;
    const std::vector<uint8_t> &pixels() const;
    int width() const;
    int height() const;
    Vec3 center() const;
    float radius() const;
    bool zUp() const;
    const Camera *initialCamera() const;
    const std::string &path() const;
    // World-space triangles of the loaded stage, excluding the viewer's own prims.
    ecm_ovstage::StageTriangles extractTriangles() const;
    // Root prim path that overlay layers should define their prims under.
    std::string overlayRoot() const;
    // Replaces the overlay layer (USDA text referenced onto the stage); empty removes it.
    void setOverlay(const std::string &layer);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace viewer
