// Path: engine/src/main/cpp/src/audio/audio_engine.cpp
#include "box/audio/audio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <numbers>
#include <utility>

#include "box/audio/wav_decoder.hpp"
#include "box/core/log.hpp"

namespace box::audio {

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "The real-time audio command queue requires lock-free 32-bit atomics.");
static_assert(std::atomic<const AudioClip*>::is_always_lock_free,
              "Audio clip pointers must be lock-free on supported Android ABIs.");

AudioEngine::AudioEngine(assets::AssetManager& assets, std::size_t loaderWorkerCount)
    : assets_(assets), loaders_(loaderWorkerCount) {
    for (auto& clipPointer : clipPointers_) clipPointer.store(nullptr, std::memory_order_relaxed);
}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::playWavAsset(const std::string& assetPath, float gain, float pan,
                               bool looping, float playbackRate) {
    if (assetPath.empty() || shuttingDown_.load(std::memory_order_acquire)) return false;
    if (!std::isfinite(gain) || !std::isfinite(pan) || !std::isfinite(playbackRate)) return false;

    const float safeGain = std::clamp(gain, 0.0f, 2.0f);
    const float safePan = std::clamp(pan, -1.0f, 1.0f);
    const float safeRate = std::clamp(playbackRate, 0.25f, 4.0f);
    std::shared_future<assets::AssetLoadResult> assetFuture =
        assets_.request(assetPath, assets::AssetType::Audio);
    return loaders_.enqueue([this, assetPath, assetFuture = std::move(assetFuture), safeGain,
                             safePan, looping, safeRate]() {
        try {
            (void)loadAndPlayWav(assetPath, assetFuture.get(), safeGain, safePan, looping,
                                 safeRate);
        } catch (const std::exception& exception) {
            BOX_LOG_ERROR("Audio asset request failed for '%s': %s", assetPath.c_str(),
                          exception.what());
        } catch (...) {
            BOX_LOG_ERROR("Audio asset request failed for '%s' with an unknown error.",
                          assetPath.c_str());
        }
    });
}

bool AudioEngine::playTone(float frequencyHz, float durationSeconds, float gain, float pan) {
    if (shuttingDown_.load(std::memory_order_acquire) || !std::isfinite(frequencyHz) ||
        !std::isfinite(durationSeconds) || !std::isfinite(gain) || !std::isfinite(pan)) {
        return false;
    }
    Command command{};
    command.type = CommandType::PlayTone;
    command.frequencyHz = std::clamp(frequencyHz, 20.0f, 20000.0f);
    command.durationSeconds = std::clamp(durationSeconds, 0.02f, 10.0f);
    command.gain = std::clamp(gain, 0.0f, 1.0f);
    command.pan = std::clamp(pan, -1.0f, 1.0f);
    return loaders_.enqueue([this, command]() {
        if (ensureStream() && !enqueueCommand(command)) {
            BOX_LOG_WARN("Audio command queue is full; a tone request was dropped.");
        }
    });
}

bool AudioEngine::setMasterVolume(float volume) {
    if (!std::isfinite(volume) || shuttingDown_.load(std::memory_order_acquire)) return false;
    Command command{};
    command.type = CommandType::SetMasterVolume;
    command.gain = std::clamp(volume, 0.0f, 1.0f);
    return enqueueCommand(command);
}

bool AudioEngine::stopAll() {
    if (shuttingDown_.load(std::memory_order_acquire)) return false;
    Command command{};
    command.type = CommandType::StopAll;
    return enqueueCommand(command);
}

void AudioEngine::pause() noexcept {
    paused_.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (stream_ != nullptr && streamOpen_.load(std::memory_order_acquire)) {
        const oboe::Result result = stream_->requestPause();
        if (result != oboe::Result::OK) {
            BOX_LOG_WARN("Oboe output pause returned %s.", oboe::convertToText(result));
        }
    }
}

void AudioEngine::resume() noexcept {
    if (shuttingDown_.load(std::memory_order_acquire)) return;
    paused_.store(false, std::memory_order_release);
    if (streamDisconnected_.load(std::memory_order_acquire)) {
        (void)loaders_.enqueue([this]() { (void)ensureStream(); });
        return;
    }
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (stream_ != nullptr && streamOpen_.load(std::memory_order_acquire)) {
        const oboe::Result result = stream_->requestStart();
        if (result != oboe::Result::OK) {
            BOX_LOG_WARN("Oboe output resume returned %s.", oboe::convertToText(result));
            streamDisconnected_.store(true, std::memory_order_release);
            streamOpen_.store(false, std::memory_order_release);
            (void)loaders_.enqueue([this]() { (void)ensureStream(); });
        }
    }
}

void AudioEngine::shutdown() noexcept {
    if (shuttingDown_.exchange(true, std::memory_order_acq_rel)) return;
    loaders_.stop();
    closeStream();
    for (auto& clipPointer : clipPointers_) clipPointer.store(nullptr, std::memory_order_release);
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* stream, void* audioData,
                                                    std::int32_t numFrames) {
    if (stream == nullptr || audioData == nullptr || numFrames <= 0) {
        return oboe::DataCallbackResult::Continue;
    }

    const std::int32_t channelCount = stream->getChannelCount();
    if (channelCount <= 0 || channelCount > 8) return oboe::DataCallbackResult::Continue;
    const std::size_t frameCount = static_cast<std::size_t>(numFrames);
    const std::size_t channels = static_cast<std::size_t>(channelCount);
    float* output = static_cast<float*>(audioData);
    std::fill_n(output, frameCount * channels, 0.0f);
    if (paused_.load(std::memory_order_relaxed) ||
        shuttingDown_.load(std::memory_order_relaxed)) {
        return oboe::DataCallbackResult::Continue;
    }

    consumeCommands();
    const double outputSampleRate = static_cast<double>(stream->getSampleRate());
    if (outputSampleRate <= 0.0) return oboe::DataCallbackResult::Continue;
    const double secondsPerFrame = 1.0 / outputSampleRate;
    const double twoPi = 2.0 * std::numbers::pi_v<double>;

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        float mixedLeft = 0.0f;
        float mixedRight = 0.0f;
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            float sampleLeft = 0.0f;
            float sampleRight = 0.0f;

            if (voice.tone) {
                const double duration = static_cast<double>(voice.toneDurationSeconds);
                const double attack = std::min(0.005, duration * 0.25);
                const double release = std::min(0.015, duration * 0.25);
                const double remaining = duration - voice.toneElapsedSeconds;
                const double attackGain = attack > 0.0
                                              ? std::clamp(voice.toneElapsedSeconds / attack, 0.0, 1.0)
                                              : 1.0;
                const double releaseGain = release > 0.0
                                               ? std::clamp(remaining / release, 0.0, 1.0)
                                               : 1.0;
                const float envelope = static_cast<float>(std::min(attackGain, releaseGain));
                const float toneSample = static_cast<float>(std::sin(voice.tonePhaseRadians)) *
                                         envelope;
                sampleLeft = toneSample;
                sampleRight = toneSample;
                voice.tonePhaseRadians +=
                    twoPi * static_cast<double>(voice.frequencyHz) / outputSampleRate;
                if (voice.tonePhaseRadians >= twoPi) voice.tonePhaseRadians -= twoPi;
                voice.toneElapsedSeconds += secondsPerFrame;
                if (voice.toneElapsedSeconds >= duration) voice.active = false;
            } else {
                const AudioClip* clip = voice.clip;
                if (clip == nullptr || !clip->valid()) {
                    voice.active = false;
                    continue;
                }
                const std::size_t sourceFrameCount = clip->frameCount();
                const double sourceFrameCountDouble = static_cast<double>(sourceFrameCount);
                if (voice.framePosition >= sourceFrameCountDouble) {
                    if (voice.looping) {
                        voice.framePosition = std::fmod(voice.framePosition,
                                                        sourceFrameCountDouble);
                    } else {
                        voice.active = false;
                        continue;
                    }
                }

                const std::size_t firstFrame = static_cast<std::size_t>(voice.framePosition);
                const std::size_t nextFrame = firstFrame + 1U < sourceFrameCount
                                                  ? firstFrame + 1U
                                                  : (voice.looping ? 0U : firstFrame);
                const float fraction = static_cast<float>(
                    voice.framePosition - static_cast<double>(firstFrame));
                const std::size_t sourceChannels = static_cast<std::size_t>(clip->channelCount);
                const std::size_t firstOffset = firstFrame * sourceChannels;
                const std::size_t nextOffset = nextFrame * sourceChannels;
                const float firstLeft = clip->samples[firstOffset];
                const float nextLeft = clip->samples[nextOffset];
                sampleLeft = firstLeft + (nextLeft - firstLeft) * fraction;
                if (clip->channelCount == 1) {
                    sampleRight = sampleLeft;
                } else {
                    const float firstRight = clip->samples[firstOffset + 1U];
                    const float nextRight = clip->samples[nextOffset + 1U];
                    sampleRight = firstRight + (nextRight - firstRight) * fraction;
                }
                const double rateRatio = static_cast<double>(clip->sampleRate) /
                                         outputSampleRate * voice.playbackRate;
                voice.framePosition += rateRatio;
            }

            mixedLeft += sampleLeft * voice.leftGain;
            mixedRight += sampleRight * voice.rightGain;
        }

        const float left = std::clamp(mixedLeft * masterVolume_, -1.0f, 1.0f);
        const float right = std::clamp(mixedRight * masterVolume_, -1.0f, 1.0f);
        const std::size_t outputOffset = frame * channels;
        if (channelCount == 1) {
            output[outputOffset] = (left + right) * 0.5f;
        } else {
            output[outputOffset] = left;
            output[outputOffset + 1U] = right;
        }
    }
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) {
    (void)stream;
    streamOpen_.store(false, std::memory_order_release);
    streamDisconnected_.store(true, std::memory_order_release);
    BOX_LOG_WARN("Oboe output stream disconnected: %s.", oboe::convertToText(error));
    if (!paused_.load(std::memory_order_acquire) &&
        !shuttingDown_.load(std::memory_order_acquire)) {
        (void)loaders_.enqueue([this]() { (void)ensureStream(); });
    }
}

bool AudioEngine::ensureStream() {
    if (shuttingDown_.load(std::memory_order_acquire) ||
        paused_.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (shuttingDown_.load(std::memory_order_acquire) ||
        paused_.load(std::memory_order_acquire)) {
        return false;
    }

    const bool disconnected = streamDisconnected_.exchange(false, std::memory_order_acq_rel);
    if (stream_ != nullptr && !disconnected) return true;
    if (stream_ != nullptr) {
        (void)stream_->close();
        stream_.reset();
    }

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(2)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Shared)
        ->setUsage(oboe::Usage::Game)
        ->setContentType(oboe::ContentType::Music)
        ->setDataCallback(this)
        ->setErrorCallback(this);

    oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK || stream_ == nullptr) {
        BOX_LOG_ERROR("Could not open the Oboe output stream: %s.", oboe::convertToText(result));
        stream_.reset();
        return false;
    }

    streamDisconnected_.store(false, std::memory_order_release);
    result = stream_->requestStart();
    if (result != oboe::Result::OK) {
        BOX_LOG_ERROR("Could not start the Oboe output stream: %s.", oboe::convertToText(result));
        (void)stream_->close();
        stream_.reset();
        return false;
    }
    streamOpen_.store(true, std::memory_order_release);
    return true;
}

void AudioEngine::closeStream() noexcept {
    std::lock_guard<std::mutex> lock(streamMutex_);
    streamOpen_.store(false, std::memory_order_release);
    streamDisconnected_.store(false, std::memory_order_release);
    if (stream_ != nullptr) {
        (void)stream_->close();
        stream_.reset();
    }
}

bool AudioEngine::enqueueCommand(const Command& command) {
    if (shuttingDown_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> lock(commandWriteMutex_);
    const std::uint32_t writeIndex = commandWrite_.load(std::memory_order_relaxed);
    const std::uint32_t readIndex = commandRead_.load(std::memory_order_acquire);
    if (writeIndex - readIndex >= static_cast<std::uint32_t>(kCommandCapacity)) return false;
    commands_[static_cast<std::size_t>(writeIndex % kCommandCapacity)] = command;
    commandWrite_.store(writeIndex + 1U, std::memory_order_release);
    return true;
}

bool AudioEngine::loadAndPlayWav(const std::string& assetPath,
                                 const assets::AssetLoadResult& result, float gain, float pan,
                                 bool looping, float playbackRate) {
    if (!result.succeeded()) {
        BOX_LOG_ERROR("Could not load WAV asset '%s': %s", assetPath.c_str(), result.error.c_str());
        return false;
    }
    std::int32_t existingClipId = -1;
    {
        std::lock_guard<std::mutex> lock(clipMutex_);
        const auto existing = clipIds_.find(assetPath);
        if (existing != clipIds_.end()) existingClipId = existing->second;
    }
    if (existingClipId >= 0) {
        if (!ensureStream()) return false;
        Command command{};
        command.type = CommandType::PlayClip;
        command.clipId = existingClipId;
        command.gain = gain;
        command.pan = pan;
        command.playbackRate = playbackRate;
        command.looping = looping;
        return enqueueCommand(command);
    }

    AudioClip decoded{};
    std::string decodeError;
    if (!decodeWavPcm(result.asset->bytes, decoded, &decodeError)) {
        BOX_LOG_ERROR("Could not decode WAV asset '%s': %s", assetPath.c_str(),
                      decodeError.c_str());
        return false;
    }
    const std::int32_t clipId = getOrCreateClip(assetPath, std::move(decoded));
    if (clipId < 0 || !ensureStream()) return false;

    Command command{};
    command.type = CommandType::PlayClip;
    command.clipId = clipId;
    command.gain = gain;
    command.pan = pan;
    command.playbackRate = playbackRate;
    command.looping = looping;
    if (!enqueueCommand(command)) {
        BOX_LOG_WARN("Audio command queue is full; WAV asset '%s' was not played.",
                     assetPath.c_str());
        return false;
    }
    return true;
}

std::int32_t AudioEngine::getOrCreateClip(const std::string& assetPath, AudioClip decoded) {
    if (!decoded.valid()) return -1;
    std::lock_guard<std::mutex> lock(clipMutex_);
    const auto existing = clipIds_.find(assetPath);
    if (existing != clipIds_.end()) return existing->second;
    if (decoded.samples.size() > kMaximumPcmCacheBytes / sizeof(float)) return -1;
    const std::size_t clipBytes = decoded.samples.size() * sizeof(float);
    if (clipBytes > kMaximumPcmCacheBytes - pcmCacheBytes_) {
        BOX_LOG_ERROR("Audio PCM cache limit (%zu MiB) is exhausted.",
                      kMaximumPcmCacheBytes / (1024U * 1024U));
        return -1;
    }

    for (std::size_t index = 0; index < clipOwners_.size(); ++index) {
        if (clipOwners_[index] != nullptr) continue;
        clipOwners_[index] = std::make_unique<AudioClip>(std::move(decoded));
        pcmCacheBytes_ += clipBytes;
        clipPointers_[index].store(clipOwners_[index].get(), std::memory_order_release);
        const auto clipId = static_cast<std::int32_t>(index);
        clipIds_.emplace(assetPath, clipId);
        return clipId;
    }
    BOX_LOG_ERROR("Audio clip capacity (%zu) is exhausted.", kMaximumClips);
    return -1;
}

void AudioEngine::consumeCommands() noexcept {
    std::uint32_t readIndex = commandRead_.load(std::memory_order_relaxed);
    const std::uint32_t writeIndex = commandWrite_.load(std::memory_order_acquire);
    while (readIndex != writeIndex) {
        const Command command = commands_[static_cast<std::size_t>(readIndex % kCommandCapacity)];
        switch (command.type) {
            case CommandType::PlayClip: {
                if (command.clipId < 0 ||
                    static_cast<std::size_t>(command.clipId) >= clipPointers_.size()) {
                    break;
                }
                const AudioClip* clip =
                    clipPointers_[static_cast<std::size_t>(command.clipId)].load(
                        std::memory_order_acquire);
                if (clip == nullptr || !clip->valid()) break;
                Voice& voice = acquireVoice();
                voice.clip = clip;
                voice.active = true;
                voice.looping = command.looping;
                voice.playbackRate = command.playbackRate;
                configureVoiceLevels(voice, command.gain, command.pan);
                break;
            }
            case CommandType::PlayTone: {
                Voice& voice = acquireVoice();
                voice.active = true;
                voice.tone = true;
                voice.frequencyHz = command.frequencyHz;
                voice.toneDurationSeconds = command.durationSeconds;
                configureVoiceLevels(voice, command.gain, command.pan);
                break;
            }
            case CommandType::SetMasterVolume:
                masterVolume_ = command.gain;
                break;
            case CommandType::StopAll:
                for (Voice& voice : voices_) voice.active = false;
                break;
        }
        ++readIndex;
    }
    commandRead_.store(readIndex, std::memory_order_release);
}

AudioEngine::Voice& AudioEngine::acquireVoice() noexcept {
    for (Voice& voice : voices_) {
        if (!voice.active) {
            voice = Voice{};
            return voice;
        }
    }
    Voice& stolen = voices_[voiceStealIndex_ % voices_.size()];
    voiceStealIndex_ = (voiceStealIndex_ + 1U) % voices_.size();
    stolen = Voice{};
    return stolen;
}

void AudioEngine::configureVoiceLevels(Voice& voice, float gain, float pan) noexcept {
    voice.gain = std::clamp(gain, 0.0f, 2.0f);
    const float safePan = std::clamp(pan, -1.0f, 1.0f);
    voice.leftGain = voice.gain * (safePan > 0.0f ? 1.0f - safePan : 1.0f);
    voice.rightGain = voice.gain * (safePan < 0.0f ? 1.0f + safePan : 1.0f);
}

}  // namespace box::audio
