// Path: engine/src/main/cpp/include/box/renderer/camera3d.hpp
#pragma once

#include <algorithm>
#include <cmath>

#include "box/math/math.hpp"

namespace box::renderer {

/** Perspective right-handed camera looking down local negative Z for GLES 3.0 scenes. */
class Camera3D final {
public:
    math::Vec3 position{0.0f, 0.0f, 3.0f};
    math::Vec3 target{};
    math::Vec3 up{0.0f, 1.0f, 0.0f};
    float verticalFovRadians = 1.04719755f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    void setPerspective(float fovRadians, float nearValue, float farValue) noexcept {
        verticalFovRadians = std::clamp(fovRadians, 0.08726646f, 2.96705973f);
        nearPlane = std::max(nearValue, 0.001f);
        farPlane = std::max(farValue, nearPlane + 0.01f);
    }

    [[nodiscard]] math::Mat4 viewMatrix() const noexcept {
        const math::Vec3 forward = math::normalized(target - position);
        if (math::length(forward) <= math::kEpsilon) return math::Mat4::identity();
        math::Vec3 right = math::normalized(math::cross(forward, up));
        if (math::length(right) <= math::kEpsilon) {
            const math::Vec3 fallbackUp = std::fabs(forward.y) < 0.9f
                                              ? math::Vec3{0.0f, 1.0f, 0.0f}
                                              : math::Vec3{1.0f, 0.0f, 0.0f};
            right = math::normalized(math::cross(forward, fallbackUp));
        }
        const math::Vec3 correctedUp = math::cross(right, forward);
        math::Mat4 view = math::Mat4::identity();
        view.values[0] = right.x;
        view.values[4] = right.y;
        view.values[8] = right.z;
        view.values[12] = -math::dot(right, position);
        view.values[1] = correctedUp.x;
        view.values[5] = correctedUp.y;
        view.values[9] = correctedUp.z;
        view.values[13] = -math::dot(correctedUp, position);
        view.values[2] = -forward.x;
        view.values[6] = -forward.y;
        view.values[10] = -forward.z;
        view.values[14] = math::dot(forward, position);
        return view;
    }

    [[nodiscard]] math::Mat4 projectionMatrix(float aspectRatio) const noexcept {
        return math::Mat4::perspective(verticalFovRadians, std::max(aspectRatio, 0.001f),
                                       nearPlane, farPlane);
    }

    [[nodiscard]] math::Mat4 viewProjection(float aspectRatio) const noexcept {
        return projectionMatrix(aspectRatio) * viewMatrix();
    }

    [[nodiscard]] math::Frustum frustum(float aspectRatio) const noexcept {
        return math::Frustum::fromViewProjection(viewProjection(aspectRatio));
    }
};

}  // namespace box::renderer
