// Path: engine/src/main/cpp/src/engine_runtime.cpp
#include "box/engine_runtime.hpp"

#include <android/native_window.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>
#include <unistd.h>

#include "box/core/frame_profiler.hpp"
#include "box/core/log.hpp"
#include "box/core/trace_section.hpp"
#include "box/core/time.hpp"
#include "box/renderer/egl_context.hpp"
#include "box/renderer/renderer.hpp"
#include "box/renderer/renderer_components.hpp"
#include "box/scene/scene_serializer.hpp"

namespace box {
namespace {

constexpr auto kTargetFrameDuration = std::chrono::duration<double>(1.0 / 60.0);
constexpr unsigned int kRetryDelayMilliseconds = 250U;

[[nodiscard]] std::uint64_t sampleResidentMemoryBytes() noexcept {
    std::FILE* statistics = std::fopen("/proc/self/statm", "r");
    if (statistics == nullptr) return 0U;
    unsigned long long totalPages = 0U;
    unsigned long long residentPages = 0U;
    const int scanned = std::fscanf(statistics, "%llu %llu", &totalPages, &residentPages);
    (void)std::fclose(statistics);
    (void)totalPages;
    if (scanned != 2) return 0U;
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return 0U;
    return static_cast<std::uint64_t>(residentPages) * static_cast<std::uint64_t>(pageSize);
}

}  // namespace

EngineRuntime::EngineRuntime(AAssetManager* assets, float density,
                             std::string shaderOverrideDirectory)
    : assets_(assets),
      shaderOverrideDirectory_(std::move(shaderOverrideDirectory)),
      assetManager_(assets, 2U),
      audioEngine_(assetManager_, 1U),
      scheduler_(2U),
      density_(std::max(density, 0.1f)) {
    audioEngine_.pause();
    scene::registerCoreComponents(world_.componentRegistry());
    renderer::registerRendererComponents(world_.componentRegistry());
}

EngineRuntime::~EngineRuntime() { stop(); }

void EngineRuntime::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) return;
    stopRequested_ = false;
    started_ = true;
    renderThread_ = std::thread(&EngineRuntime::run, this);
    BOX_LOG_INFO("Native render thread started.");
}

void EngineRuntime::stop() {
    ANativeWindow* unconsumedWindow = nullptr;
    bool threadWasStarted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threadWasStarted = started_;
        if (threadWasStarted) {
            stopRequested_ = true;
            unconsumedWindow = pendingWindow_;
            pendingWindow_ = nullptr;
            surfaceUpdatePending_ = false;
        }
    }
    if (unconsumedWindow != nullptr) ANativeWindow_release(unconsumedWindow);
    if (threadWasStarted) {
        condition_.notify_all();
        if (renderThread_.joinable()) renderThread_.join();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
        }
        BOX_LOG_INFO("Native render thread stopped.");
    }
    scheduler_.jobs().waitIdle();
    audioEngine_.shutdown();
    assetManager_.shutdown();
}

void EngineRuntime::setSurface(ANativeWindow* ownedWindow, bool waitUntilApplied) {
    ANativeWindow* replacedWindow = nullptr;
    std::uint64_t requestedGeneration = 0;
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopRequested_ || !started_) {
            replacedWindow = ownedWindow;
        } else {
            replacedWindow = pendingWindow_;
            pendingWindow_ = ownedWindow;
            surfaceUpdatePending_ = true;
            requestedGeneration = ++requestedSurfaceGeneration_;
            accepted = true;
        }
    }
    if (replacedWindow != nullptr) ANativeWindow_release(replacedWindow);
    condition_.notify_all();

    if (waitUntilApplied && accepted) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this, requestedGeneration]() {
            return processedSurfaceGeneration_ >= requestedGeneration || stopRequested_ || !started_;
        });
    }
}

void EngineRuntime::setPaused(bool paused) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed = paused_ != paused;
        paused_ = paused;
        if (changed && paused) {
            pendingUiEvents_.clear();
            uiTextInputFocusRequested_ = false;
            pendingUiTextInputFocusValue_.reset();
            renderer::UiFrameInput cancel{};
            cancel.pointerAction = renderer::UiPointerAction::Cancel;
            pendingUiEvents_.push_back(std::move(cancel));
        }
    }
    if (changed) {
        if (paused) audioEngine_.pause();
        else audioEngine_.resume();
    }
    condition_.notify_all();
}

void EngineRuntime::setScreenInfo(int width, int height, float density) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screenWidth_ = std::max(width, 1);
        screenHeight_ = std::max(height, 1);
        density_ = std::max(density, 0.1f);
    }
    condition_.notify_all();
}

void EngineRuntime::setRenderScale(float scale) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        renderScale_ = std::clamp(scale, 0.5f, 1.0f);
    }
    condition_.notify_all();
}

void EngineRuntime::setAdaptiveResolution(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        adaptiveResolutionEnabled_ = enabled;
    }
    condition_.notify_all();
}

void EngineRuntime::requestShaderReload() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shaderReloadRequested_ = true;
    }
    condition_.notify_all();
}

void EngineRuntime::setWindowInsetsPixels(int left, int top, int right, int bottom) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        safeInsetsPixels_ = {
            static_cast<float>(std::max(left, 0)),
            static_cast<float>(std::max(top, 0)),
            static_cast<float>(std::max(right, 0)),
            static_cast<float>(std::max(bottom, 0)),
        };
    }
    condition_.notify_all();
}

void EngineRuntime::enqueueUiPointer(renderer::UiPointerAction action,
                                     std::int32_t pointerId, float xPixels, float yPixels) {
    if (action == renderer::UiPointerAction::None || !std::isfinite(xPixels) ||
        !std::isfinite(yPixels)) {
        return;
    }
    renderer::UiFrameInput input{};
    input.pointerAction = action;
    input.pointerId = pointerId;
    input.pointerXpx = xPixels;
    input.pointerYpx = yPixels;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (action == renderer::UiPointerAction::Move && !pendingUiEvents_.empty() &&
            pendingUiEvents_.back().pointerAction == renderer::UiPointerAction::Move &&
            pendingUiEvents_.back().pointerId == pointerId) {
            pendingUiEvents_.back() = std::move(input);
        } else {
            constexpr std::size_t kMaximumQueuedUiEvents = 64U;
            if (pendingUiEvents_.size() >= kMaximumQueuedUiEvents) {
                const auto moveEvent = std::find_if(
                    pendingUiEvents_.begin(), pendingUiEvents_.end(), [](const auto& pending) {
                        return pending.pointerAction == renderer::UiPointerAction::Move;
                    });
                if (moveEvent != pendingUiEvents_.end()) pendingUiEvents_.erase(moveEvent);
                else pendingUiEvents_.pop_front();
            }
            pendingUiEvents_.push_back(std::move(input));
        }
    }
    condition_.notify_all();
}

std::optional<std::string> EngineRuntime::consumeUiTextInputFocusRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!uiTextInputFocusRequested_) return std::nullopt;
    uiTextInputFocusRequested_ = false;
    std::optional<std::string> value = std::move(pendingUiTextInputFocusValue_);
    pendingUiTextInputFocusValue_.reset();
    if (!value.has_value()) value = std::string{};
    return value;
}

void EngineRuntime::enqueueUiText(std::string textUtf8, bool replaceText,
                                  bool backspace, bool submit) {
    if (textUtf8.empty() && !replaceText && !backspace && !submit) return;
    renderer::UiFrameInput input{};
    input.textUtf8 = std::move(textUtf8);
    input.replaceText = replaceText;
    input.backspace = backspace;
    input.submit = submit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        constexpr std::size_t kMaximumQueuedUiEvents = 64U;
        if (pendingUiEvents_.size() >= kMaximumQueuedUiEvents) pendingUiEvents_.pop_front();
        pendingUiEvents_.push_back(std::move(input));
    }
    condition_.notify_all();
}

void EngineRuntime::run() {
    renderer::EglContext egl;
    renderer::Renderer gameRenderer(assets_, shaderOverrideDirectory_);
    ANativeWindow* currentWindow = nullptr;
    bool rendererReady = false;
    bool eglReady = false;
    core::TimeSystem timeSystem;
    core::FrameProfiler frameProfiler;
    timeSystem.reset();
    double previousFrameMilliseconds = 16.67;
    std::uint64_t residentMemoryBytes = 0U;
    auto nextMemorySample = core::TimeSystem::Clock::time_point{};
    auto nextFrameDeadline = core::TimeSystem::Clock::now();

    const auto tearDownGraphics = [&]() {
        if (rendererReady) {
            if (eglReady) gameRenderer.shutdown();
            else gameRenderer.abandon();
            rendererReady = false;
        }
        if (eglReady) {
            egl.shutdown();
            eglReady = false;
        }
    };

    bool exitRequested = false;
    while (!exitRequested) {
        ANativeWindow* replacementWindow = nullptr;
        bool applySurfaceUpdate = false;
        bool pausedSnapshot = true;
        bool reloadShaders = false;
        std::uint64_t replacementGeneration = 0;
        int screenWidth = 0;
        int screenHeight = 0;
        float density = 1.0f;
        float renderScale = 1.0f;
        bool adaptiveResolutionEnabled = true;
        renderer::UiFrameInput uiInput{};
        renderer::UiInsets safeInsetsPixels{};

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopRequested_) {
                exitRequested = true;
            } else if (surfaceUpdatePending_) {
                replacementWindow = pendingWindow_;
                pendingWindow_ = nullptr;
                surfaceUpdatePending_ = false;
                replacementGeneration = requestedSurfaceGeneration_;
                applySurfaceUpdate = true;
            }

            pausedSnapshot = paused_;
            screenWidth = screenWidth_;
            screenHeight = screenHeight_;
            density = density_;
            renderScale = renderScale_;
            adaptiveResolutionEnabled = adaptiveResolutionEnabled_;
            reloadShaders = shaderReloadRequested_;
            shaderReloadRequested_ = false;
            safeInsetsPixels = safeInsetsPixels_;
            if (!pendingUiEvents_.empty()) {
                uiInput = std::move(pendingUiEvents_.front());
                pendingUiEvents_.pop_front();
            }
        }
        if (exitRequested) break;

        if (applySurfaceUpdate) {
            tearDownGraphics();
            if (currentWindow != nullptr) ANativeWindow_release(currentWindow);
            currentWindow = replacementWindow;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                processedSurfaceGeneration_ =
                    std::max(processedSurfaceGeneration_, replacementGeneration);
            }
            condition_.notify_all();
            BOX_LOG_INFO("Android surface updated (%s).", currentWindow != nullptr ? "available" : "lost");
        }

        if (pausedSnapshot || currentWindow == nullptr) {
            tearDownGraphics();
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this, &currentWindow]() {
                return stopRequested_ || surfaceUpdatePending_ ||
                       (!paused_ && currentWindow != nullptr);
            });
            continue;
        }

        const int nativeWidth = ANativeWindow_getWidth(currentWindow);
        const int nativeHeight = ANativeWindow_getHeight(currentWindow);
        if (nativeWidth > 0) screenWidth = nativeWidth;
        if (nativeHeight > 0) screenHeight = nativeHeight;
        screenWidth = std::max(screenWidth, 1);
        screenHeight = std::max(screenHeight, 1);
        density = std::max(density, 0.1f);

        if (!eglReady) {
            if (!egl.initialize(currentWindow)) {
                waitForLifecycleChange(kRetryDelayMilliseconds);
                continue;
            }
            eglReady = true;
            if (!gameRenderer.initialize()) {
                gameRenderer.shutdown();
                rendererReady = false;
                egl.shutdown();
                eglReady = false;
                waitForLifecycleChange(kRetryDelayMilliseconds);
                continue;
            }
            rendererReady = true;
            timeSystem.reset();
            nextFrameDeadline = core::TimeSystem::Clock::now();
        }

        gameRenderer.setRenderScale(renderScale);
        gameRenderer.setAdaptiveResolutionEnabled(adaptiveResolutionEnabled);
        if (reloadShaders) (void)gameRenderer.reloadShaders();

        const auto memorySampleTime = core::TimeSystem::Clock::now();
        if (memorySampleTime >= nextMemorySample) {
            residentMemoryBytes = sampleResidentMemoryBytes();
            nextMemorySample = memorySampleTime + std::chrono::seconds(1);
        }
        const auto frameStart = core::TimeSystem::Clock::now();
        frameProfiler.beginFrame();
        const double variableDelta = timeSystem.tick(
            frameStart,
            [&frameProfiler](double /* fixedDelta */) {
                [[maybe_unused]] core::TraceSection trace("BoxEngine.FixedUpdate");
                [[maybe_unused]] auto profile =
                    frameProfiler.profile(core::ProfileSubsystem::FixedUpdate);
                // The phase-1 runtime owns the fixed update slot; gameplay systems are added later.
            },
            [this, &frameProfiler](double frameDelta) {
                [[maybe_unused]] core::TraceSection updateTrace("BoxEngine.VariableUpdate");
                [[maybe_unused]] auto updateProfile =
                    frameProfiler.profile(core::ProfileSubsystem::VariableUpdate);
                scheduler_.run(frameDelta);
                {
                    [[maybe_unused]] core::TraceSection sceneTrace("BoxEngine.SceneGraph");
                    [[maybe_unused]] auto sceneProfile =
                        frameProfiler.profile(core::ProfileSubsystem::SceneGraph);
                    sceneGraph_.updateWorldTransforms();
                }
            });
        renderer::DebugOverlayStats debugStats{};
        debugStats.frame = frameProfiler.snapshot();
        debugStats.profiler = &frameProfiler;
        debugStats.entityCount = world_.entityCount();
        debugStats.residentMemoryBytes = residentMemoryBytes;
        {
            [[maybe_unused]] core::TraceSection renderTrace("BoxEngine.Renderer");
            [[maybe_unused]] auto renderProfile =
                frameProfiler.profile(core::ProfileSubsystem::Renderer);
            gameRenderer.render(screenWidth, screenHeight, density, timeSystem.elapsedSeconds(),
                                variableDelta, previousFrameMilliseconds, std::move(uiInput),
                                safeInsetsPixels, debugStats);
        }
        if (gameRenderer.consumeTextInputFocusRequest()) {
            std::string focusValue = gameRenderer.consumeTextInputFocusValue();
            std::lock_guard<std::mutex> lock(mutex_);
            pendingUiTextInputFocusValue_ = std::move(focusValue);
            uiTextInputFocusRequested_ = true;
        }

        bool swapSucceeded = false;
        {
            [[maybe_unused]] core::TraceSection swapTrace("BoxEngine.EGLSwapBuffers");
            [[maybe_unused]] auto swapProfile =
                frameProfiler.profile(core::ProfileSubsystem::SwapBuffers);
            swapSucceeded = egl.swapBuffers();
        }
        if (!swapSucceeded) {
            BOX_LOG_WARN("Resetting EGL after a failed buffer swap.");
            gameRenderer.abandon();
            rendererReady = false;
            egl.shutdown();
            eglReady = false;
            waitForLifecycleChange(kRetryDelayMilliseconds);
            continue;
        }

        (void)frameProfiler.endFrame(
            std::chrono::duration<double, std::milli>(kTargetFrameDuration).count());
        const auto frameEnd = core::TimeSystem::Clock::now();
        previousFrameMilliseconds = frameProfiler.snapshot().frameMilliseconds;
        nextFrameDeadline += std::chrono::duration_cast<core::TimeSystem::Clock::duration>(
            kTargetFrameDuration);
        if (frameEnd < nextFrameDeadline) {
            std::this_thread::sleep_until(nextFrameDeadline);
        } else if (frameEnd - nextFrameDeadline > std::chrono::milliseconds(100)) {
            nextFrameDeadline = frameEnd;
        }
    }

    tearDownGraphics();
    if (currentWindow != nullptr) ANativeWindow_release(currentWindow);
    BOX_LOG_INFO("Render thread exited cleanly.");
}

void EngineRuntime::waitForLifecycleChange(unsigned int timeoutMilliseconds) {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait_for(lock, std::chrono::milliseconds(timeoutMilliseconds), [this]() {
        return stopRequested_ || surfaceUpdatePending_ || paused_;
    });
}

}  // namespace box
