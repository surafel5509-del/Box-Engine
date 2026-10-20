#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace box::math {

constexpr float kEpsilon = 1.0e-6f;
constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float xValue, float yValue) : x(xValue), y(yValue) {}
    constexpr Vec2 operator+(Vec2 rhs) const { return {x + rhs.x, y + rhs.y}; }
    constexpr Vec2 operator-(Vec2 rhs) const { return {x - rhs.x, y - rhs.y}; }
    constexpr Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    constexpr Vec2 operator/(float scalar) const { return {x / scalar, y / scalar}; }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}
    constexpr Vec3 operator+(Vec3 rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    constexpr Vec3 operator-(Vec3 rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    constexpr Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    constexpr Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float xValue, float yValue, float zValue, float wValue)
        : x(xValue), y(yValue), z(zValue), w(wValue) {}
};

[[nodiscard]] constexpr float dot(Vec2 lhs, Vec2 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}
[[nodiscard]] constexpr float dot(Vec3 lhs, Vec3 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}
[[nodiscard]] constexpr Vec3 cross(Vec3 lhs, Vec3 rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}
[[nodiscard]] inline float length(Vec2 value) { return std::sqrt(dot(value, value)); }
[[nodiscard]] inline float length(Vec3 value) { return std::sqrt(dot(value, value)); }
[[nodiscard]] inline Vec2 normalized(Vec2 value) {
    const float magnitude = length(value);
    return magnitude > kEpsilon ? value / magnitude : Vec2{};
}
[[nodiscard]] inline Vec3 normalized(Vec3 value) {
    const float magnitude = length(value);
    return magnitude > kEpsilon ? value / magnitude : Vec3{};
}

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    [[nodiscard]] static Quat identity() { return {}; }

    [[nodiscard]] static Quat fromAxisAngle(Vec3 axis, float radians) {
        const Vec3 unitAxis = normalized(axis);
        const float halfAngle = radians * 0.5f;
        const float sine = std::sin(halfAngle);
        return {unitAxis.x * sine, unitAxis.y * sine, unitAxis.z * sine,
                std::cos(halfAngle)};
    }

    [[nodiscard]] Quat normalizedValue() const {
        const float magnitude = std::sqrt(x * x + y * y + z * z + w * w);
        if (magnitude <= kEpsilon) return {};
        return {x / magnitude, y / magnitude, z / magnitude, w / magnitude};
    }

    [[nodiscard]] Quat operator*(Quat rhs) const {
        return {w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
                w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
                w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
                w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z};
    }
};

/** Column-major 4x4 matrix compatible with OpenGL ES uniform uploads. */
struct Mat4 {
    std::array<float, 16> values{};

    [[nodiscard]] static Mat4 identity() {
        Mat4 result{};
        result.values[0] = 1.0f;
        result.values[5] = 1.0f;
        result.values[10] = 1.0f;
        result.values[15] = 1.0f;
        return result;
    }

    [[nodiscard]] static Mat4 translation(Vec3 value) {
        Mat4 result = identity();
        result.values[12] = value.x;
        result.values[13] = value.y;
        result.values[14] = value.z;
        return result;
    }

    [[nodiscard]] static Mat4 scale(Vec3 value) {
        Mat4 result{};
        result.values[0] = value.x;
        result.values[5] = value.y;
        result.values[10] = value.z;
        result.values[15] = 1.0f;
        return result;
    }

    [[nodiscard]] static Mat4 rotation(Quat input) {
        const Quat q = input.normalizedValue();
        const float xx = q.x * q.x;
        const float yy = q.y * q.y;
        const float zz = q.z * q.z;
        const float xy = q.x * q.y;
        const float xz = q.x * q.z;
        const float yz = q.y * q.z;
        const float wx = q.w * q.x;
        const float wy = q.w * q.y;
        const float wz = q.w * q.z;

        Mat4 result = identity();
        result.values[0] = 1.0f - 2.0f * (yy + zz);
        result.values[1] = 2.0f * (xy + wz);
        result.values[2] = 2.0f * (xz - wy);
        result.values[4] = 2.0f * (xy - wz);
        result.values[5] = 1.0f - 2.0f * (xx + zz);
        result.values[6] = 2.0f * (yz + wx);
        result.values[8] = 2.0f * (xz + wy);
        result.values[9] = 2.0f * (yz - wx);
        result.values[10] = 1.0f - 2.0f * (xx + yy);
        return result;
    }

    [[nodiscard]] static Mat4 orthographic(float left, float right, float bottom, float top,
                                            float nearPlane, float farPlane) {
        Mat4 result = identity();
        const float width = right - left;
        const float height = top - bottom;
        const float depth = farPlane - nearPlane;
        if (std::fabs(width) <= kEpsilon || std::fabs(height) <= kEpsilon ||
            std::fabs(depth) <= kEpsilon) {
            return result;
        }
        result.values[0] = 2.0f / width;
        result.values[5] = 2.0f / height;
        result.values[10] = -2.0f / depth;
        result.values[12] = -(right + left) / width;
        result.values[13] = -(top + bottom) / height;
        result.values[14] = -(farPlane + nearPlane) / depth;
        return result;
    }

    [[nodiscard]] static Mat4 perspective(float verticalFovRadians, float aspect,
                                           float nearPlane, float farPlane) {
        Mat4 result{};
        if (aspect <= kEpsilon || nearPlane <= 0.0f || farPlane <= nearPlane) return result;
        const float tangent = std::tan(verticalFovRadians * 0.5f);
        if (std::fabs(tangent) <= kEpsilon) return result;
        result.values[0] = 1.0f / (aspect * tangent);
        result.values[5] = 1.0f / tangent;
        result.values[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        result.values[11] = -1.0f;
        result.values[14] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
        return result;
    }

    [[nodiscard]] Mat4 operator*(const Mat4& rhs) const {
        Mat4 result{};
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (std::size_t index = 0; index < 4; ++index) {
                    sum += values[index * 4 + row] * rhs.values[column * 4 + index];
                }
                result.values[column * 4 + row] = sum;
            }
        }
        return result;
    }

    [[nodiscard]] Vec4 transform(Vec4 value) const {
        return {values[0] * value.x + values[4] * value.y + values[8] * value.z + values[12] * value.w,
                values[1] * value.x + values[5] * value.y + values[9] * value.z + values[13] * value.w,
                values[2] * value.x + values[6] * value.y + values[10] * value.z + values[14] * value.w,
                values[3] * value.x + values[7] * value.y + values[11] * value.z + values[15] * value.w};
    }
};

struct AABB {
    Vec3 minimum{};
    Vec3 maximum{};

    [[nodiscard]] bool contains(Vec3 point) const {
        return point.x >= minimum.x && point.x <= maximum.x &&
               point.y >= minimum.y && point.y <= maximum.y &&
               point.z >= minimum.z && point.z <= maximum.z;
    }

    [[nodiscard]] bool intersects(const AABB& other) const {
        return minimum.x <= other.maximum.x && maximum.x >= other.minimum.x &&
               minimum.y <= other.maximum.y && maximum.y >= other.minimum.y &&
               minimum.z <= other.maximum.z && maximum.z >= other.minimum.z;
    }
};

struct Ray {
    Vec3 origin{};
    Vec3 direction{0.0f, 0.0f, -1.0f};
};

[[nodiscard]] inline bool intersect(const Ray& ray, const AABB& bounds,
                                    float& distanceNear, float& distanceFar) {
    distanceNear = 0.0f;
    distanceFar = std::numeric_limits<float>::infinity();
    const float origins[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float directions[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float minimums[3] = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
    const float maximums[3] = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};

    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::fabs(directions[axis]) <= kEpsilon) {
            if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis]) return false;
            continue;
        }
        const float inverseDirection = 1.0f / directions[axis];
        float first = (minimums[axis] - origins[axis]) * inverseDirection;
        float second = (maximums[axis] - origins[axis]) * inverseDirection;
        if (first > second) std::swap(first, second);
        distanceNear = std::max(distanceNear, first);
        distanceFar = std::min(distanceFar, second);
        if (distanceNear > distanceFar) return false;
    }
    return distanceFar >= 0.0f;
}

struct Plane {
    Vec3 normal{};
    float distance = 0.0f;
};

struct Frustum {
    enum PlaneIndex : std::size_t { Left, Right, Bottom, Top, Near, Far, Count };
    std::array<Plane, Count> planes{};

    [[nodiscard]] static Frustum fromViewProjection(const Mat4& matrix) {
        // Extract matrix rows from the column-major storage and combine row 3 with rows 0-2.
        const float rows[4][4] = {
            {matrix.values[0], matrix.values[4], matrix.values[8], matrix.values[12]},
            {matrix.values[1], matrix.values[5], matrix.values[9], matrix.values[13]},
            {matrix.values[2], matrix.values[6], matrix.values[10], matrix.values[14]},
            {matrix.values[3], matrix.values[7], matrix.values[11], matrix.values[15]},
        };
        Frustum result{};
        // Assemble each clip plane explicitly so the sign of every row combination is clear.
        const float coefficients[Count][4] = {
            {rows[3][0] + rows[0][0], rows[3][1] + rows[0][1], rows[3][2] + rows[0][2], rows[3][3] + rows[0][3]},
            {rows[3][0] - rows[0][0], rows[3][1] - rows[0][1], rows[3][2] - rows[0][2], rows[3][3] - rows[0][3]},
            {rows[3][0] + rows[1][0], rows[3][1] + rows[1][1], rows[3][2] + rows[1][2], rows[3][3] + rows[1][3]},
            {rows[3][0] - rows[1][0], rows[3][1] - rows[1][1], rows[3][2] - rows[1][2], rows[3][3] - rows[1][3]},
            {rows[3][0] + rows[2][0], rows[3][1] + rows[2][1], rows[3][2] + rows[2][2], rows[3][3] + rows[2][3]},
            {rows[3][0] - rows[2][0], rows[3][1] - rows[2][1], rows[3][2] - rows[2][2], rows[3][3] - rows[2][3]},
        };
        for (std::size_t index = 0; index < Count; ++index) {
            const Vec3 normal{coefficients[index][0], coefficients[index][1], coefficients[index][2]};
            const float magnitude = length(normal);
            if (magnitude > kEpsilon) {
                result.planes[index] = {normal / magnitude, coefficients[index][3] / magnitude};
            }
        }
        return result;
    }

    [[nodiscard]] bool intersects(const AABB& bounds) const {
        for (const Plane& plane : planes) {
            const Vec3 positive{plane.normal.x >= 0.0f ? bounds.maximum.x : bounds.minimum.x,
                                plane.normal.y >= 0.0f ? bounds.maximum.y : bounds.minimum.y,
                                plane.normal.z >= 0.0f ? bounds.maximum.z : bounds.minimum.z};
            if (dot(plane.normal, positive) + plane.distance < 0.0f) return false;
        }
        return true;
    }
};

struct Transform {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] Mat4 matrix() const {
        return Mat4::translation(position) * Mat4::rotation(rotation) * Mat4::scale(scale);
    }
};

}  // namespace box::math
