#include "ecm/Ecm.h"
#include "Contours.h"
#include "Heightfield.h"
#include "MedialAxis.h"
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace ecm
{
namespace
{
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Plane coordinates: (u, v) horizontal, h up.
Vec3 toPlane(const Vec3 &p, UpAxis up)
{
    return up == UpAxis::Z ? p : Vec3{p.x, p.z, p.y};
}
Vec3 fromPlane(double u, double v, double h, UpAxis up)
{
    return up == UpAxis::Z ? Vec3{u, v, h} : Vec3{u, h, v};
}

void validate(const BuildSettings &s)
{
    const auto positive = [](double value) { return std::isfinite(value) && value > 0; };
    if (!positive(s.agentRadius) || !positive(s.agentHeight) || !positive(s.cellSize) || !positive(s.metersPerUnit))
        throw std::invalid_argument("Agent radius, height, cell size and meters per unit must be positive.");
    if (!std::isfinite(s.maxStepHeight) || s.maxStepHeight < 0)
        throw std::invalid_argument("Maximum step height must be zero or positive.");
    if (!(s.maxSlopeDegrees > 0 && s.maxSlopeDegrees < 90))
        throw std::invalid_argument("Maximum slope must be between 0 and 90 degrees.");
    if (s.maxCells < 16)
        throw std::invalid_argument("The cell budget is too small.");
}
} // namespace

Map build(const TriangleMesh &mesh, const BuildSettings &settings)
{
    validate(settings);
    Map map;
    map.settings = settings;
    auto &stats = map.stats;
    stats.triangles = mesh.indices.size() / 3;
    for (const uint32_t index : mesh.indices)
        if (index >= mesh.vertices.size())
            throw std::invalid_argument("Triangle index out of range.");

    std::vector<Vec3> plane;
    plane.reserve(mesh.vertices.size());
    for (const auto &p : mesh.vertices)
        plane.push_back(toPlane(p, settings.up));
    const double units = 1 / settings.metersPerUnit; // stage units per meter
    detail::FloorSettings floorSettings;
    floorSettings.cellSize = settings.cellSize * units;
    floorSettings.agentHeight = settings.agentHeight * units;
    floorSettings.maxStep = settings.maxStepHeight * units;
    floorSettings.cosMaxSlope = std::cos(settings.maxSlopeDegrees * 3.14159265358979323846 / 180);
    floorSettings.maxCells = settings.maxCells;
    const auto grid = detail::buildFloor(plane, mesh.indices, floorSettings, stats);
    stats.cellSize = grid.cellSize * settings.metersPerUnit;

    auto start = Clock::now();
    std::vector<uint8_t> mask(grid.floor.size());
    for (std::size_t i = 0; i < mask.size(); ++i)
        mask[i] = !std::isnan(grid.floor[i]);
    // One cell of tolerance removes the staircase of the voxelization; the error stays within
    // the voxel size that already bounds the boundary's accuracy.
    const auto boundary = detail::extractBoundary(mask, grid.width, grid.height, 1.0);
    stats.walkableCells = boundary.freeCells;
    stats.boundaryLoops = boundary.loops.size();
    for (const auto &loop : boundary.loops)
        stats.boundarySegments += loop.size();
    stats.contourMs = msSince(start);

    start = Clock::now();
    // Sample curved edges about every 25 cm, and at least every other cell.
    const double spacing = std::max(1.0, 0.25 / stats.cellSize);
    const auto axis = detail::computeMedialAxis(boundary.loops, spacing);
    stats.voronoiEdges = axis.voronoiEdges;
    stats.voronoiMs = msSince(start);

    start = Clock::now();
    // Grid corner (x, y) to world, lifted onto the floor.
    const auto world = [&](double x, double y, double fallback) {
        double h = grid.heightAt(x, y);
        if (std::isnan(h))
            h = fallback;
        return fromPlane(grid.originU + x * grid.cellSize, grid.originV + y * grid.cellSize, h, settings.up);
    };
    const double meters = grid.cellSize * settings.metersPerUnit; // meters per cell
    for (const auto &v : axis.vertices)
        map.vertices.push_back({world(v.x, v.y, stats.floorHeight), v.clearance * meters, {}});
    for (const auto &e : axis.edges)
    {
        Edge edge;
        edge.from = e.from;
        edge.to = e.to;
        edge.minClearance = INFINITY;
        double previousHeight = stats.floorHeight;
        for (std::size_t i = 0; i < e.samples.size(); ++i)
        {
            const auto &s = e.samples[i];
            Sample out;
            out.position = world(s.x, s.y, previousHeight);
            previousHeight = toPlane(out.position, settings.up).z;
            out.left = world(s.leftX, s.leftY, previousHeight);
            out.right = world(s.rightX, s.rightY, previousHeight);
            out.clearance = s.clearance * meters;
            out.event = s.event;
            edge.minClearance = std::min(edge.minClearance, out.clearance);
            if (i)
                edge.length += std::hypot(s.x - e.samples[i - 1].x, s.y - e.samples[i - 1].y) * meters;
            edge.samples.push_back(out);
        }
        const uint32_t id = uint32_t(map.edges.size());
        map.vertices[edge.from].edges.push_back(id);
        if (edge.to != edge.from)
            map.vertices[edge.to].edges.push_back(id);
        map.edges.push_back(std::move(edge));
    }
    for (const auto &loop : boundary.loops)
    {
        std::vector<Vec3> points;
        for (const auto &p : loop)
            points.push_back(world(p.x, p.y, stats.floorHeight));
        map.boundary.push_back(std::move(points));
    }
    stats.graphMs = msSince(start);
    return map;
}
} // namespace ecm
