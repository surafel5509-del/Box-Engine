#include "box/renderer/particle_system.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace box::renderer {

ParticleSystem::ParticleSystem(std::size_t capacity, std::uint32_t seed)
    : capacity_(capacity), randomState_(seed == 0U ? 1U : seed) {
    particles_.reserve(capacity_);
}

void ParticleSystem::setConfig(ParticleEmitterConfig config) noexcept {
    config.emissionRate = std::max(config.emissionRate, 0.0f);
    config.lifetimeSeconds = std::max(config.lifetimeSeconds, 0.01f);
    config.speed = std::max(config.speed, 0.0f);
    config.spreadRadians = std::clamp(config.spreadRadians, 0.0f, 6.2831853f);
    config.startSize = std::max(config.startSize, 0.0f);
    config.endSize = std::max(config.endSize, 0.0f);
    config.direction = math::normalized(config.direction);
    if (math::length(config.direction) <= math::kEpsilon) config.direction = {0.0f, 1.0f};
    config_ = config;
}

void ParticleSystem::emit(std::size_t count) {
    const std::size_t available = capacity_ - particles_.size();
    const std::size_t countToSpawn = std::min(count, available);
    for (std::size_t index = 0; index < countToSpawn; ++index) spawnOne();
}

void ParticleSystem::update(double deltaSeconds) {
    const float delta = static_cast<float>(std::clamp(deltaSeconds, 0.0, 0.25));
    if (emitting_ && config_.emissionRate > 0.0f) {
        emissionAccumulator_ += static_cast<double>(config_.emissionRate) * delta;
        const std::size_t count = static_cast<std::size_t>(emissionAccumulator_);
        if (count > 0U) {
            emit(count);
            emissionAccumulator_ -= static_cast<double>(count);
        }
    }

    std::size_t index = 0;
    while (index < particles_.size()) {
        Particle& particle = particles_[index];
        particle.ageSeconds += delta;
        if (particle.ageSeconds >= particle.lifetimeSeconds) {
            particle = std::move(particles_.back());
            particles_.pop_back();
            continue;
        }
        particle.velocity = particle.velocity + config_.gravity * delta;
        particle.position = particle.position + particle.velocity * delta;
        ++index;
    }
}

void ParticleSystem::render(SpriteBatch& batch) const {
    for (const Particle& particle : particles_) {
        const float amount = std::clamp(particle.ageSeconds / particle.lifetimeSeconds, 0.0f, 1.0f);
        const float size = particle.startSize + (particle.endSize - particle.startSize) * amount;
        const math::Vec4 color{
            particle.startColor.x + (particle.endColor.x - particle.startColor.x) * amount,
            particle.startColor.y + (particle.endColor.y - particle.startColor.y) * amount,
            particle.startColor.z + (particle.endColor.z - particle.startColor.z) * amount,
            particle.startColor.w + (particle.endColor.w - particle.startColor.w) * amount,
        };
        batch.drawSolidQuad(particle.position.x - size * 0.5f,
                            particle.position.y - size * 0.5f, size, size, color);
    }
}

void ParticleSystem::clear() noexcept {
    particles_.clear();
    emissionAccumulator_ = 0.0;
}

float ParticleSystem::randomUnit() noexcept {
    randomState_ ^= randomState_ << 13U;
    randomState_ ^= randomState_ >> 17U;
    randomState_ ^= randomState_ << 5U;
    return static_cast<float>(randomState_ & 0x00FFFFFFU) / 16777216.0f;
}

void ParticleSystem::spawnOne() {
    const float directionAngle = std::atan2(config_.direction.y, config_.direction.x);
    const float angle = directionAngle + (randomUnit() - 0.5f) * config_.spreadRadians;
    const float speed = config_.speed * (0.5f + randomUnit() * 0.5f);
    const float lifetime = config_.lifetimeSeconds * (0.8f + randomUnit() * 0.4f);
    Particle particle{};
    particle.position = origin_;
    particle.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
    particle.lifetimeSeconds = lifetime;
    particle.startSize = config_.startSize;
    particle.endSize = config_.endSize;
    particle.startColor = config_.startColor;
    particle.endColor = config_.endColor;
    particles_.push_back(particle);
}

}  // namespace box::renderer
