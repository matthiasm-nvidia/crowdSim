#pragma once
// Explicit Corridor Map (Geraerts 2010) for one walkable floor of an arbitrary triangle soup.
//
// Pipeline: triangles -> voxel heightfield -> largest connected walkable floor -> free-space
// boundary polygons -> segment Voronoi diagram -> medial axis annotated with clearance and the
// closest obstacle points on either side. Nothing here depends on USD or the viewer.
#include <cstdint>
#include <string>
#include <vector>

namespace ecm
{
struct Vec3
{
    double x = 0, y = 0, z = 0;
};

enum class UpAxis
{
    Y,
    Z
};

// World-space triangles in stage units. Winding and normals are ignored.
struct TriangleMesh
{
    std::vector<Vec3> vertices;
    std::vector<uint32_t> indices; // three per triangle
};

struct BuildSettings
{
    // Agent parameters, in meters / degrees.
    double agentRadius = 0.3;
    double agentHeight = 1.8;
    double maxSlopeDegrees = 45;
    double maxStepHeight = 0.4;
    // Horizontal voxel size in meters. Grows automatically when the grid would exceed maxCells.
    double cellSize = 0.05;
    std::size_t maxCells = std::size_t(4096) * 4096;
    // Scene description.
    UpAxis up = UpAxis::Y;
    double metersPerUnit = 1;
};

// One point along a medial-axis edge. Positions are world space (stage units) on the floor;
// clearance is in meters. left/right are the closest obstacle points when walking from the
// edge's start vertex to its end vertex.
struct Sample
{
    Vec3 position;
    double clearance = 0;
    Vec3 left, right;
    bool event = false; // a Voronoi vertex: the closest obstacle feature changes here
};

struct Vertex
{
    Vec3 position;
    double clearance = 0; // meters
    std::vector<uint32_t> edges;
};

struct Edge
{
    uint32_t from = 0, to = 0;
    std::vector<Sample> samples; // first and last coincide with the end vertices
    double length = 0;           // meters
    double minClearance = 0;     // meters
};

struct BuildStats
{
    std::size_t triangles = 0;
    int gridWidth = 0, gridHeight = 0;
    double cellSize = 0; // meters, after automatic growth
    std::size_t walkableCells = 0;
    std::size_t boundaryLoops = 0, boundarySegments = 0;
    std::size_t voronoiEdges = 0;
    double floorHeight = 0; // stage units, median of the selected floor
    double rasterizeMs = 0, floorMs = 0, contourMs = 0, voronoiMs = 0, graphMs = 0;
};

struct Map
{
    std::vector<Vertex> vertices;
    std::vector<Edge> edges;
    // Free-space boundary (obstacle outlines) at floor height, closed loops without a repeated end point.
    std::vector<std::vector<Vec3>> boundary;
    BuildStats stats;
    BuildSettings settings;
    bool empty() const
    {
        return edges.empty();
    }
};

// Throws std::invalid_argument for bad settings and std::runtime_error when no floor is found.
Map build(const TriangleMesh &mesh, const BuildSettings &settings);
} // namespace ecm
