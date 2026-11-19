#include <ecm_ovstage/StageMesh.h>
#include <ecm_ovstage/StageData.h>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace ecm_ovstage
{
namespace
{
using Matrix = std::array<double, 16>; // row-major, row vectors: p' = p * M
constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

Matrix multiply(const Matrix &a, const Matrix &b)
{
    Matrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
    return r;
}

// Local-space triangles of one prim.
struct Local
{
    std::vector<std::array<double, 3>> points;
    std::vector<uint32_t> triangles;
};

// Surface of revolution around local +Z from a (radius, z) profile.
Local revolve(const std::vector<std::array<double, 2>> &profile)
{
    constexpr int segments = 24;
    Local local;
    for (const auto &[r, z] : profile)
        for (int j = 0; j < segments; ++j)
        {
            const double angle = 2 * 3.14159265358979323846 * j / segments;
            local.points.push_back({r * std::cos(angle), r * std::sin(angle), z});
        }
    for (uint32_t i = 0; i + 1 < profile.size(); ++i)
        for (uint32_t j = 0; j < segments; ++j)
        {
            const uint32_t a = i * segments + j, b = i * segments + (j + 1) % segments;
            const uint32_t c = a + segments, d = b + segments;
            local.triangles.insert(local.triangles.end(), {a, b, d, a, d, c});
        }
    return local;
}

double attribute(const stage::Prim &prim, const char *name, double fallback)
{
    const auto found = prim.find(name);
    return found != prim.end() && !found->second.numbers.empty() ? found->second.numbers[0] : fallback;
}

// Implicit UsdGeom shapes with their schema fallbacks. Empty for anything else.
Local primitive(const std::string &type, const stage::Prim &prim)
{
    Local local;
    if (type == "Cube")
    {
        const double h = attribute(prim, "size", 2) / 2;
        for (int i = 0; i < 8; ++i)
            local.points.push_back({i & 1 ? h : -h, i & 2 ? h : -h, i & 4 ? h : -h});
        local.triangles = {0, 1, 3, 0, 3, 2, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4,
                           2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 3, 7, 1, 7, 5};
        return local;
    }
    std::vector<std::array<double, 2>> profile;
    const auto arc = [&](double radius, double center, double from, double to) {
        constexpr int steps = 8;
        for (int i = 0; i <= steps; ++i)
        {
            const double angle = from + (to - from) * i / steps;
            profile.push_back({radius * std::cos(angle), center + radius * std::sin(angle)});
        }
    };
    constexpr double halfPi = 1.57079632679489661923;
    if (type == "Sphere")
        arc(attribute(prim, "radius", 1), 0, -halfPi, halfPi);
    else if (type == "Cylinder" || type == "Cone")
    {
        const double r = attribute(prim, "radius", 1), h = attribute(prim, "height", 2) / 2;
        profile = {{0, -h}, {r, -h}};
        if (type == "Cylinder")
            profile.push_back({r, h});
        profile.push_back({0, h});
    }
    else if (type == "Capsule")
    {
        const double r = attribute(prim, "radius", 0.5), h = attribute(prim, "height", 1) / 2;
        arc(r, -h, -halfPi, 0);
        arc(r, h, 0, halfPi);
    }
    else
        return local;
    local = revolve(profile);
    // Shapes are authored around Z; map that axis to the authored one.
    const auto axis = prim.find("axis");
    const std::string name = axis != prim.end() ? axis->second.token : "Z";
    for (auto &p : local.points)
    {
        if (name == "X")
            p = {p[2], p[0], p[1]};
        else if (name == "Y")
            p = {p[1], p[2], p[0]};
    }
    return local;
}

Local mesh(const stage::Prim &prim)
{
    Local local;
    const auto points = prim.find("points"), counts = prim.find("faceVertexCounts"),
               indices = prim.find("faceVertexIndices");
    if (points == prim.end() || counts == prim.end() || indices == prim.end())
        return local;
    const auto &p = points->second.numbers;
    for (std::size_t i = 0; i + 2 < p.size(); i += 3)
        local.points.push_back({p[i], p[i + 1], p[i + 2]});
    const auto &index = indices->second.numbers;
    std::size_t offset = 0;
    for (const double countValue : counts->second.numbers)
    {
        const std::size_t count = std::size_t(countValue);
        if (offset + count > index.size())
            break;
        for (std::size_t k = 1; k + 1 < count; ++k)
        {
            const double a = index[offset], b = index[offset + k], c = index[offset + k + 1];
            if (a < local.points.size() && b < local.points.size() && c < local.points.size())
                local.triangles.insert(local.triangles.end(), {uint32_t(a), uint32_t(b), uint32_t(c)});
        }
        offset += count;
    }
    return local;
}

std::string parent(const std::string &path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos || slash == 0 ? std::string() : path.substr(0, slash);
}
} // namespace

StageTriangles extractStageTriangles(ovstage_instance_t *stageInstance, ovstage_ordinal_t ordinal,
                                     const std::string &skipRoot)
{
    StageTriangles result;
    // Prototype roots and the instance roots that place them.
    std::set<std::string> prototypes;
    std::map<std::string, std::string> prototypeOf; // instance root -> prototype root
    for (const auto &prototype : stage::prototypeRoots(stageInstance))
    {
        prototypes.insert(prototype);
        for (const auto &instance : stage::instanceRoots(stageInstance, prototype))
            prototypeOf[instance] = prototype;
    }
    const auto snapshot =
        stage::read(stageInstance, ordinal,
                    {"usd-prim-type", "points", "faceVertexCounts", "faceVertexIndices", "omni:fabric:worldMatrix",
                     "visibility", "purpose", "size", "radius", "height", "axis", "usd-metadata:upAxis",
                     "usd-metadata:metersPerUnit"});

    // The prototype whose subtree contains a path ("" for the composed scene), and inherited
    // invisibility or guide purpose up to that owner.
    const auto owner = [&](const std::string &path) {
        for (auto p = parent(path); !p.empty(); p = parent(p))
            if (prototypes.count(p))
                return p;
        return std::string();
    };
    const auto hidden = [&](const std::string &path) {
        for (auto p = path; !p.empty(); p = parent(p))
        {
            const auto found = snapshot.find(p);
            if (found == snapshot.end())
                continue;
            const auto visibility = found->second.find("visibility"), purpose = found->second.find("purpose");
            if ((visibility != found->second.end() && visibility->second.token == "invisible") ||
                (purpose != found->second.end() && purpose->second.token == "guide"))
                return true;
        }
        return false;
    };
    const auto matrix = [&](const stage::Prim &prim) {
        Matrix m = identity;
        const auto found = prim.find("omni:fabric:worldMatrix");
        if (found != prim.end() && found->second.numbers.size() == 16)
            std::copy(found->second.numbers.begin(), found->second.numbers.end(), m.begin());
        return m;
    };

    struct Gprim
    {
        const std::string *path;
        Local local;
    };
    std::map<std::string, std::vector<Gprim>> gprims;                 // owner -> geometry
    std::map<std::string, std::vector<const std::string *>> instances; // owner -> instance roots
    for (const auto &[path, prim] : snapshot)
    {
        if (const auto up = prim.find("usd-metadata:upAxis"); up != prim.end() && !up->second.token.empty())
            result.zUp = up->second.token == "Z";
        if (const auto mpu = prim.find("usd-metadata:metersPerUnit"); mpu != prim.end() && !mpu->second.numbers.empty())
            result.metersPerUnit = mpu->second.numbers[0];
        if (path == skipRoot || path.rfind(skipRoot + "/", 0) == 0)
            continue;
        if (prototypeOf.count(path))
        {
            instances[owner(path)].push_back(&path);
            continue;
        }
        const auto type = prim.find("usd-prim-type");
        if (type == prim.end())
            continue;
        Local local = type->second.token == "Mesh" ? mesh(prim) : primitive(type->second.token, prim);
        if (local.triangles.empty())
            continue;
        if (hidden(path))
        {
            ++result.hidden;
            continue;
        }
        (type->second.token == "Mesh" ? result.meshes : result.primitives)++;
        gprims[owner(path)].push_back({&path, std::move(local)});
    }

    auto &out = result.mesh;
    const auto emit = [&](const Local &local, const Matrix &m) {
        const uint32_t base = uint32_t(out.vertices.size());
        for (const auto &[x, y, z] : local.points)
            out.vertices.push_back({x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
                                    x * m[2] + y * m[6] + z * m[10] + m[14]});
        for (const uint32_t index : local.triangles)
            out.indices.push_back(base + index);
    };
    // Prototype prims carry transforms relative to their prototype root; an instance root's
    // world matrix places the prototype. Nested instances compose recursively.
    std::function<void(const std::string &, const Matrix &, int)> place = [&](const std::string &root,
                                                                              const Matrix &placement, int depth) {
        if (depth > 32)
            throw std::runtime_error("Instancing is nested too deeply.");
        if (const auto found = gprims.find(root); found != gprims.end())
            for (const auto &g : found->second)
                emit(g.local, multiply(matrix(snapshot.at(*g.path)), placement));
        if (const auto found = instances.find(root); found != instances.end())
            for (const auto *instance : found->second)
            {
                if (hidden(*instance))
                    continue;
                ++result.instances;
                place(prototypeOf.at(*instance), multiply(matrix(snapshot.at(*instance)), placement), depth + 1);
            }
    };
    place("", identity, 0);
    return result;
}
} // namespace ecm_ovstage
