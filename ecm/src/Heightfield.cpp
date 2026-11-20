#include "Heightfield.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace ecm::detail
{
namespace
{
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Solid vertical interval in one grid column. `walkable` describes its top surface.
struct Span
{
    float lo, hi;
    int32_t next;
    uint8_t walkable;
};

class Heightfield
{
  public:
    Heightfield(int width, int height, float mergeTolerance)
        : width(width), height(height), heads(std::size_t(width) * height, -1), mergeTolerance(mergeTolerance)
    {
    }
    // Inserts a span into a column, merging every span it overlaps. When merged tops are within
    // the step height either walkable flag wins; otherwise the higher top decides.
    void add(std::size_t column, float lo, float hi, bool walkable)
    {
        int32_t previous = -1, current = heads[column];
        while (current != -1)
        {
            const Span &span = spans[current];
            if (span.lo > hi)
                break;
            if (span.hi < lo)
            {
                previous = current;
                current = span.next;
                continue;
            }
            lo = std::min(lo, span.lo);
            if (std::abs(span.hi - hi) <= mergeTolerance)
            {
                walkable = walkable || span.walkable;
                hi = std::max(hi, span.hi);
            }
            else if (span.hi > hi)
            {
                hi = span.hi;
                walkable = span.walkable != 0;
            }
            const int32_t next = span.next;
            release(current);
            if (previous == -1)
                heads[column] = next;
            else
                spans[previous].next = next;
            current = next;
        }
        const int32_t created = allocate();
        spans[created] = {lo, hi, current, uint8_t(walkable)};
        if (previous == -1)
            heads[column] = created;
        else
            spans[previous].next = created;
    }

    int width, height;
    std::vector<int32_t> heads;
    std::vector<Span> spans;

  private:
    int32_t allocate()
    {
        if (freeList != -1)
        {
            const int32_t index = freeList;
            freeList = spans[index].next;
            return index;
        }
        if (spans.size() >= std::size_t(std::numeric_limits<int32_t>::max()))
            throw std::runtime_error("Heightfield is too large; increase the cell size.");
        spans.push_back({});
        return int32_t(spans.size() - 1);
    }
    void release(int32_t index)
    {
        spans[index].next = freeList;
        freeList = index;
    }
    float mergeTolerance;
    int32_t freeList = -1;
};

// Sutherland-Hodgman clip of a convex polygon against one axis-aligned half plane.
int clip(const Vec3 *in, int count, Vec3 *out, int axis, double value, bool keepGreater)
{
    const auto coordinate = [axis](const Vec3 &p) { return axis == 0 ? p.x : p.y; };
    int written = 0;
    for (int i = 0; i < count; ++i)
    {
        const Vec3 &a = in[i], &b = in[(i + 1) % count];
        const double da = (coordinate(a) - value) * (keepGreater ? 1 : -1);
        const double db = (coordinate(b) - value) * (keepGreater ? 1 : -1);
        if (da >= 0)
            out[written++] = a;
        if ((da >= 0) != (db >= 0))
        {
            const double t = da / (da - db);
            out[written++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }
    }
    return written;
}

void rasterize(Heightfield &field, const Vec3 (&triangle)[3], bool walkable, double originU, double originV,
               double cell)
{
    double minU = triangle[0].x, maxU = minU, minV = triangle[0].y, maxV = minV;
    for (const auto &p : triangle)
    {
        minU = std::min(minU, p.x);
        maxU = std::max(maxU, p.x);
        minV = std::min(minV, p.y);
        maxV = std::max(maxV, p.y);
    }
    const int x0 = std::clamp(int(std::floor((minU - originU) / cell)), 0, field.width - 1);
    const int x1 = std::clamp(int(std::floor((maxU - originU) / cell)), 0, field.width - 1);
    const int y0 = std::clamp(int(std::floor((minV - originV) / cell)), 0, field.height - 1);
    const int y1 = std::clamp(int(std::floor((maxV - originV) / cell)), 0, field.height - 1);
    // Walls project to (nearly) zero plan area; they mark every cell they pass through.
    const double planArea = (triangle[1].x - triangle[0].x) * (triangle[2].y - triangle[0].y) -
                            (triangle[2].x - triangle[0].x) * (triangle[1].y - triangle[0].y);
    const bool vertical = std::abs(planArea) <= 1e-6 * cell * cell;
    Vec3 row[8], rowTmp[8], cellPoly[8], cellTmp[8];
    for (int y = y0; y <= y1; ++y)
    {
        const double v0 = originV + y * cell, v1 = v0 + cell;
        int n = clip(triangle, 3, rowTmp, 1, v0, true);
        n = clip(rowTmp, n, row, 1, v1, false);
        if (n < 3)
            continue;
        for (int x = x0; x <= x1; ++x)
        {
            const double u0 = originU + x * cell, u1 = u0 + cell;
            int m = clip(row, n, cellTmp, 0, u0, true);
            m = clip(cellTmp, m, cellPoly, 0, u1, false);
            if (m < 3)
                continue;
            if (!vertical)
            {
                // A sloped triangle that only touches the cell along an edge does not cover it.
                double area = 0;
                for (int i = 0; i < m; ++i)
                    area += cellPoly[i].x * cellPoly[(i + 1) % m].y - cellPoly[(i + 1) % m].x * cellPoly[i].y;
                if (std::abs(area) <= 1e-9 * cell * cell)
                    continue;
            }
            double lo = cellPoly[0].z, hi = lo;
            for (int i = 1; i < m; ++i)
            {
                lo = std::min(lo, cellPoly[i].z);
                hi = std::max(hi, cellPoly[i].z);
            }
            field.add(std::size_t(y) * field.width + x, float(lo), float(hi), walkable);
        }
    }
}
} // namespace

double FloorGrid::heightAt(double x, double y) const
{
    const int cx = int(std::floor(x)), cy = int(std::floor(y));
    double best = std::numeric_limits<double>::quiet_NaN(), bestDistance = std::numeric_limits<double>::max();
    for (int ring = 0; ring <= 3; ++ring)
    {
        for (int j = cy - ring; j <= cy + ring; ++j)
            for (int i = cx - ring; i <= cx + ring; ++i)
            {
                if (std::max(std::abs(i - cx), std::abs(j - cy)) != ring || !walkable(i, j))
                    continue;
                const double dx = i + 0.5 - x, dy = j + 0.5 - y, distance = dx * dx + dy * dy;
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = floor[std::size_t(j) * width + i];
                }
            }
        if (!std::isnan(best))
            return best;
    }
    return best;
}

FloorGrid buildFloor(const std::vector<Vec3> &vertices, const std::vector<uint32_t> &indices,
                     const FloorSettings &settings, BuildStats &stats)
{
    auto start = Clock::now();
    double minU = std::numeric_limits<double>::max(), minV = minU;
    double maxU = std::numeric_limits<double>::lowest(), maxV = maxU;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        for (int k = 0; k < 3; ++k)
        {
            const auto &p = vertices[indices[i + k]];
            minU = std::min(minU, p.x);
            maxU = std::max(maxU, p.x);
            minV = std::min(minV, p.y);
            maxV = std::max(maxV, p.y);
        }
    if (!(minU <= maxU && minV <= maxV))
        throw std::runtime_error("The scene contains no triangles.");

    // One cell of margin keeps the floor away from the grid border, so every boundary is closed.
    double cell = settings.cellSize;
    const auto dimensions = [&](double c) {
        return std::pair<double, double>{std::ceil((maxU - minU) / c) + 2, std::ceil((maxV - minV) / c) + 2};
    };
    auto [w, h] = dimensions(cell);
    if (w * h > double(settings.maxCells))
    {
        cell *= std::sqrt(w * h / double(settings.maxCells)) * 1.001;
        std::tie(w, h) = dimensions(cell);
    }
    FloorGrid grid;
    grid.width = int(w);
    grid.height = int(h);
    grid.cellSize = cell;
    grid.originU = minU - cell;
    grid.originV = minV - cell;

    Heightfield field(grid.width, grid.height, float(settings.maxStep));
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const Vec3 triangle[3] = {vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]};
        const Vec3 e1{triangle[1].x - triangle[0].x, triangle[1].y - triangle[0].y, triangle[1].z - triangle[0].z};
        const Vec3 e2{triangle[2].x - triangle[0].x, triangle[2].y - triangle[0].y, triangle[2].z - triangle[0].z};
        const Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (!(length > 0) || !std::isfinite(length))
            continue;
        rasterize(field, triangle, std::abs(n.z) >= settings.cosMaxSlope * length, grid.originU, grid.originV, cell);
    }
    stats.rasterizeMs = msSince(start);
    start = Clock::now();

    // Walkable surfaces: tops of walkable spans with enough free space above them.
    constexpr float open = std::numeric_limits<float>::infinity();
    const auto ceiling = [&](int32_t s) { return field.spans[s].next == -1 ? open : field.spans[field.spans[s].next].lo; };
    const auto standable = [&](int32_t s) {
        return field.spans[s].walkable && ceiling(s) - field.spans[s].hi >= settings.actorHeight;
    };
    // Flood fill 4-connected surfaces whose heights differ by at most a step and whose shared
    // opening fits the actor. The largest region is the floor.
    std::vector<int32_t> region(field.spans.size(), -1);
    std::vector<std::size_t> regionSize;
    std::vector<std::pair<int32_t, int32_t>> queue; // (column, span)
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int32_t column = 0; column < int32_t(field.heads.size()); ++column)
        for (int32_t s = field.heads[column]; s != -1; s = field.spans[s].next)
        {
            if (region[s] != -1 || !standable(s))
                continue;
            const int32_t id = int32_t(regionSize.size());
            regionSize.push_back(0);
            region[s] = id;
            queue.assign(1, {column, s});
            while (!queue.empty())
            {
                const auto [c, a] = queue.back();
                queue.pop_back();
                ++regionSize[id];
                const int cx = c % grid.width, cy = c / grid.width;
                const float floorA = field.spans[a].hi, ceilA = ceiling(a);
                for (int k = 0; k < 4; ++k)
                {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || ny < 0 || nx >= grid.width || ny >= grid.height)
                        continue;
                    const int32_t nc = ny * grid.width + nx;
                    for (int32_t b = field.heads[nc]; b != -1; b = field.spans[b].next)
                    {
                        if (region[b] != -1 || !standable(b))
                            continue;
                        const float floorB = field.spans[b].hi;
                        if (std::abs(floorB - floorA) > settings.maxStep ||
                            std::min(ceilA, ceiling(b)) - std::max(floorA, floorB) < settings.actorHeight)
                            continue;
                        region[b] = id;
                        queue.push_back({nc, b});
                    }
                }
            }
        }
    if (regionSize.empty())
        throw std::runtime_error("No walkable floor found. Check the slope, step and height settings.");
    const int32_t floorRegion =
        int32_t(std::max_element(regionSize.begin(), regionSize.end()) - regionSize.begin());

    grid.floor.assign(field.heads.size(), std::numeric_limits<float>::quiet_NaN());
    std::vector<float> heights;
    for (std::size_t column = 0; column < field.heads.size(); ++column)
        for (int32_t s = field.heads[column]; s != -1; s = field.spans[s].next)
            if (region[s] == floorRegion)
            {
                // A self-overlapping floor (e.g. a spiral ramp) keeps its lowest surface per column.
                grid.floor[column] = field.spans[s].hi;
                heights.push_back(field.spans[s].hi);
                break;
            }
    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
    stats.floorHeight = heights[heights.size() / 2];
    stats.gridWidth = grid.width;
    stats.gridHeight = grid.height;
    stats.floorMs = msSince(start);
    return grid;
}
} // namespace ecm::detail
