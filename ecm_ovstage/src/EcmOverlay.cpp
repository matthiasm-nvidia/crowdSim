#include <ecm_ovstage/EcmOverlay.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <vector>

namespace ecm_ovstage
{
namespace
{
using Color = std::array<float, 3>;

Color narrowColor()
{
    return {0.9f, 0.12f, 0.1f};
}
// Yellow at the actor radius through green and cyan to blue at eight times the radius.
Color clearanceColor(double clearance, double radius)
{
    if (clearance < radius)
        return narrowColor();
    static constexpr Color stops[] = {{1.0f, 0.85f, 0.1f}, {0.25f, 0.85f, 0.25f}, {0.1f, 0.75f, 0.95f}, {0.35f, 0.35f, 1.0f}};
    const double t = std::clamp(std::log(clearance / radius) / std::log(8.0), 0.0, 1.0) * 3;
    const int i = std::min(2, int(t));
    const float f = float(t - i);
    Color c;
    for (int k = 0; k < 3; ++k)
        c[k] = stops[i][k] + (stops[i + 1][k] - stops[i][k]) * f;
    return c;
}

class Curves
{
  public:
    void add(const std::vector<ecm::Vec3> &curve, const std::vector<Color> &curveColors)
    {
        if (curve.size() < 2 || std::all_of(curve.begin(), curve.end(), [&](const ecm::Vec3 &p) {
                return p.x == curve[0].x && p.y == curve[0].y && p.z == curve[0].z;
            }))
            return;
        counts.push_back(int(curve.size()));
        points.insert(points.end(), curve.begin(), curve.end());
        colors.insert(colors.end(), curveColors.begin(), curveColors.end());
    }
    void write(std::ostream &os, const char *name, double width) const
    {
        if (counts.empty())
            return;
        os << "        def BasisCurves \"" << name << "\"\n        {\n"
           << "            uniform token type = \"linear\"\n"
           << "            uniform token wrap = \"nonperiodic\"\n"
           << "            bool primvars:doNotCastShadows = 1\n"
           << "            int[] curveVertexCounts = [";
        for (std::size_t i = 0; i < counts.size(); ++i)
            os << (i ? ", " : "") << counts[i];
        os << "]\n            point3f[] points = [";
        double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto &p = points[i];
            os << (i ? ", " : "") << '(' << p.x << ", " << p.y << ", " << p.z << ')';
            const double v[3] = {p.x, p.y, p.z};
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], v[k] - width);
                hi[k] = std::max(hi[k], v[k] + width);
            }
        }
        os << "]\n            float3[] extent = [(" << lo[0] << ", " << lo[1] << ", " << lo[2] << "), (" << hi[0]
           << ", " << hi[1] << ", " << hi[2] << ")]\n"
           << "            float[] widths = [" << width << "] (\n                interpolation = \"constant\"\n"
           << "            )\n            color3f[] primvars:displayColor = [";
        for (std::size_t i = 0; i < colors.size(); ++i)
            os << (i ? ", " : "") << '(' << colors[i][0] << ", " << colors[i][1] << ", " << colors[i][2] << ')';
        os << "] (\n                interpolation = \"vertex\"\n            )\n        }\n";
    }

  private:
    std::vector<int> counts;
    std::vector<ecm::Vec3> points;
    std::vector<Color> colors;
};
} // namespace

std::string ecmOverlayLayer(const ecm::Map &map, const EcmDisplay &display, const std::string &root)
{
    const auto &settings = map.settings;
    const double units = 1 / settings.metersPerUnit;
    const double radius = settings.actorRadius;
    // Lift lines a few centimeters so they do not z-fight with the floor.
    const double lift = 0.03 * units;
    const auto lifted = [&](ecm::Vec3 p) {
        (settings.up == ecm::UpAxis::Z ? p.z : p.y) += lift;
        return p;
    };
    const auto color = [&](double clearance) {
        if (clearance < radius)
            return narrowColor();
        return display.clearanceColors ? clearanceColor(clearance, radius) : Color{0.1f, 0.9f, 1.0f};
    };

    Curves axis, spokesLeft, spokesRight, boundary;
    if (display.medialAxis)
        for (const auto &edge : map.edges)
        {
            std::vector<ecm::Vec3> points;
            std::vector<Color> colors;
            const auto flush = [&] {
                axis.add(points, colors);
                points.clear();
                colors.clear();
            };
            for (std::size_t i = 0; i < edge.samples.size(); ++i)
            {
                const auto &s = edge.samples[i];
                const bool wide = display.narrow || s.clearance >= radius;
                if (i && !display.narrow)
                {
                    // Cut the curve exactly where the clearance crosses the actor radius.
                    const auto &prev = edge.samples[i - 1];
                    const bool prevWide = prev.clearance >= radius;
                    if (wide != prevWide)
                    {
                        const double t = (radius - prev.clearance) / (s.clearance - prev.clearance);
                        const ecm::Vec3 cut{prev.position.x + (s.position.x - prev.position.x) * t,
                                            prev.position.y + (s.position.y - prev.position.y) * t,
                                            prev.position.z + (s.position.z - prev.position.z) * t};
                        points.push_back(lifted(cut));
                        colors.push_back(color(radius));
                        if (!wide)
                            flush();
                    }
                }
                if (wide)
                {
                    points.push_back(lifted(s.position));
                    colors.push_back(color(s.clearance));
                }
            }
            flush();
            if (display.spokes)
                for (const auto &s : edge.samples)
                    if (s.event && (display.narrow || s.clearance >= radius))
                    {
                        spokesLeft.add({lifted(s.position), lifted(s.left)}, {Color{1, 0.55f, 0.1f}, Color{1, 0.55f, 0.1f}});
                        spokesRight.add({lifted(s.position), lifted(s.right)},
                                        {Color{0.8f, 0.3f, 1}, Color{0.8f, 0.3f, 1}});
                    }
        }
    if (display.boundary)
        for (const auto &loop : map.boundary)
        {
            std::vector<ecm::Vec3> points;
            for (const auto &p : loop)
                points.push_back(lifted(p));
            points.push_back(points.front());
            boundary.add(points, std::vector<Color>(points.size(), Color{0.85f, 0.85f, 0.85f}));
        }

    std::ostringstream os;
    os.precision(9);
    os << "#usda 1.0\n";
    // Every component but the last already exists on the stage.
    std::vector<std::string> parts;
    for (std::size_t start = 1; start < root.size();)
    {
        const auto end = std::min(root.find('/', start), root.size());
        parts.push_back(root.substr(start, end - start));
        start = end + 1;
    }
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        os << "over \"" << parts[i] << "\"\n{\n";
    os << "    def Scope \"" << parts.back() << "\"\n    {\n";
    const double width = display.lineWidth * units;
    axis.write(os, "MedialAxis", width);
    spokesLeft.write(os, "LeftSpokes", width * 0.5);
    spokesRight.write(os, "RightSpokes", width * 0.5);
    boundary.write(os, "Boundary", width * 0.6);
    os << "    }\n";
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        os << "}\n";
    return os.str();
}
} // namespace ecm_ovstage
