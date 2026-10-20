// Path: engine/src/main/cpp/include/box/audio/audio_clip.hpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace box::audio {

/** Immutable interleaved mono/stereo float PCM clip used by the real-time mixer. */
struct AudioClip {
    std::int32_t sampleRate = 0;
    std::int32_t channelCount = 0;
    std::vector<float> samples;

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return channelCount <= 0
                   ? 0U
                   : samples.size() / static_cast<std::size_t>(channelCount);
    }

    [[nodiscard]] bool valid() const noexcept {
        return sampleRate > 0 && (channelCount == 1 || channelCount == 2) &&
               !samples.empty() &&
               samples.size() % static_cast<std::size_t>(channelCount) == 0U;
    }
};

}  // namespace box::audio
