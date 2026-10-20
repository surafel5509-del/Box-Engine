// Path: engine/src/main/cpp/include/box/engine_runtime.hpp
#pragma once

#include <android/asset_manager.h>
#include <android/native_window.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "box/assets/asset_manager.hpp"
#include "box/audio/audio_engine.hpp"
#include "box/ecs/ecs.hpp"
#include "box/ecs/system_scheduler.hpp"
#include "box/scene/scene_graph.hpp"
#include "box/renderer/ui_types.hpp"

namespace box {

/** Coordinates Android lifecycle events and confines all EGL/GLES calls to one render thread. */
class EngineRuntime final {
public:
    EngineRuntime(AAssetManager* assets, float density, std::string shaderOverrideDirectory);
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    void start();
    void stop();
    void setSurface(ANativeWindow* ownedWindow, bool waitUntilApplied = false);
    void setPaused(bool paused);
    void setScreenInfo(int width, int height, float density);
    void setRenderScale(float scale);
    void setAdaptiveResolution(bool enabled);
    void requestShaderReload();
    void setWindowInsetsPixels(int left, int top, int right, int bottom);
    void enqueueUiPointer(renderer::UiPointerAction action, std::int32_t pointerId,
                          float xPixels, float yPixels);
    void enqueueUiText(std::string textUtf8, bool replaceText, bool backspace, bool submit);
    [[nodiscard]] std::optional<std::string> consumeUiTextInputFocusRequest();

    [[nodiscard]] assets::AssetManager& assetManager() noexcept { return assetManager_; }
    [[nodiscard]] audio::AudioEngine& audio() noexcept { return audioEngine_; }
    [[nodiscard]] ecs::World& world() noexcept { return world_; }
    [[nodiscard]] ecs::SystemScheduler& systems() noexcept { return scheduler_; }
    [[nodiscard]] scene::SceneGraph& sceneGraph() noexcept { return sceneGraph_; }

private:
    void run();
    void waitForLifecycleChange(unsigned int timeoutMilliseconds);

    AAssetManager* assets_ = nullptr;
    std::string shaderOverrideDirectory_;
    assets::AssetManager assetManager_;
    audio::AudioEngine audioEngine_;
    ecs::World world_;
    ecs::SystemScheduler scheduler_;
    scene::SceneGraph sceneGraph_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread renderThread_;
    ANativeWindow* pendingWindow_ = nullptr;
    std::deque<renderer::UiFrameInput> pendingUiEvents_;
    renderer::UiInsets safeInsetsPixels_{};
    std::optional<std::string> pendingUiTextInputFocusValue_;
    bool uiTextInputFocusRequested_ = false;
    std::uint64_t requestedSurfaceGeneration_ = 0;
    std::uint64_t processedSurfaceGeneration_ = 0;
    bool surfaceUpdatePending_ = false;
    bool stopRequested_ = false;
    bool paused_ = true;
    bool shaderReloadRequested_ = false;
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    float density_ = 1.0f;
    float renderScale_ = 1.0f;
    bool adaptiveResolutionEnabled_ = true;
    bool started_ = false;
};

}  // namespace box
