#include "box/renderer/camera2d.hpp"

#include <cmath>

namespace box::renderer {

void Camera2D::setViewport(ViewportRect value) noexcept {
    value.width = std::clamp(value.width, 0.01f, 1.0f);
    value.height = std::clamp(value.height, 0.01f, 1.0f);
    value.x = std::clamp(value.x, 0.0f, 1.0f - value.width);
    value.y = std::clamp(value.y, 0.0f, 1.0f - value.height);
    viewport = value;
}

void Camera2D::setPixelPerfect(bool enabled, float pixelsPerUnitValue) noexcept {
    pixelPerfect = enabled;
    pixelsPerUnit = std::max(pixelsPerUnitValue, 0.01f);
}

void Camera2D::addShake(float amplitude, float durationSeconds) noexcept {
    shakeAmplitude_ = std::max(amplitude, 0.0f);
    shakeDuration_ = std::max(durationSeconds, 0.0f);
    shakeElapsed_ = 0.0f;
}

void Camera2D::update(double deltaSeconds) noexcept {
    if (shakeDuration_ <= 0.0f) return;
    shakeElapsed_ = std::min(shakeDuration_, shakeElapsed_ +
                              static_cast<float>(std::max(deltaSeconds, 0.0)));
}

ViewportPixels Camera2D::viewportPixels(int surfaceWidth, int surfaceHeight,
                                        float renderScale) const noexcept {
    const float scale = std::clamp(renderScale, 0.5f, 1.0f);
    const int scaledWidth = std::max(1, static_cast<int>(std::lround(
        static_cast<double>(surfaceWidth) * scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::lround(
        static_cast<double>(surfaceHeight) * scale)));
    const int x = static_cast<int>(std::lround(static_cast<float>(scaledWidth) * viewport.x));
    const int y = static_cast<int>(std::lround(static_cast<float>(scaledHeight) * viewport.y));
    const int width = std::max(1, static_cast<int>(std::lround(
        static_cast<float>(scaledWidth) * viewport.width)));
    const int height = std::max(1, static_cast<int>(std::lround(
        static_cast<float>(scaledHeight) * viewport.height)));
    return {x, y, std::min(width, scaledWidth - x), std::min(height, scaledHeight - y)};
}

math::Mat4 Camera2D::viewProjection(int viewportWidthPixels, int viewportHeightPixels,
                                    float density) const noexcept {
    if (viewportWidthPixels <= 0 || viewportHeightPixels <= 0) return math::Mat4::identity();
    density = std::max(density, 0.1f);
    const float safeZoom = std::clamp(zoom, 0.05f, 64.0f);
    const float worldWidth = static_cast<float>(viewportWidthPixels) / density / safeZoom;
    const float worldHeight = static_cast<float>(viewportHeightPixels) / density / safeZoom;
    math::Vec2 center = position + shakeOffset();
    if (pixelPerfect) {
        const float worldUnitsPerPixel = 1.0f / std::max(pixelsPerUnit, 0.01f);
        center.x = std::round(center.x / worldUnitsPerPixel) * worldUnitsPerPixel;
        center.y = std::round(center.y / worldUnitsPerPixel) * worldUnitsPerPixel;
    }

    const math::Mat4 projection = math::Mat4::orthographic(
        -worldWidth * 0.5f, worldWidth * 0.5f, -worldHeight * 0.5f,
        worldHeight * 0.5f, -1.0f, 1.0f);
    return projection * math::Mat4::translation({-center.x, -center.y, 0.0f});
}

math::Vec2 Camera2D::shakeOffset() const noexcept {
    if (shakeDuration_ <= 0.0f || shakeElapsed_ >= shakeDuration_) return {};
    const float remaining = 1.0f - shakeElapsed_ / shakeDuration_;
    const float phase = shakeElapsed_ * 47.0f;
    return {std::sin(phase) * shakeAmplitude_ * remaining,
            std::cos(phase * 1.37f) * shakeAmplitude_ * remaining};
}

}  // namespace box::renderer
