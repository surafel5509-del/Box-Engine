#pragma once

#include <string>
#include <vector>

#include "box/renderer/sprite_batch.hpp"

namespace box::renderer {

struct AnimationFrame {
    UvRect uv{};
    float durationSeconds = 0.1f;
};

struct AnimationClip {
    std::string name;
    std::vector<AnimationFrame> frames;
    bool looping = true;
};

struct AnimationSample {
    UvRect current{};
    UvRect previous{};
    float blend = 1.0f;
    bool hasPrevious = false;
};

/** Variable-duration sprite-sheet timeline with loop control and clip cross-fading. */
class SpriteAnimationPlayer final {
public:
    [[nodiscard]] bool play(AnimationClip clip, float blendDurationSeconds = 0.0f);
    void stop() noexcept;
    void update(double deltaSeconds) noexcept;
    [[nodiscard]] AnimationSample sample() const noexcept;
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] std::size_t frameIndex() const noexcept;
    [[nodiscard]] const std::string& clipName() const noexcept { return clip_.name; }
    [[nodiscard]] double elapsedSeconds() const noexcept { return elapsedSeconds_; }

private:
    [[nodiscard]] UvRect frameAt(double timeSeconds) const noexcept;
    [[nodiscard]] double totalDuration() const noexcept;

    AnimationClip clip_;
    UvRect previousFrame_{};
    double elapsedSeconds_ = 0.0;
    float blendDuration_ = 0.0f;
    float blendElapsed_ = 0.0f;
    bool playing_ = false;
    bool hasPrevious_ = false;
};

}  // namespace box::renderer
