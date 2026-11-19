#include "Camera.h"
#include <algorithm>

namespace viewer
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
Vec3 rotate(Vec3 v, Vec3 axis, float angle)
{
    axis.normalize();
    return v * std::cos(angle) + axis.cross(v) * std::sin(angle) + axis * (axis.dot(v) * (1 - std::cos(angle)));
}
} // namespace
void Camera::init()
{
    *this = Camera{};
    look_at(pos, {});
}
void Camera::look_at(Vec3 position, Vec3 target)
{
    pos = position;
    forward = target - pos;
    if (forward.normalize() == 0)
        forward = {0, 0, -1};
    right = forward.cross(orbitUp);
    if (right.normalize() < 1e-5f)
    {
        right = forward.cross(std::abs(forward.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
        right.normalize();
    }
    up = right.cross(forward);
    up.normalize();
}
void Camera::frame(Vec3 center, float radius, bool zUp, float aspect)
{
    radius = std::max(radius, 0.001f);
    orthographic = false;
    horizontalOffset = verticalOffset = 0;
    fov = 40;
    orbitUp = zUp ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    Vec3 direction = zUp ? Vec3{1, -1, 0.65f} : Vec3{1, 0.65f, 1};
    direction.normalize();
    const float halfAngle = std::atan(std::tan(fov * pi / 360) * std::min(1.0f, std::max(0.01f, aspect)));
    look_at(center + direction * (radius / std::sin(halfAngle) * 1.15f), center);
    nearClip = std::max(0.0001f, radius * 0.00001f);
    farClip = std::max(1000.0f, radius * 1000);
    speed = std::max(0.0001f, radius * 0.05f);
}
void Camera::orbit(float dx, float dy, Vec3 center)
{
    Vec3 offset = rotate(pos - center, orbitUp, -dx * 0.01f);
    Vec3 axis = rotate(right, orbitUp, -dx * 0.01f);
    Vec3 pitched = rotate(offset, axis, -dy * 0.01f);
    Vec3 direction = pitched;
    direction.normalize();
    if (std::abs(direction.dot(orbitUp)) < 0.995f)
        offset = pitched;
    look_at(center + offset, center);
}
void Camera::look(float dx, float dy)
{
    const Vec3 yawed = rotate(forward, orbitUp, -dx * pi / 360);
    Vec3 axis = yawed.cross(orbitUp);
    axis.normalize();
    const Vec3 pitched = rotate(yawed, axis, -dy * pi / 360);
    look_at(pos, pos + (std::abs(pitched.dot(orbitUp)) < 0.995f ? pitched : yawed));
}
void Camera::pan(float dx, float dy, float scale)
{
    pos += right * (-dx * scale) + up * (dy * scale);
}
void Camera::zoom(float amount)
{
    amount = std::clamp(amount, -10.0f, 10.0f);
    if (orthographic)
        orthographicHeight = std::max(0.00001f, orthographicHeight * std::exp(-0.1f * amount));
    else
        pos += forward * (speed * amount);
}
} // namespace viewer
