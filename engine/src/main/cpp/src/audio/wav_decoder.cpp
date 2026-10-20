// Path: engine/src/main/cpp/src/audio/wav_decoder.cpp
#include "box/audio/wav_decoder.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace box::audio {
namespace {

constexpr std::size_t kMaximumDecodedBytes = 64U * 1024U * 1024U;

void setError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

[[nodiscard]] bool matches(std::span<const std::uint8_t> bytes, std::size_t offset,
                           const char* text, std::size_t length) noexcept {
    if (offset > bytes.size() || length > bytes.size() - offset) return false;
    for (std::size_t index = 0; index < length; ++index) {
        if (bytes[offset + index] != static_cast<std::uint8_t>(text[index])) return false;
    }
    return true;
}

[[nodiscard]] bool readU16(std::span<const std::uint8_t> bytes, std::size_t offset,
                           std::uint16_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 2U) return false;
    value = static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
    return true;
}

[[nodiscard]] bool readU32(std::span<const std::uint8_t> bytes, std::size_t offset,
                           std::uint32_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 4U) return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    return true;
}

[[nodiscard]] float decodeSample(std::span<const std::uint8_t> bytes, std::size_t offset,
                                 std::uint16_t format, std::uint16_t bitsPerSample) noexcept {
    if (format == 3U) {
        std::uint32_t raw = 0;
        if (!readU32(bytes, offset, raw)) return 0.0f;
        const float sample = std::bit_cast<float>(raw);
        return std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
    }

    if (bitsPerSample == 8U) {
        return (static_cast<float>(bytes[offset]) - 128.0f) / 128.0f;
    }
    if (bitsPerSample == 16U) {
        std::uint16_t raw = 0;
        (void)readU16(bytes, offset, raw);
        std::int32_t sample = static_cast<std::int32_t>(raw);
        if ((raw & 0x8000U) != 0U) sample -= 0x10000;
        return static_cast<float>(sample) / 32768.0f;
    }
    if (bitsPerSample == 24U) {
        const std::uint32_t raw = static_cast<std::uint32_t>(bytes[offset]) |
                                  (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                                  (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U);
        std::int32_t sample = static_cast<std::int32_t>(raw);
        if ((raw & 0x00800000U) != 0U) sample -= 0x01000000;
        return static_cast<float>(sample) / 8388608.0f;
    }

    std::uint32_t raw = 0;
    (void)readU32(bytes, offset, raw);
    std::int64_t sample = static_cast<std::int64_t>(raw);
    if ((raw & 0x80000000U) != 0U) sample -= 0x100000000LL;
    return static_cast<float>(static_cast<double>(sample) / 2147483648.0);
}

}  // namespace

bool decodeWavPcm(std::span<const std::uint8_t> bytes, AudioClip& output, std::string* error) {
    if (bytes.size() < 12U || !matches(bytes, 0U, "RIFF", 4U) ||
        !matches(bytes, 8U, "WAVE", 4U)) {
        setError(error, "Audio asset is not a RIFF/WAVE file.");
        return false;
    }

    bool foundFormat = false;
    bool foundData = false;
    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t blockAlign = 0;
    std::uint32_t sampleRate = 0;
    std::size_t dataOffset = 0;
    std::size_t dataLength = 0;
    std::size_t chunkOffset = 12U;

    while (chunkOffset <= bytes.size() && bytes.size() - chunkOffset >= 8U) {
        std::uint32_t chunkLength32 = 0;
        if (!readU32(bytes, chunkOffset + 4U, chunkLength32)) {
            setError(error, "WAV chunk header is truncated.");
            return false;
        }
        const std::size_t payloadOffset = chunkOffset + 8U;
        const std::size_t chunkLength = static_cast<std::size_t>(chunkLength32);
        if (chunkLength > bytes.size() - payloadOffset) {
            setError(error, "WAV chunk extends past the end of the asset.");
            return false;
        }

        if (matches(bytes, chunkOffset, "fmt ", 4U) && !foundFormat) {
            if (chunkLength < 16U || !readU16(bytes, payloadOffset, format) ||
                !readU16(bytes, payloadOffset + 2U, channels) ||
                !readU32(bytes, payloadOffset + 4U, sampleRate) ||
                !readU16(bytes, payloadOffset + 12U, blockAlign) ||
                !readU16(bytes, payloadOffset + 14U, bitsPerSample)) {
                setError(error, "WAV format chunk is malformed.");
                return false;
            }
            foundFormat = true;
        } else if (matches(bytes, chunkOffset, "data", 4U) && !foundData) {
            dataOffset = payloadOffset;
            dataLength = chunkLength;
            foundData = true;
        }

        const std::size_t padding = chunkLength % 2U;
        if (chunkLength > std::numeric_limits<std::size_t>::max() - payloadOffset - padding) {
            setError(error, "WAV chunk length overflows the address space.");
            return false;
        }
        const std::size_t nextOffset = payloadOffset + chunkLength + padding;
        if (nextOffset > bytes.size()) {
            if (padding != 0U && payloadOffset + chunkLength == bytes.size()) break;
            setError(error, "WAV chunk padding is truncated.");
            return false;
        }
        chunkOffset = nextOffset;
    }

    if (!foundFormat || !foundData) {
        setError(error, "WAV file is missing its format or data chunk.");
        return false;
    }
    if ((format != 1U && format != 3U) || channels == 0U || channels > 2U ||
        sampleRate < 8000U || sampleRate > 192000U || dataLength == 0U) {
        setError(error, "WAV encoding, channel count, sample rate, or data length is unsupported.");
        return false;
    }
    const bool validPcmBits = bitsPerSample == 8U || bitsPerSample == 16U ||
                              bitsPerSample == 24U || bitsPerSample == 32U;
    if (!validPcmBits || (format == 3U && bitsPerSample != 32U)) {
        setError(error, "WAV bit depth is unsupported.");
        return false;
    }
    const std::uint32_t bytesPerSample = static_cast<std::uint32_t>(bitsPerSample / 8U);
    const std::uint32_t expectedBlockAlign = static_cast<std::uint32_t>(channels) * bytesPerSample;
    if (blockAlign != expectedBlockAlign || dataLength % blockAlign != 0U) {
        setError(error, "WAV block alignment does not match its PCM data.");
        return false;
    }

    const std::size_t frameCount = dataLength / blockAlign;
    const std::size_t sampleCount = frameCount * static_cast<std::size_t>(channels);
    if (sampleCount > kMaximumDecodedBytes / sizeof(float)) {
        setError(error, "Decoded WAV audio exceeds the 64 MiB safety limit.");
        return false;
    }

    AudioClip decoded{};
    decoded.sampleRate = static_cast<std::int32_t>(sampleRate);
    decoded.channelCount = static_cast<std::int32_t>(channels);
    decoded.samples.resize(sampleCount);
    for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const std::size_t sourceOffset = dataOffset + sampleIndex * bytesPerSample;
        decoded.samples[sampleIndex] =
            decodeSample(bytes, sourceOffset, format, bitsPerSample);
    }
    output = std::move(decoded);
    if (error != nullptr) error->clear();
    return true;
}

}  // namespace box::audio
