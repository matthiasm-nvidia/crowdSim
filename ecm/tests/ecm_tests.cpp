// CPU tests for the explicit corridor map on small synthetic scenes.
#include "ecm/Ecm.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <string>

namespace
{
int failures = 0;
void expect(bool condition, const std::string &what)
{
    if (!condition)
    {
        std::printf("  FAILED: %s\n", what.c_str());
        ++failures;
    }
}

// Scenes are authored Z-up in meters; `transform` maps them to other conventions.
struct Scene
{
    ecm::TriangleMesh mesh;
    void quad(ecm::Vec3 a, ecm::Vec3 b, ecm::Vec3 c, ecm::Vec3 d)
    {
        const uint32_t base = uint32_t(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), {a, b, c, d});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    void floor(double x0, double y0, double x1, double y1, double z = 0)
    {
        quad({x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z});
    }
    void box(double x0, double y0, double z0, double x1, double y1, double z1)
    {
        floor(x0, y0, x1, y1, z0);
        floor(x0, y0, x1, y1, z1);
        quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1});
        quad({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});
        quad({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1});
        quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1});
    }
};

double distance(const ecm::Vec3 &a, const ecm::Vec3 &b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Independent cycles in the graph: one per enclosed obstacle.
std::size_t cycles(const ecm::Map &map)
{
    std::vector<uint32_t> parent(map.vertices.size());
    std::iota(parent.begin(), parent.end(), 0u);
    std::function<uint32_t(uint32_t)> find = [&](uint32_t v) { return parent[v] == v ? v : parent[v] = find(parent[v]); };
    std::size_t components = map.vertices.size();
    for (const auto &e : map.edges)
    {
        const uint32_t a = find(e.from), b = find(e.to);
        if (a != b)
        {
            parent[a] = b;
            --components;
        }
    }
    return map.edges.size() + components - map.vertices.size();
}

double maxClearance(const ecm::Map &map)
{
    double best = 0;
    for (const auto &v : map.vertices)
        best = std::max(best, v.clearance);
    for (const auto &e : map.edges)
        for (const auto &s : e.samples)
            best = std::max(best, s.clearance);
    return best;
}

// Structural invariants every map must satisfy.
void checkInvariants(const ecm::Map &map, double metersPerUnit)
{
    expect(!map.edges.empty(), "map has edges");
    const double tolerance = 2 * map.stats.cellSize;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < map.edges.size(); ++i)
    {
        const auto &e = map.edges[i];
        if (e.samples.size() < 2 || e.from >= map.vertices.size() || e.to >= map.vertices.size())
        {
            ++bad;
            continue;
        }
        if (distance(e.samples.front().position, map.vertices[e.from].position) * metersPerUnit > 1e-6 ||
            distance(e.samples.back().position, map.vertices[e.to].position) * metersPerUnit > 1e-6)
            ++bad;
        for (const auto &s : e.samples)
        {
            const double left = distance(s.position, s.left) * metersPerUnit;
            const double right = distance(s.position, s.right) * metersPerUnit;
            if (s.clearance < 0 || std::abs(left - s.clearance) > tolerance || std::abs(right - s.clearance) > tolerance)
                ++bad;
        }
    }
    expect(bad == 0, "edge samples are consistent (" + std::to_string(bad) + " bad)");
    for (uint32_t v = 0; v < map.vertices.size(); ++v)
    {
        for (const uint32_t e : map.vertices[v].edges)
            expect(map.edges[e].from == v || map.edges[e].to == v, "vertex edge lists reference the vertex");
    }
}

ecm::Map run(const char *name, const Scene &scene, ecm::BuildSettings settings = {})
{
    std::printf("%s\n", name);
    const auto map = ecm::build(scene.mesh, settings);
    std::printf("  %zu vertices, %zu edges, %zu loops, %zu cells, max clearance %.3f m, %d cycles\n",
                map.vertices.size(), map.edges.size(), map.stats.boundaryLoops, map.stats.walkableCells,
                maxClearance(map), int(cycles(map)));
    checkInvariants(map, settings.metersPerUnit);
    return map;
}

void openSquare()
{
    Scene scene;
    scene.floor(0, 0, 10, 10);
    ecm::BuildSettings settings;
    settings.up = ecm::UpAxis::Z;
    const auto map = run("open square", scene, settings);
    expect(std::abs(maxClearance(map) - 5) < 0.1, "center clearance is half the width");
    expect(cycles(map) == 0, "no obstacle, no cycle");
    expect(std::abs(double(map.stats.walkableCells) - 40000) < 1000, "floor area in cells");
}

void pillar()
{
    Scene scene;
    scene.floor(0, 0, 20, 10);
    scene.box(9, 4, 0, 11, 6, 3);
    ecm::BuildSettings settings;
    settings.up = ecm::UpAxis::Z;
    const auto map = run("pillar", scene, settings);
    expect(cycles(map) == 1, "one cycle around the pillar");
    expect(map.boundary.size() == 2, "outer boundary and pillar hole");
}

void table(double agentHeight, std::size_t expectedCycles)
{
    Scene scene;
    scene.floor(0, 0, 10, 10);
    scene.box(4, 4, 0.75, 6, 6, 0.8);
    ecm::BuildSettings settings;
    settings.up = ecm::UpAxis::Z;
    settings.agentHeight = agentHeight;
    const auto map = run(("table, agent height " + std::to_string(agentHeight)).c_str(), scene, settings);
    expect(cycles(map) == expectedCycles, "table blocks only agents taller than the gap");
}

void step(double boxHeight, std::size_t expectedCycles)
{
    Scene scene;
    scene.floor(0, 0, 10, 10);
    scene.box(4, 4, 0, 6, 6, boxHeight);
    ecm::BuildSettings settings;
    settings.up = ecm::UpAxis::Z;
    const auto map = run(("box of height " + std::to_string(boxHeight)).c_str(), scene, settings);
    expect(cycles(map) == expectedCycles, "boxes lower than a step are walkable");
}

void mezzanine()
{
    Scene scene;
    scene.floor(0, 0, 20, 20);
    scene.box(5, 5, 2.8, 15, 15, 3);
    scene.box(9.5, 9.5, 0, 10.5, 10.5, 2.8); // supporting column
    ecm::BuildSettings settings;
    settings.up = ecm::UpAxis::Z;
    const auto map = run("mezzanine", scene, settings);
    expect(std::abs(map.stats.floorHeight) < 1e-6, "ground floor selected");
    expect(cycles(map) == 1, "only the column is an obstacle below the mezzanine");
}

void ramp(double maxSlope, bool connected)
{
    Scene scene;
    scene.floor(0, 0, 10, 10);
    scene.quad({10, 0, 0}, {14, 0, 2}, {14, 10, 2}, {10, 10, 0}); // 26.6 degrees
    scene.box(14, 0, 0, 20, 10, 2);
    ecm::BuildSettings settings;
    settings.up = ecm::UpAxis::Z;
    settings.maxSlopeDegrees = maxSlope;
    const auto map = run(("ramp, max slope " + std::to_string(maxSlope)).c_str(), scene, settings);
    const double area = double(map.stats.walkableCells) * map.stats.cellSize * map.stats.cellSize;
    expect(connected ? area > 190 : std::abs(area - 100) < 3, "ramp joins the platform only when walkable");
}

void yUpCentimeters()
{
    Scene scene;
    scene.floor(0, 0, 1000, 1000);
    scene.box(400, 400, 0, 600, 600, 300);
    for (auto &v : scene.mesh.vertices)
        v = {v.x, v.z, -v.y};
    ecm::BuildSettings settings;
    settings.up = ecm::UpAxis::Y;
    settings.metersPerUnit = 0.01;
    const auto map = run("Y-up centimeters", scene, settings);
    expect(cycles(map) == 1, "one cycle around the box");
    expect(std::abs(map.stats.cellSize - 0.05) < 1e-9, "cell size in meters");
    for (const auto &v : map.vertices)
        expect(std::abs(v.position.y) < 1e-3, "medial axis lies on the Y-up floor");
}

void invalidSettings()
{
    std::printf("invalid settings\n");
    Scene scene;
    scene.floor(0, 0, 1, 1);
    ecm::BuildSettings settings;
    settings.maxSlopeDegrees = 95;
    bool threw = false;
    try
    {
        ecm::build(scene.mesh, settings);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    expect(threw, "slope above 90 degrees is rejected");
}
} // namespace

int main()
{
    try
    {
        openSquare();
        pillar();
        table(1.8, 1);
        table(0.5, 0);
        step(0.2, 0);
        step(0.6, 1);
        mezzanine();
        ramp(45, true);
        ramp(20, false);
        yUpCentimeters();
        invalidSettings();
    }
    catch (const std::exception &e)
    {
        std::printf("  EXCEPTION: %s\n", e.what());
        ++failures;
    }
    std::printf(failures ? "%d failure(s)\n" : "All ECM tests passed.\n", failures);
    return failures ? 1 : 0;
}
