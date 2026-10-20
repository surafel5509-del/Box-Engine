#pragma once

#include <algorithm>
#include <cmath>

#include "box/math/math.hpp"

namespace box::renderer {

struct ViewportRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct ViewportPixels {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
};

/** Orthographic 2D camera with normalized viewport, pan/zoom, shake, and pixel snapping. */
class Camera2D final {
public:
    math::Vec2 position{};
    float zoom = 1.0f;
    ViewportRect viewport{};
    bool pixelPerfect = false;
    float pixelsPerUnit = 1.0f;

    void pan(math::Vec2 delta) noexcept { position = position + delta; }
    void setZoom(float value) noexcept { zoom = std::clamp(value, 0.05f, 64.0f); }
    void setViewport(ViewportRect value) noexcept;
    void setPixelPerfect(bool enabled, float pixelsPerUnit = 1.0f) noexcept;
    void addShake(float amplitude, float durationSeconds) noexcept;
    void update(double deltaSeconds) noexcept;

    [[nodiscard]] ViewportPixels viewportPixels(int surfaceWidth, int surfaceHeight,
                                                float renderScale = 1.0f) const noexcept;
    [[nodiscard]] math::Mat4 viewProjection(int viewportWidthPixels,
                                            int viewportHeightPixels,
                                            float density) const noexcept;

private:
    [[nodiscard]] math::Vec2 shakeOffset() const noexcept;

    float shakeAmplitude_ = 0.0f;
    float shakeDuration_ = 0.0f;
    float shakeElapsed_ = 0.0f;
};

}  // namespace box::renderer
