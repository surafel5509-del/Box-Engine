#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "box/math/math.hpp"
#include "box/renderer/sprite_batch.hpp"

namespace box::renderer {

struct ParticleEmitterConfig {
    float emissionRate = 0.0f;
    float lifetimeSeconds = 1.0f;
    float speed = 40.0f;
    float spreadRadians = 0.7f;
    math::Vec2 direction{0.0f, 1.0f};
    math::Vec2 gravity{};
    float startSize = 8.0f;
    float endSize = 1.0f;
    math::Vec4 startColor{1.0f, 0.7f, 0.25f, 1.0f};
    math::Vec4 endColor{0.8f, 0.2f, 0.08f, 0.0f};
};

struct Particle {
    math::Vec2 position{};
    math::Vec2 velocity{};
    float ageSeconds = 0.0f;
    float lifetimeSeconds = 1.0f;
    float startSize = 1.0f;
    float endSize = 1.0f;
    math::Vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
    math::Vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
};

/** Bounded CPU particle simulation with deterministic emission and sprite-batch rendering. */
class ParticleSystem final {
public:
    explicit ParticleSystem(std::size_t capacity = 1024U, std::uint32_t seed = 0xC001D00DU);

    void setConfig(ParticleEmitterConfig config) noexcept;
    [[nodiscard]] const ParticleEmitterConfig& config() const noexcept { return config_; }
    void setEmitting(bool enabled) noexcept { emitting_ = enabled; }
    void setOrigin(math::Vec2 origin) noexcept { origin_ = origin; }
    void emit(std::size_t count);
    void update(double deltaSeconds);
    void render(SpriteBatch& batch) const;
    void clear() noexcept;

    [[nodiscard]] std::size_t particleCount() const noexcept { return particles_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    [[nodiscard]] float randomUnit() noexcept;
    void spawnOne();

    ParticleEmitterConfig config_{};
    std::vector<Particle> particles_;
    std::size_t capacity_ = 0;
    math::Vec2 origin_{};
    std::uint32_t randomState_ = 1U;
    double emissionAccumulator_ = 0.0;
    bool emitting_ = false;
};

}  // namespace box::renderer
