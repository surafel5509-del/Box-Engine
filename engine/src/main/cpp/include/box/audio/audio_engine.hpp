// Path: engine/src/main/cpp/include/box/audio/audio_engine.hpp
#pragma once

#include <cstring>

#include <oboe/Oboe.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "box/assets/asset_manager.hpp"
#include "box/audio/audio_clip.hpp"
#include "box/ecs/job_system.hpp"

namespace box::audio {

/** Oboe low-latency output mixer. All file access and WAV decoding run off the audio callback. */
class AudioEngine final : public oboe::AudioStreamDataCallback,
                          public oboe::AudioStreamErrorCallback {
public:
    explicit AudioEngine(assets::AssetManager& assets, std::size_t loaderWorkerCount = 1U);
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /** Queue an asynchronous packaged WAV load and play request. */
    [[nodiscard]] bool playWavAsset(const std::string& assetPath, float gain = 1.0f,
                                    float pan = 0.0f, bool looping = false,
                                    float playbackRate = 1.0f);
    /** Queue a short sine tone, useful for diagnostics and simple sound effects. */
    [[nodiscard]] bool playTone(float frequencyHz = 440.0f, float durationSeconds = 0.2f,
                                float gain = 0.25f, float pan = 0.0f);
    [[nodiscard]] bool setMasterVolume(float volume);
    [[nodiscard]] bool stopAll();
    void pause() noexcept;
    void resume() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool streamOpen() const noexcept { return streamOpen_.load(); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                          std::int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    enum class CommandType : std::uint8_t { PlayClip, PlayTone, SetMasterVolume, StopAll };

    struct Command {
        CommandType type = CommandType::StopAll;
        std::int32_t clipId = -1;
        float gain = 1.0f;
        float pan = 0.0f;
        float playbackRate = 1.0f;
        float frequencyHz = 440.0f;
        float durationSeconds = 0.2f;
        bool looping = false;
    };

    struct Voice {
        const AudioClip* clip = nullptr;
        double framePosition = 0.0;
        double toneElapsedSeconds = 0.0;
        double tonePhaseRadians = 0.0;
        float gain = 0.0f;
        float leftGain = 0.0f;
        float rightGain = 0.0f;
        float playbackRate = 1.0f;
        float frequencyHz = 440.0f;
        float toneDurationSeconds = 0.0f;
        bool active = false;
        bool tone = false;
        bool looping = false;
    };

    [[nodiscard]] bool ensureStream();
    void closeStream() noexcept;
    [[nodiscard]] bool enqueueCommand(const Command& command);
    [[nodiscard]] bool loadAndPlayWav(const std::string& assetPath,
                                      const assets::AssetLoadResult& result,
                                      float gain, float pan, bool looping,
                                      float playbackRate);
    [[nodiscard]] std::int32_t getOrCreateClip(const std::string& assetPath,
                                               AudioClip decoded);
    void consumeCommands() noexcept;
    [[nodiscard]] Voice& acquireVoice() noexcept;
    void configureVoiceLevels(Voice& voice, float gain, float pan) noexcept;

    static constexpr std::size_t kMaximumClips = 128U;
    static constexpr std::size_t kMaximumVoices = 32U;
    static constexpr std::size_t kCommandCapacity = 128U;
    static constexpr std::size_t kMaximumPcmCacheBytes = 128U * 1024U * 1024U;

    assets::AssetManager& assets_;
    ecs::JobSystem loaders_;
    std::array<std::unique_ptr<AudioClip>, kMaximumClips> clipOwners_{};
    std::array<std::atomic<const AudioClip*>, kMaximumClips> clipPointers_{};
    std::unordered_map<std::string, std::int32_t> clipIds_;
    std::size_t pcmCacheBytes_ = 0U;
    std::mutex clipMutex_;
    std::array<Command, kCommandCapacity> commands_{};
    std::atomic<std::uint32_t> commandWrite_{0U};
    std::atomic<std::uint32_t> commandRead_{0U};
    std::mutex commandWriteMutex_;
    std::array<Voice, kMaximumVoices> voices_{};
    std::size_t voiceStealIndex_ = 0U;
    float masterVolume_ = 1.0f;
    std::mutex streamMutex_;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::atomic<bool> streamOpen_{false};
    std::atomic<bool> streamDisconnected_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> shuttingDown_{false};
};

}  // namespace box::audio
