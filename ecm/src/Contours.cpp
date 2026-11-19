#include "Contours.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ecm::detail
{
namespace
{
int64_t cross(IPoint o, IPoint a, IPoint b)
{
    return int64_t(a.x - o.x) * (b.y - o.y) - int64_t(a.y - o.y) * (b.x - o.x);
}
int64_t area2(const Loop &loop)
{
    int64_t sum = 0;
    for (std::size_t i = 0; i < loop.size(); ++i)
    {
        const auto &a = loop[i], &b = loop[(i + 1) % loop.size()];
        sum += int64_t(a.x) * b.y - int64_t(b.x) * a.y;
    }
    return sum;
}

// Clears one cell of every 2x2 block whose free cells touch only diagonally. Afterwards each
// grid corner has at most one outgoing boundary edge, so boundary loops never share a vertex.
void removeDiagonalContacts(std::vector<uint8_t> &mask, int width, int height)
{
    for (bool changed = true; changed;)
    {
        changed = false;
        for (int y = 0; y + 1 < height; ++y)
            for (int x = 0; x + 1 < width; ++x)
            {
                uint8_t *row0 = &mask[std::size_t(y) * width + x], *row1 = row0 + width;
                const bool a = row0[0], b = row0[1], c = row1[0], d = row1[1];
                if (a && d && !b && !c)
                {
                    row0[0] = 0;
                    changed = true;
                }
                else if (b && c && !a && !d)
                {
                    row0[1] = 0;
                    changed = true;
                }
            }
    }
}

std::size_t keepLargestComponent(std::vector<uint8_t> &mask, int width, int height)
{
    std::vector<int32_t> label(mask.size(), -1);
    std::vector<std::size_t> sizes;
    std::vector<int32_t> stack;
    for (int32_t start = 0; start < int32_t(mask.size()); ++start)
    {
        if (!mask[start] || label[start] != -1)
            continue;
        const int32_t id = int32_t(sizes.size());
        sizes.push_back(0);
        label[start] = id;
        stack.assign(1, start);
        while (!stack.empty())
        {
            const int32_t c = stack.back();
            stack.pop_back();
            ++sizes[id];
            const int x = c % width, y = c / width;
            const int32_t neighbors[4] = {x > 0 ? c - 1 : -1, x + 1 < width ? c + 1 : -1, y > 0 ? c - width : -1,
                                          y + 1 < height ? c + width : -1};
            for (const int32_t n : neighbors)
                if (n >= 0 && mask[n] && label[n] == -1)
                {
                    label[n] = id;
                    stack.push_back(n);
                }
        }
    }
    if (sizes.empty())
        return 0;
    const int32_t keep = int32_t(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
    for (std::size_t i = 0; i < mask.size(); ++i)
        mask[i] = mask[i] && label[i] == keep;
    return sizes[keep];
}

std::vector<Loop> traceLoops(const std::vector<uint8_t> &mask, int width, int height)
{
    const int corners = width + 1;
    std::vector<int32_t> next(std::size_t(corners) * (height + 1), -1);
    const auto id = [corners](int x, int y) { return int32_t(y * corners + x); };
    const auto free = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && mask[std::size_t(y) * width + x];
    };
    const auto link = [&](int32_t from, int32_t to) {
        if (next[from] != -1)
            throw std::logic_error("Boundary corner has two outgoing edges.");
        next[from] = to;
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            if (!free(x, y))
                continue;
            if (!free(x, y - 1))
                link(id(x, y), id(x + 1, y));
            if (!free(x + 1, y))
                link(id(x + 1, y), id(x + 1, y + 1));
            if (!free(x, y + 1))
                link(id(x + 1, y + 1), id(x, y + 1));
            if (!free(x - 1, y))
                link(id(x, y + 1), id(x, y));
        }
    std::vector<Loop> loops;
    for (int32_t start = 0; start < int32_t(next.size()); ++start)
    {
        if (next[start] == -1)
            continue;
        Loop raw;
        for (int32_t c = start; next[c] != -1;)
        {
            raw.push_back({c % corners, c / corners});
            const int32_t following = next[c];
            next[c] = -1;
            c = following;
        }
        // Keep only corners where the direction changes.
        Loop loop;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const auto &prev = raw[(i + raw.size() - 1) % raw.size()], &cur = raw[i],
                       &nxt = raw[(i + 1) % raw.size()];
            if (cross(prev, cur, nxt) != 0)
                loop.push_back(cur);
        }
        loops.push_back(std::move(loop));
    }
    return loops;
}

double segmentDistance2(IPoint p, IPoint a, IPoint b)
{
    const double abx = b.x - a.x, aby = b.y - a.y, apx = p.x - a.x, apy = p.y - a.y;
    const double len2 = abx * abx + aby * aby;
    const double t = len2 > 0 ? std::clamp((apx * abx + apy * aby) / len2, 0.0, 1.0) : 0.0;
    const double dx = apx - t * abx, dy = apy - t * aby;
    return dx * dx + dy * dy;
}

Loop simplify(const Loop &loop, double tolerance)
{
    const std::size_t n = loop.size();
    if (tolerance <= 0 || n <= 4)
        return loop;
    // Split the closed loop at vertex 0 and the vertex farthest from it.
    std::size_t far = 0;
    double farDistance = -1;
    for (std::size_t i = 1; i < n; ++i)
    {
        const double dx = loop[i].x - loop[0].x, dy = loop[i].y - loop[0].y;
        if (dx * dx + dy * dy > farDistance)
        {
            farDistance = dx * dx + dy * dy;
            far = i;
        }
    }
    std::vector<uint8_t> keep(n, 0);
    keep[0] = keep[far] = 1;
    const double limit = tolerance * tolerance;
    std::vector<std::pair<std::size_t, std::size_t>> stack{{0, far}, {far, n}};
    while (!stack.empty())
    {
        const auto [first, last] = stack.back();
        stack.pop_back();
        const IPoint a = loop[first], b = loop[last % n];
        double worst = -1;
        std::size_t worstIndex = 0;
        for (std::size_t i = first + 1; i < last; ++i)
        {
            const double d = segmentDistance2(loop[i], a, b);
            if (d > worst)
            {
                worst = d;
                worstIndex = i;
            }
        }
        if (worst > limit)
        {
            keep[worstIndex] = 1;
            stack.push_back({first, worstIndex});
            stack.push_back({worstIndex, last});
        }
    }
    Loop result;
    for (std::size_t i = 0; i < n; ++i)
        if (keep[i])
            result.push_back(loop[i]);
    const int64_t before = area2(loop), after = area2(result);
    if (result.size() < 3 || after == 0 || (after > 0) != (before > 0))
        return loop;
    return result;
}

bool onSegment(IPoint p, IPoint a, IPoint b)
{
    return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
           p.y <= std::max(a.y, b.y);
}
// True if the closed segments share any point.
bool touches(IPoint a, IPoint b, IPoint c, IPoint d)
{
    const int64_t d1 = cross(a, b, c), d2 = cross(a, b, d), d3 = cross(c, d, a), d4 = cross(c, d, b);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        return true;
    return (d1 == 0 && onSegment(c, a, b)) || (d2 == 0 && onSegment(d, a, b)) || (d3 == 0 && onSegment(a, c, d)) ||
           (d4 == 0 && onSegment(b, c, d));
}

// Marks every loop that has a segment touching a non-adjacent segment, or folding back on
// its neighbour.
std::vector<uint8_t> findInvalidLoops(const std::vector<Loop> &loops, int width, int height)
{
    struct Segment
    {
        IPoint a, b;
        int32_t loop, index;
    };
    std::vector<Segment> segments;
    for (int32_t l = 0; l < int32_t(loops.size()); ++l)
        for (int32_t i = 0; i < int32_t(loops[l].size()); ++i)
            segments.push_back({loops[l][i], loops[l][(i + 1) % loops[l].size()], l, i});
    constexpr int bucket = 16;
    const int bw = width / bucket + 2, bh = height / bucket + 2;
    std::vector<std::vector<int32_t>> buckets(std::size_t(bw) * bh);
    for (int32_t s = 0; s < int32_t(segments.size()); ++s)
    {
        const auto &seg = segments[s];
        const int x0 = std::clamp(std::min(seg.a.x, seg.b.x) / bucket, 0, bw - 1);
        const int x1 = std::clamp(std::max(seg.a.x, seg.b.x) / bucket, 0, bw - 1);
        const int y0 = std::clamp(std::min(seg.a.y, seg.b.y) / bucket, 0, bh - 1);
        const int y1 = std::clamp(std::max(seg.a.y, seg.b.y) / bucket, 0, bh - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                buckets[std::size_t(y) * bw + x].push_back(s);
    }
    std::vector<uint8_t> invalid(loops.size(), 0);
    for (const auto &list : buckets)
        for (std::size_t i = 0; i < list.size(); ++i)
            for (std::size_t j = i + 1; j < list.size(); ++j)
            {
                const auto &s = segments[list[i]], &t = segments[list[j]];
                if (invalid[s.loop] && invalid[t.loop])
                    continue;
                bool bad;
                const int32_t n = int32_t(loops[s.loop].size());
                const bool tFollows = s.loop == t.loop && (s.index + 1) % n == t.index;
                const bool sFollows = s.loop == t.loop && (t.index + 1) % n == s.index;
                if (tFollows || sFollows)
                {
                    // Adjacent segments share one end point; reject only a fold back onto each other.
                    const auto &first = tFollows ? s : t, &second = tFollows ? t : s;
                    const int64_t turn = cross(first.a, first.b, second.b);
                    const int64_t dot = int64_t(first.b.x - first.a.x) * (second.b.x - second.a.x) +
                                        int64_t(first.b.y - first.a.y) * (second.b.y - second.a.y);
                    bad = turn == 0 && dot < 0;
                }
                else
                    bad = touches(s.a, s.b, t.a, t.b);
                if (bad)
                    invalid[s.loop] = invalid[t.loop] = 1;
            }
    return invalid;
}
} // namespace

BoundaryResult extractBoundary(std::vector<uint8_t> &mask, int width, int height, double tolerance)
{
    BoundaryResult result;
    removeDiagonalContacts(mask, width, height);
    result.freeCells = keepLargestComponent(mask, width, height);
    if (!result.freeCells)
        return result;
    const auto raw = traceLoops(mask, width, height);
    result.loops.resize(raw.size());
    std::vector<double> loopTolerance(raw.size(), tolerance);
    std::vector<uint8_t> pending(raw.size(), 1);
    // Simplify; any loop involved in an intersection is redone at half the tolerance until the
    // exact staircase (always valid) is reached.
    for (;;)
    {
        bool any = false;
        for (std::size_t i = 0; i < raw.size(); ++i)
            if (pending[i])
            {
                result.loops[i] = simplify(raw[i], loopTolerance[i]);
                any = true;
            }
        if (!any)
            break;
        const auto invalid = findInvalidLoops(result.loops, width, height);
        bool unresolved = false;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            pending[i] = invalid[i];
            if (invalid[i])
            {
                if (loopTolerance[i] == 0)
                    throw std::logic_error("Exact boundary loops intersect.");
                loopTolerance[i] = loopTolerance[i] < 0.3 ? 0 : loopTolerance[i] * 0.5;
                unresolved = true;
            }
        }
        if (!unresolved)
            break;
    }
    return result;
}
} // namespace ecm::detail
