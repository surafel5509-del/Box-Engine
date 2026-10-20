// Path: engine/src/main/cpp/include/box/audio/wav_decoder.hpp
#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "box/audio/audio_clip.hpp"

namespace box::audio {

/** Decode a little-endian RIFF/WAVE PCM or IEEE-float file into interleaved float samples. */
[[nodiscard]] bool decodeWavPcm(std::span<const std::uint8_t> bytes, AudioClip& output,
                                std::string* error = nullptr);

}  // namespace box::audio
