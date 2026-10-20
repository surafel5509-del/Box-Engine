#include "box/renderer/sprite_animation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace box::renderer {

bool SpriteAnimationPlayer::play(AnimationClip clip, float blendDurationSeconds) {
    if (clip.frames.empty()) return false;
    previousFrame_ = playing_ ? frameAt(elapsedSeconds_) : UvRect{};
    hasPrevious_ = playing_ && blendDurationSeconds > 0.0f;
    clip_ = std::move(clip);
    elapsedSeconds_ = 0.0;
    blendDuration_ = std::max(blendDurationSeconds, 0.0f);
    blendElapsed_ = 0.0f;
    playing_ = true;
    return true;
}

void SpriteAnimationPlayer::stop() noexcept {
    playing_ = false;
    hasPrevious_ = false;
    blendDuration_ = 0.0f;
    blendElapsed_ = 0.0f;
}

void SpriteAnimationPlayer::update(double deltaSeconds) noexcept {
    if (!playing_ || clip_.frames.empty()) return;
    const double safeDelta = std::max(deltaSeconds, 0.0);
    elapsedSeconds_ += safeDelta;
    const double duration = totalDuration();
    if (clip_.looping && duration > 0.0 && elapsedSeconds_ >= duration) {
        elapsedSeconds_ = std::fmod(elapsedSeconds_, duration);
    } else if (!clip_.looping && elapsedSeconds_ >= duration) {
        elapsedSeconds_ = duration;
        playing_ = false;
    }
    blendElapsed_ = std::min(blendDuration_, blendElapsed_ + static_cast<float>(safeDelta));
    if (blendElapsed_ >= blendDuration_) hasPrevious_ = false;
}

AnimationSample SpriteAnimationPlayer::sample() const noexcept {
    AnimationSample result{};
    result.current = frameAt(elapsedSeconds_);
    if (hasPrevious_ && blendDuration_ > 0.0f) {
        result.previous = previousFrame_;
        result.blend = std::clamp(blendElapsed_ / blendDuration_, 0.0f, 1.0f);
        result.hasPrevious = result.blend < 1.0f;
    }
    return result;
}

std::size_t SpriteAnimationPlayer::frameIndex() const noexcept {
    if (clip_.frames.empty()) return 0U;
    double time = elapsedSeconds_;
    const double duration = totalDuration();
    if (clip_.looping && duration > 0.0) time = std::fmod(time, duration);
    for (std::size_t index = 0; index < clip_.frames.size(); ++index) {
        const double frameDuration = std::max(clip_.frames[index].durationSeconds, 0.001f);
        if (time < frameDuration || index + 1U == clip_.frames.size()) return index;
        time -= frameDuration;
    }
    return clip_.frames.size() - 1U;
}

UvRect SpriteAnimationPlayer::frameAt(double timeSeconds) const noexcept {
    if (clip_.frames.empty()) return {};
    double time = std::max(timeSeconds, 0.0);
    const double duration = totalDuration();
    if (clip_.looping && duration > 0.0) time = std::fmod(time, duration);
    for (std::size_t index = 0; index < clip_.frames.size(); ++index) {
        const double frameDuration = std::max(clip_.frames[index].durationSeconds, 0.001f);
        if (time < frameDuration || index + 1U == clip_.frames.size()) {
            return clip_.frames[index].uv;
        }
        time -= frameDuration;
    }
    return clip_.frames.back().uv;
}

double SpriteAnimationPlayer::totalDuration() const noexcept {
    double total = 0.0;
    for (const AnimationFrame& frame : clip_.frames) {
        total += std::max(frame.durationSeconds, 0.001f);
    }
    return total;
}

}  // namespace box::renderer
