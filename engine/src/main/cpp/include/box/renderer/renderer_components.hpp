#pragma once

#include <cstdint>
#include <string>

#include "box/ecs/ecs.hpp"
#include "box/math/math.hpp"
#include "box/renderer/camera2d.hpp"
#include "box/renderer/particle_system.hpp"
#include "box/renderer/sprite_animation.hpp"
#include "box/renderer/tilemap.hpp"

namespace box::renderer {

struct SpriteComponent {
    std::string textureAsset;
    std::string regionName;
    math::Vec2 size{1.0f, 1.0f};
    math::Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    std::int32_t layer = 0;
    bool visible = true;
};

struct SpriteAnimationComponent {
    std::string clipName;
    double elapsedSeconds = 0.0;
    float playbackRate = 1.0f;
    float blendSeconds = 0.0f;
    bool looping = true;
    bool playing = true;
};

struct SdfTextComponent {
    std::string text;
    std::string fontAsset;
    math::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float scale = 1.0f;
    std::int32_t layer = 0;
};

struct ParticleEmitterComponent {
    ParticleEmitterConfig config{};
    bool enabled = false;
};

/** Registers serializable rendering components for scene persistence and inspector tools. */
void registerRendererComponents(ecs::ComponentRegistry& registry);

}  // namespace box::renderer
