// Path: engine/src/main/cpp/include/box/core/frame_profiler.hpp
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

namespace box::core {

enum class ProfileSubsystem : std::uint8_t {
    FixedUpdate,
    VariableUpdate,
    SceneGraph,
    Renderer,
    Ui,
    SwapBuffers,
    Count,
};

constexpr std::size_t kProfileSubsystemCount =
    static_cast<std::size_t>(ProfileSubsystem::Count);

struct FrameProfileSnapshot {
    std::uint64_t frameIndex = 0U;
    double frameMilliseconds = 0.0;
    double framesPerSecond = 0.0;
    double smoothedFrameMilliseconds = 0.0;
    std::array<double, kProfileSubsystemCount> subsystemMilliseconds{};
};

/** Low-overhead per-frame CPU timings; scoped samples may be recorded from worker threads. */
class FrameProfiler final {
public:
    using Clock = std::chrono::steady_clock;

    class Scope final {
    public:
        Scope() = default;
        Scope(FrameProfiler& profiler, ProfileSubsystem subsystem) noexcept
            : profiler_(&profiler), subsystem_(subsystem), start_(Clock::now()) {}
        ~Scope() { stop(); }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        Scope(Scope&& other) noexcept
            : profiler_(std::exchange(other.profiler_, nullptr)),
              subsystem_(other.subsystem_), start_(other.start_) {}

        Scope& operator=(Scope&& other) noexcept {
            if (this != &other) {
                stop();
                profiler_ = std::exchange(other.profiler_, nullptr);
                subsystem_ = other.subsystem_;
                start_ = other.start_;
            }
            return *this;
        }

        void stop() noexcept {
            if (profiler_ == nullptr) return;
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - start_).count();
            profiler_->record(subsystem_, elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0U);
            profiler_ = nullptr;
        }

    private:
        FrameProfiler* profiler_ = nullptr;
        ProfileSubsystem subsystem_ = ProfileSubsystem::Renderer;
        Clock::time_point start_{};
    };

    FrameProfiler() = default;
    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;

    void beginFrame() noexcept {
        frameStart_ = Clock::now();
        for (auto& elapsed : elapsedNanoseconds_) elapsed.store(0U, std::memory_order_relaxed);
    }

    [[nodiscard]] Scope profile(ProfileSubsystem subsystem) noexcept {
        return Scope(*this, subsystem);
    }

    [[nodiscard]] FrameProfileSnapshot endFrame(double minimumFrameMilliseconds = 0.0) noexcept {
        const auto frameEnd = Clock::now();
        const double processingMilliseconds =
            std::chrono::duration<double, std::milli>(frameEnd - frameStart_).count();
        const double frameMilliseconds = std::max(processingMilliseconds,
                                                   minimumFrameMilliseconds);
        FrameProfileSnapshot result{};
        result.frameIndex = ++frameIndex_;
        result.frameMilliseconds = frameMilliseconds;
        result.framesPerSecond = frameMilliseconds > 0.0 ? 1000.0 / frameMilliseconds : 0.0;
        smoothedFrameMilliseconds_ = smoothedFrameMilliseconds_ == 0.0
                                         ? frameMilliseconds
                                         : smoothedFrameMilliseconds_ * 0.9 + frameMilliseconds * 0.1;
        result.smoothedFrameMilliseconds = smoothedFrameMilliseconds_;
        for (std::size_t index = 0; index < elapsedNanoseconds_.size(); ++index) {
            result.subsystemMilliseconds[index] =
                static_cast<double>(elapsedNanoseconds_[index].load(std::memory_order_relaxed)) /
                1000000.0;
        }
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            latest_ = result;
        }
        return result;
    }

    [[nodiscard]] FrameProfileSnapshot snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        return latest_;
    }

private:
    void record(ProfileSubsystem subsystem, std::uint64_t elapsedNanoseconds) noexcept {
        const std::size_t index = static_cast<std::size_t>(subsystem);
        if (index < elapsedNanoseconds_.size()) {
            elapsedNanoseconds_[index].fetch_add(elapsedNanoseconds, std::memory_order_relaxed);
        }
    }

    std::array<std::atomic<std::uint64_t>, kProfileSubsystemCount> elapsedNanoseconds_{};
    Clock::time_point frameStart_{};
    std::uint64_t frameIndex_ = 0U;
    double smoothedFrameMilliseconds_ = 0.0;
    mutable std::mutex snapshotMutex_;
    FrameProfileSnapshot latest_{};
};

}  // namespace box::core
