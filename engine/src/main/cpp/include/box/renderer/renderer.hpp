// Path: engine/src/main/cpp/include/box/renderer/renderer.hpp
#pragma once

#include <android/asset_manager.h>

#include <cstddef>
#include <string>

#include "box/renderer/camera2d.hpp"
#include "box/renderer/debug_overlay.hpp"
#include "box/renderer/immediate_ui.hpp"
#include "box/renderer/particle_system.hpp"
#include "box/renderer/render_target.hpp"
#include "box/renderer/shader_program.hpp"
#include "box/renderer/sprite_animation.hpp"
#include "box/renderer/sprite_batch.hpp"
#include "box/renderer/texture.hpp"
#include "box/renderer/texture_atlas.hpp"
#include "box/renderer/tilemap.hpp"

namespace box::renderer {

/** GLES 3.0 sample renderer integrating camera, animation, atlas tiles, and CPU particles. */
class Renderer final {
public:
    Renderer(AAssetManager* assets, std::string shaderOverrideDirectory);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool initialize();
    void render(int widthPixels, int heightPixels, float density, double elapsedSeconds,
                double deltaSeconds, double previousFrameMilliseconds,
                UiFrameInput uiInput = {}, UiInsets safeInsetsPixels = {},
                DebugOverlayStats debugStats = {});
    [[nodiscard]] bool reloadShaders();
    void setRenderScale(float scale) noexcept;
    void setAdaptiveResolutionEnabled(bool enabled) noexcept;
    [[nodiscard]] Camera2D& camera() noexcept { return camera_; }
    void shutdown() noexcept;
    void abandon() noexcept;

    [[nodiscard]] float renderScale() const noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] bool consumeTextInputFocusRequest() noexcept;
    [[nodiscard]] std::string consumeTextInputFocusValue();

private:
    class AdaptiveResolutionController final {
    public:
        void update(double frameMilliseconds) noexcept;
        void reset() noexcept;
        [[nodiscard]] float scale() const noexcept { return scale_; }

    private:
        float scale_ = 1.0f;
        unsigned int slowFrames_ = 0;
        unsigned int fastFrames_ = 0;
    };

    AAssetManager* assets_ = nullptr;
    std::string shaderOverrideDirectory_;
    ShaderProgram shader_;
    SpriteBatch batch_;
    ImmediateUi ui_;
    Texture2D checkerTexture_;
    RenderTarget renderTarget_;
    Camera2D camera_;
    SpriteAnimationPlayer animation_;
    ParticleSystem particles_{128U, 0xB0A5EEDU};
    TextureAtlas tileAtlas_;
    TilemapRenderer tilemapRenderer_;
    Tilemap2D sampleTilemap_;
    AdaptiveResolutionController adaptiveController_;
    float uiSliderValue_ = 0.7f;
    std::string uiTextInput_;
    std::string uiConsoleCommand_;
    std::string uiTextInputFocusValue_;
    bool uiMenuOpen_ = false;
    bool uiConsoleOpen_ = false;
    bool uiDebugOverlayOpen_ = true;
    bool uiTextInputFocusRequest_ = false;
    std::size_t previousDrawCallCount_ = 0U;
    float requestedScale_ = 1.0f;
    bool adaptiveResolutionEnabled_ = true;
    bool ready_ = false;
};

}  // namespace box::renderer
