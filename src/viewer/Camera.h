#pragma once
#include <cmath>

namespace viewer
{
struct Vec3
{
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    float &operator[](int i)
    {
        return i == 0 ? x : i == 1 ? y : z;
    }
    float operator[](int i) const
    {
        return i == 0 ? x : i == 1 ? y : z;
    }
    Vec3 operator+(Vec3 v) const
    {
        return {x + v.x, y + v.y, z + v.z};
    }
    Vec3 operator-(Vec3 v) const
    {
        return {x - v.x, y - v.y, z - v.z};
    }
    Vec3 operator*(float s) const
    {
        return {x * s, y * s, z * s};
    }
    Vec3 &operator+=(Vec3 v)
    {
        return *this = *this + v;
    }
    float dot(Vec3 v) const
    {
        return x * v.x + y * v.y + z * v.z;
    }
    Vec3 cross(Vec3 v) const
    {
        return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
    }
    float magnitude() const
    {
        return std::sqrt(dot(*this));
    }
    float normalize()
    {
        const float length = magnitude();
        if (length > 0)
            *this = *this * (1 / length);
        return length;
    }
};

// Orbit/fly controls adapted from fbik; no physics math or OpenGL state required.
class Camera
{
  public:
    void init();
    void look_at(Vec3 position, Vec3 target);
    void frame(Vec3 center, float radius, bool zUp, float aspect = 1);
    void orbit(float dx, float dy, Vec3 center);
    void look(float dx, float dy);
    void pan(float dx, float dy, float scale);
    void zoom(float amount);

    Vec3 pos{6, 4, 6}, forward{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0};
    Vec3 orbitUp{0, 1, 0};
    float speed = 0.1f, fov = 40;
    bool orthographic = false;
    float orthographicHeight = 1;
    float horizontalOffset = 0, verticalOffset = 0;
    float nearClip = 0.001f, farClip = 1e9f;
};
} // namespace viewer
