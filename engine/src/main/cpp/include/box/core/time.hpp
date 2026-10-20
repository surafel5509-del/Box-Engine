#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

namespace box::core {

/** Fixed-step accumulator with a clamped variable delta for mobile frame hitches. */
class TimeSystem final {
public:
    using Clock = std::chrono::steady_clock;

    explicit TimeSystem(double fixedStepSeconds = 1.0 / 60.0,
                        double maximumFrameDeltaSeconds = 0.25,
                        std::size_t maximumCatchUpSteps = 5) noexcept
        : fixedStepSeconds_(fixedStepSeconds),
          maximumFrameDeltaSeconds_(maximumFrameDeltaSeconds),
          maximumCatchUpSteps_(maximumCatchUpSteps) {}

    void reset(Clock::time_point now = Clock::now()) noexcept {
        lastFrame_ = now;
        started_ = true;
        accumulatorSeconds_ = 0.0;
        elapsedSeconds_ = 0.0;
        interpolationAlpha_ = 0.0;
    }

    template <typename FixedUpdate, typename VariableUpdate>
    double tick(Clock::time_point now, FixedUpdate&& fixedUpdate,
                VariableUpdate&& variableUpdate) {
        const double delta = tick(now, static_cast<FixedUpdate&&>(fixedUpdate));
        variableUpdate(delta);
        return delta;
    }

    template <typename FixedUpdate>
    double tick(Clock::time_point now, FixedUpdate&& fixedUpdate) {
        if (!started_) {
            reset(now);
            return 0.0;
        }

        const double rawDelta = std::chrono::duration<double>(now - lastFrame_).count();
        lastFrame_ = now;
        const double delta = std::clamp(rawDelta, 0.0, maximumFrameDeltaSeconds_);
        elapsedSeconds_ += delta;
        accumulatorSeconds_ += delta;

        std::size_t stepCount = 0;
        while (accumulatorSeconds_ >= fixedStepSeconds_ && stepCount < maximumCatchUpSteps_) {
            fixedUpdate(fixedStepSeconds_);
            accumulatorSeconds_ -= fixedStepSeconds_;
            ++stepCount;
        }

        // Drop excessive backlog rather than running unbounded catch-up work after a pause.
        if (stepCount == maximumCatchUpSteps_ && accumulatorSeconds_ >= fixedStepSeconds_) {
            accumulatorSeconds_ = std::fmod(accumulatorSeconds_, fixedStepSeconds_);
        }
        interpolationAlpha_ = accumulatorSeconds_ / fixedStepSeconds_;
        return delta;
    }

    [[nodiscard]] double elapsedSeconds() const noexcept { return elapsedSeconds_; }
    [[nodiscard]] double fixedStepSeconds() const noexcept { return fixedStepSeconds_; }
    [[nodiscard]] double interpolationAlpha() const noexcept { return interpolationAlpha_; }

private:
    Clock::time_point lastFrame_{};
    double fixedStepSeconds_;
    double maximumFrameDeltaSeconds_;
    std::size_t maximumCatchUpSteps_;
    double accumulatorSeconds_ = 0.0;
    double elapsedSeconds_ = 0.0;
    double interpolationAlpha_ = 0.0;
    bool started_ = false;
};

}  // namespace box::core
