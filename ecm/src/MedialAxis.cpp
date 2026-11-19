#include "MedialAxis.h"
#include <algorithm>
#include <boost/polygon/voronoi_builder.hpp>
#include <boost/polygon/voronoi_diagram.hpp>
#include <cmath>
#include <unordered_map>

namespace ecm::detail
{
namespace
{
using Diagram = boost::polygon::voronoi_diagram<double>;
using Cell = Diagram::cell_type;
using VEdge = Diagram::edge_type;
using VVertex = Diagram::vertex_type;

struct Segment
{
    IPoint a, b;
    bool startReflex, endReflex; // free space wraps more than 180 degrees around the end point
};

struct Site
{
    bool point;
    double ax, ay, bx, by;
    void closest(double x, double y, double &cx, double &cy) const
    {
        if (point)
        {
            cx = ax;
            cy = ay;
            return;
        }
        const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
        const double t = len2 > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0, 1.0) : 0.0;
        cx = ax + t * dx;
        cy = ay + t * dy;
    }
};

class Extractor
{
  public:
    explicit Extractor(const std::vector<Loop> &loops)
    {
        for (const auto &loop : loops)
        {
            const std::size_t n = loop.size();
            const auto reflex = [&](std::size_t i) {
                const IPoint &p = loop[(i + n - 1) % n], &c = loop[i], &q = loop[(i + 1) % n];
                return int64_t(c.x - p.x) * (q.y - c.y) - int64_t(c.y - p.y) * (q.x - c.x) < 0;
            };
            for (std::size_t i = 0; i < n; ++i)
                segments.push_back({loop[i], loop[(i + 1) % n], reflex(i), reflex((i + 1) % n)});
        }
    }
    std::vector<Segment> segments;

    Site site(const Cell &cell) const
    {
        const Segment &s = segments[cell.source_index()];
        if (cell.contains_segment())
            return {false, double(s.a.x), double(s.a.y), double(s.b.x), double(s.b.y)};
        const IPoint &p = cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT ? s.a : s.b;
        return {true, double(p.x), double(p.y), 0, 0};
    }
    // Whether the part of `cell` around point (x, y) lies in free space.
    bool free(const Cell &cell, double x, double y) const
    {
        const Segment &s = segments[cell.source_index()];
        if (cell.contains_segment())
            return double(s.b.x - s.a.x) * (y - s.a.y) - double(s.b.y - s.a.y) * (x - s.a.x) > 0;
        return cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT ? s.startReflex
                                                                                              : s.endReflex;
    }
};

AxisSample annotate(double x, double y, const Site &left, const Site &right)
{
    AxisSample sample;
    sample.x = x;
    sample.y = y;
    left.closest(x, y, sample.leftX, sample.leftY);
    right.closest(x, y, sample.rightX, sample.rightY);
    sample.clearance = std::min(std::hypot(x - sample.leftX, y - sample.leftY),
                                std::hypot(x - sample.rightX, y - sample.rightY));
    return sample;
}

// Samples one Voronoi edge from vertex0 to vertex1. The edge's own cell lies to its left.
std::vector<AxisSample> sampleEdge(const VEdge &edge, const Site &left, const Site &right, double spacing)
{
    const double x0 = edge.vertex0()->x(), y0 = edge.vertex0()->y();
    const double x1 = edge.vertex1()->x(), y1 = edge.vertex1()->y();
    std::vector<std::pair<double, double>> points;
    if (edge.is_curved())
    {
        // Parabola with the point site as focus and the segment site as directrix, sampled in
        // the segment's frame where it is y = ((x - px)^2 + py^2) / (2 py).
        const Site &point = left.point ? left : right, &segment = left.point ? right : left;
        double dx = segment.bx - segment.ax, dy = segment.by - segment.ay;
        const double length = std::hypot(dx, dy);
        dx /= length;
        dy /= length;
        double nx = -dy, ny = dx;
        const auto local = [&](double x, double y, double &u, double &v) {
            u = (x - segment.ax) * dx + (y - segment.ay) * dy;
            v = (x - segment.ax) * nx + (y - segment.ay) * ny;
        };
        double px, py, u0, v0, u1, v1;
        local(point.ax, point.ay, px, py);
        if (py < 0)
        {
            nx = -nx;
            ny = -ny;
            py = -py;
        }
        local(x0, y0, u0, v0);
        local(x1, y1, u1, v1);
        const double arc = std::max(std::hypot(x1 - x0, y1 - y0), std::abs(v1 - v0) + std::abs(u1 - u0));
        const int count = std::max(2, int(std::ceil(arc / spacing)) + 1);
        points.push_back({x0, y0});
        for (int i = 1; i + 1 < count; ++i)
        {
            const double u = u0 + (u1 - u0) * i / (count - 1);
            const double v = py > 0 ? ((u - px) * (u - px) + py * py) / (2 * py) : 0;
            points.push_back({segment.ax + dx * u + nx * v, segment.ay + dy * u + ny * v});
        }
        points.push_back({x1, y1});
    }
    else
    {
        const int count = std::max(2, int(std::ceil(std::hypot(x1 - x0, y1 - y0) / spacing)) + 1);
        for (int i = 0; i < count; ++i)
        {
            const double t = double(i) / (count - 1);
            points.push_back({x0 + (x1 - x0) * t, y0 + (y1 - y0) * t});
        }
    }
    std::vector<AxisSample> samples;
    for (const auto &[x, y] : points)
        samples.push_back(annotate(x, y, left, right));
    samples.front().event = samples.back().event = true;
    return samples;
}

void reverse(std::vector<AxisSample> &samples)
{
    std::reverse(samples.begin(), samples.end());
    for (auto &s : samples)
    {
        std::swap(s.leftX, s.rightX);
        std::swap(s.leftY, s.rightY);
    }
}
} // namespace

MedialAxis computeMedialAxis(const std::vector<Loop> &loops, double spacing)
{
    MedialAxis result;
    Extractor extractor(loops);
    if (extractor.segments.empty())
        return result;
    boost::polygon::voronoi_builder<int32_t> builder;
    for (const auto &s : extractor.segments)
        builder.insert_segment(s.a.x, s.a.y, s.b.x, s.b.y);
    Diagram diagram;
    builder.construct(&diagram);
    result.voronoiEdges = diagram.num_edges() / 2;

    // Raw medial-axis edges between Voronoi vertices.
    struct RawEdge
    {
        uint32_t from, to;
        std::vector<AxisSample> samples;
    };
    std::vector<RawEdge> raw;
    std::vector<AxisVertex> rawVertices;
    std::unordered_map<const VVertex *, uint32_t> vertexIds;
    const auto vertexId = [&](const VVertex *v, double clearance) {
        const auto [it, inserted] = vertexIds.try_emplace(v, uint32_t(rawVertices.size()));
        if (inserted)
            rawVertices.push_back({v->x(), v->y(), clearance});
        return it->second;
    };
    for (const auto &edge : diagram.edges())
    {
        if (edge.color())
            continue;
        edge.color(1);
        edge.twin()->color(1);
        // Secondary edges separate a segment from its own end point; they are not on the medial axis.
        if (!edge.is_primary() || !edge.is_finite())
            continue;
        const double mx = 0.5 * (edge.vertex0()->x() + edge.vertex1()->x());
        const double my = 0.5 * (edge.vertex0()->y() + edge.vertex1()->y());
        if (!extractor.free(*edge.cell(), mx, my) || !extractor.free(*edge.twin()->cell(), mx, my))
            continue;
        auto samples =
            sampleEdge(edge, extractor.site(*edge.cell()), extractor.site(*edge.twin()->cell()), spacing);
        const uint32_t from = vertexId(edge.vertex0(), samples.front().clearance);
        const uint32_t to = vertexId(edge.vertex1(), samples.back().clearance);
        raw.push_back({from, to, std::move(samples)});
    }

    // Merge chains through degree-two vertices.
    std::vector<std::vector<uint32_t>> incident(rawVertices.size());
    for (uint32_t e = 0; e < raw.size(); ++e)
    {
        incident[raw[e].from].push_back(e);
        incident[raw[e].to].push_back(e);
    }
    std::vector<int64_t> outputId(rawVertices.size(), -1);
    const auto keepVertex = [&](uint32_t v) {
        if (outputId[v] < 0)
        {
            outputId[v] = int64_t(result.vertices.size());
            result.vertices.push_back(rawVertices[v]);
        }
        return uint32_t(outputId[v]);
    };
    std::vector<uint8_t> used(raw.size(), 0);
    const auto walk = [&](uint32_t start, uint32_t firstEdge) {
        AxisEdge out;
        out.from = keepVertex(start);
        uint32_t current = start, e = firstEdge;
        for (;;)
        {
            used[e] = 1;
            auto samples = raw[e].samples;
            uint32_t other = raw[e].to;
            if (raw[e].from != current)
            {
                reverse(samples);
                other = raw[e].from;
            }
            out.samples.insert(out.samples.end(), samples.begin() + (out.samples.empty() ? 0 : 1), samples.end());
            current = other;
            if (outputId[current] >= 0 || incident[current].size() != 2)
                break;
            const uint32_t next = incident[current][0] == e ? incident[current][1] : incident[current][0];
            if (used[next])
                break;
            e = next;
        }
        out.to = keepVertex(current);
        result.edges.push_back(std::move(out));
    };
    for (uint32_t v = 0; v < rawVertices.size(); ++v)
        if (incident[v].size() != 2)
            keepVertex(v);
    for (uint32_t v = 0; v < rawVertices.size(); ++v)
        if (outputId[v] >= 0)
            for (const uint32_t e : incident[v])
                if (!used[e])
                    walk(v, e);
    // Cycles without junctions.
    for (uint32_t e = 0; e < raw.size(); ++e)
        if (!used[e])
            walk(raw[e].from, e);
    return result;
}
} // namespace ecm::detail
