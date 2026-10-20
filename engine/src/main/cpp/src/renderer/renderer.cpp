// Path: engine/src/main/cpp/src/renderer/renderer.cpp
#include "box/renderer/renderer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

#include "box/core/log.hpp"
#include "box/core/trace_section.hpp"
#include "box/math/math.hpp"
#include "box/renderer/gl_utils.hpp"

namespace box::renderer {
namespace {

[[nodiscard]] float clampRenderScale(float scale) noexcept {
    return std::clamp(scale, 0.5f, 1.0f);
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

void executeDebugCommand(std::string_view command, const DebugOverlayStats& stats,
                         std::size_t drawCalls) {
    command = trim(command);
    if (command.empty()) return;
    const std::size_t separator = command.find_first_of(" \t\r\n");
    std::string verb(command.substr(0U, separator));
    std::transform(verb.begin(), verb.end(), verb.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    const std::string_view argument = separator == std::string_view::npos
                                          ? std::string_view{}
                                          : trim(command.substr(separator + 1U));
    if (verb == "help" && argument.empty()) {
        BOX_LOG_INFO("Commands: help, clear, echo <text>, stats, entities");
    } else if (verb == "clear" && argument.empty()) {
        core::clearLogStream();
        BOX_LOG_INFO("Console log cleared.");
    } else if (verb == "echo") {
        if (!argument.empty()) {
            BOX_LOG_INFO("%.*s", static_cast<int>(argument.size()), argument.data());
        }
    } else if (verb == "stats" && argument.empty()) {
        BOX_LOG_INFO("FPS %.1f, frame %.1f ms, draw calls %zu, RSS %.1f MB",
                     stats.frame.framesPerSecond, stats.frame.frameMilliseconds, drawCalls,
                     static_cast<double>(stats.residentMemoryBytes) / (1024.0 * 1024.0));
    } else if (verb == "entities" && argument.empty()) {
        BOX_LOG_INFO("Entity count: %zu", stats.entityCount);
    } else {
        BOX_LOG_WARN("Unknown command. Type help for the command list.");
    }
}

}  // namespace

Renderer::Renderer(AAssetManager* assets, std::string shaderOverrideDirectory)
    : assets_(assets),
      shaderOverrideDirectory_(std::move(shaderOverrideDirectory)),
      batch_(4096) {}

Renderer::~Renderer() { shutdown(); }

bool Renderer::initialize() {
    shutdown();
    if (!shader_.load(assets_, shaderOverrideDirectory_)) {
        BOX_LOG_ERROR("Failed to initialize the GLES shader program.");
        return false;
    }
    if (!batch_.initialize()) {
        shader_.destroy();
        return false;
    }

    constexpr GLsizei textureSize = 8;
    std::array<std::uint8_t, static_cast<std::size_t>(textureSize * textureSize * 4)> pixels{};
    for (GLsizei y = 0; y < textureSize; ++y) {
        for (GLsizei x = 0; x < textureSize; ++x) {
            const bool lightTile = ((x / 2) + (y / 2)) % 2 == 0;
            const std::size_t index = static_cast<std::size_t>((y * textureSize + x) * 4);
            pixels[index] = lightTile ? 255U : 42U;
            pixels[index + 1] = lightTile ? 218U : 115U;
            pixels[index + 2] = lightTile ? 196U : 220U;
            pixels[index + 3] = 255U;
        }
    }
    if (!checkerTexture_.uploadRgba8(textureSize, textureSize, pixels.data(), GL_NEAREST,
                                     GL_NEAREST)) {
        batch_.destroy();
        shader_.destroy();
        return false;
    }

    TextureAtlasBuilder atlasBuilder(64, 1);
    AtlasImage tileImage{"checker-tile", textureSize, textureSize,
                         std::vector<std::uint8_t>(pixels.begin(), pixels.end())};
    std::string atlasError;
    if (!atlasBuilder.add(std::move(tileImage)) || !atlasBuilder.build(tileAtlas_, &atlasError) ||
        !sampleTilemap_.resize(8U, 1U)) {
        BOX_LOG_ERROR("Failed to build the sample runtime atlas: %s", atlasError.c_str());
        tileAtlas_.destroy();
        checkerTexture_.destroy();
        batch_.destroy();
        shader_.destroy();
        return false;
    }
    sampleTilemap_.regionNames = {"checker-tile"};
    for (std::uint32_t x = 0; x < sampleTilemap_.width; ++x) {
        const std::uint8_t flags = (x % 2U == 0U) ? 0U : kTileFlipX;
        (void)sampleTilemap_.setTile(x, 0U, 0U, flags);
    }

    AnimationClip sampleClip{};
    sampleClip.name = "checker-sheet";
    sampleClip.looping = true;
    sampleClip.frames = {
        {UvRect{0.0f, 0.0f, 0.5f, 0.5f}, 0.18f},
        {UvRect{0.5f, 0.0f, 1.0f, 0.5f}, 0.18f},
        {UvRect{0.5f, 0.5f, 1.0f, 1.0f}, 0.18f},
        {UvRect{0.0f, 0.5f, 0.5f, 1.0f}, 0.18f},
    };
    (void)animation_.play(std::move(sampleClip));

    ParticleEmitterConfig sampleParticles{};
    sampleParticles.emissionRate = 5.0f;
    sampleParticles.lifetimeSeconds = 1.0f;
    sampleParticles.speed = 28.0f;
    sampleParticles.spreadRadians = 1.2f;
    sampleParticles.direction = {0.0f, 1.0f};
    sampleParticles.startSize = 5.0f;
    sampleParticles.endSize = 1.0f;
    sampleParticles.startColor = {0.35f, 0.86f, 1.0f, 0.8f};
    sampleParticles.endColor = {0.22f, 0.42f, 1.0f, 0.0f};
    particles_.setConfig(sampleParticles);
    particles_.setEmitting(true);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    ready_ = !checkGlErrors("renderer initialization");
    if (!ready_) shutdown();
    adaptiveController_.reset();
    return ready_;
}

void Renderer::render(int widthPixels, int heightPixels, float density,
                      double elapsedSeconds, double deltaSeconds,
                      double previousFrameMilliseconds, UiFrameInput uiInput,
                      UiInsets safeInsetsPixels, DebugOverlayStats debugStats) {
    if (!ready_ || widthPixels <= 0 || heightPixels <= 0) return;
    density = std::max(density, 0.1f);
    if (adaptiveResolutionEnabled_) adaptiveController_.update(previousFrameMilliseconds);
    batch_.resetDrawCallCount();

    const float scale = renderScale();
    const GLsizei targetWidth = std::max<GLsizei>(
        1, static_cast<GLsizei>(std::lround(static_cast<double>(widthPixels) * scale)));
    const GLsizei targetHeight = std::max<GLsizei>(
        1, static_cast<GLsizei>(std::lround(static_cast<double>(heightPixels) * scale)));
    const bool useOffscreen = renderTarget_.resize(targetWidth, targetHeight);
    const float actualScale = useOffscreen ? scale : 1.0f;
    const ViewportPixels viewport = camera_.viewportPixels(widthPixels, heightPixels, actualScale);
    const ViewportPixels logicalViewport = camera_.viewportPixels(widthPixels, heightPixels);
    if (useOffscreen) {
        renderTarget_.bind();
        glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    }

    glClearColor(0.035f, 0.052f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float widthDp = static_cast<float>(logicalViewport.width) / density;
    const float heightDp = static_cast<float>(logicalViewport.height) / density;
    camera_.update(deltaSeconds);
    animation_.update(deltaSeconds);
    particles_.setOrigin({camera_.position.x, camera_.position.y - heightDp * 0.20f});
    particles_.update(deltaSeconds);
    const math::Mat4 projection = camera_.viewProjection(
        logicalViewport.width, logicalViewport.height, density);
    batch_.begin(shader_, projection);

    sampleTilemap_.tileWidth = std::clamp(widthDp / 8.0f, 8.0f, 24.0f);
    sampleTilemap_.tileHeight = sampleTilemap_.tileWidth;
    sampleTilemap_.origin = {-sampleTilemap_.tileWidth * 4.0f, -heightDp * 0.42f};
    tilemapRenderer_.draw(batch_, tileAtlas_, sampleTilemap_);

    const float centerX = camera_.position.x;
    const float centerY = camera_.position.y + heightDp * 0.12f;
    const float radius = std::min(widthDp, heightDp) * 0.18f;
    const std::array<math::Vec2, 3> triangle = {
        math::Vec2{centerX, centerY + radius},
        math::Vec2{centerX - radius, centerY - radius},
        math::Vec2{centerX + radius, centerY - radius},
    };
    const std::array<math::Vec4, 3> triangleColors = {
        math::Vec4{0.38f, 0.92f, 0.79f, 1.0f},
        math::Vec4{0.24f, 0.55f, 1.0f, 1.0f},
        math::Vec4{0.91f, 0.43f, 0.87f, 1.0f},
    };
    batch_.drawTriangle(triangle, triangleColors);

    const float spriteSize = std::clamp(std::min(widthDp, heightDp) * 0.12f, 24.0f, 88.0f);
    const float baseY = camera_.position.y - heightDp * 0.32f;
    constexpr std::array<math::Vec4, 5> tints = {
        math::Vec4{1.0f, 1.0f, 1.0f, 1.0f},
        math::Vec4{1.0f, 0.76f, 0.40f, 1.0f},
        math::Vec4{0.72f, 0.84f, 1.0f, 1.0f},
        math::Vec4{0.95f, 0.67f, 0.89f, 1.0f},
        math::Vec4{0.74f, 1.0f, 0.72f, 1.0f},
    };
    for (std::size_t index = 0; index < tints.size(); ++index) {
        const float position = static_cast<float>(index + 1U) /
                               static_cast<float>(tints.size() + 1U);
        const float x = camera_.position.x + (position - 0.5f) * widthDp - spriteSize * 0.5f;
        const double phase = elapsedSeconds * 2.0 + static_cast<double>(index) * 0.7;
        const float bob = static_cast<float>(std::sin(phase)) *
                          std::min(heightDp * 0.025f, 14.0f);
        if (index == 0U) {
            const AnimationSample sample = animation_.sample();
            if (sample.hasPrevious) {
                math::Vec4 oldTint = tints[index];
                oldTint.w *= 1.0f - sample.blend;
                batch_.drawSpriteRegion(checkerTexture_, x, baseY + bob, spriteSize, spriteSize,
                                        sample.previous, oldTint);
                math::Vec4 newTint = tints[index];
                newTint.w *= sample.blend;
                batch_.drawSpriteRegion(checkerTexture_, x, baseY + bob, spriteSize, spriteSize,
                                        sample.current, newTint);
            } else {
                batch_.drawSpriteRegion(checkerTexture_, x, baseY + bob, spriteSize, spriteSize,
                                        sample.current, tints[index]);
            }
        } else {
            batch_.drawSprite(checkerTexture_, x, baseY + bob, spriteSize, spriteSize, tints[index]);
        }
    }
    particles_.render(batch_);
    batch_.flush();

    if (useOffscreen) {
        renderTarget_.blitToDefault(widthPixels, heightPixels);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glViewport(0, 0, widthPixels, heightPixels);

    const float surfaceWidthDp = static_cast<float>(widthPixels) / density;
    const float surfaceHeightDp = static_cast<float>(heightPixels) / density;
    const math::Mat4 uiProjection = math::Mat4::orthographic(
        0.0f, surfaceWidthDp, surfaceHeightDp, 0.0f, -1.0f, 1.0f);
    batch_.begin(shader_, uiProjection);
    ui_.beginFrame(batch_, static_cast<float>(widthPixels), static_cast<float>(heightPixels),
                   density, safeInsetsPixels, std::move(uiInput));
    [[maybe_unused]] core::TraceSection uiTrace("BoxEngine.UI");
    auto uiProfile = debugStats.profiler != nullptr
                         ? debugStats.profiler->profile(core::ProfileSubsystem::Ui)
                         : core::FrameProfiler::Scope{};
    if (ui_.button("debug.overlay", uiDebugOverlayOpen_ ? "DEBUG ON" : "DEBUG OFF",
                   {8.0f, 8.0f, 104.0f, 28.0f}, UiAnchor::TopRight)) {
        uiDebugOverlayOpen_ = !uiDebugOverlayOpen_;
    }
    if (uiDebugOverlayOpen_) {
        GLint activeProgram = 0;
        GLint activeFramebuffer = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &activeProgram);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &activeFramebuffer);
        const bool depthEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        const bool blendEnabled = glIsEnabled(GL_BLEND) == GL_TRUE;
        const UiRect panel = ui_.layout().resolve(
            {8.0f, 42.0f, 212.0f, 108.0f}, UiAnchor::TopRight);
        batch_.drawSolidQuad(panel.x, panel.y, panel.width, panel.height,
                             {0.025f, 0.045f, 0.08f, 0.88f});
        const auto& cpu = debugStats.frame.subsystemMilliseconds;
        char frameLine[64]{};
        char drawLine[64]{};
        char memoryLine[64]{};
        char glLine[64]{};
        char cpuLineA[64]{};
        char cpuLineB[64]{};
        (void)std::snprintf(frameLine, sizeof(frameLine), "FPS %.1f FT %.1fMS",
                            debugStats.frame.framesPerSecond,
                            debugStats.frame.frameMilliseconds);
        (void)std::snprintf(drawLine, sizeof(drawLine), "DRAW %zu ENT %zu",
                            previousDrawCallCount_, debugStats.entityCount);
        (void)std::snprintf(memoryLine, sizeof(memoryLine), "MEM %.1fMB",
                            static_cast<double>(debugStats.residentMemoryBytes) /
                                (1024.0 * 1024.0));
        (void)std::snprintf(glLine, sizeof(glLine), "GL P%d F%d D%d B%d", activeProgram,
                            activeFramebuffer, depthEnabled ? 1 : 0, blendEnabled ? 1 : 0);
        (void)std::snprintf(cpuLineA, sizeof(cpuLineA), "CPU F%.1f V%.1f SG%.1f",
                            cpu[static_cast<std::size_t>(core::ProfileSubsystem::FixedUpdate)],
                            cpu[static_cast<std::size_t>(core::ProfileSubsystem::VariableUpdate)],
                            cpu[static_cast<std::size_t>(core::ProfileSubsystem::SceneGraph)]);
        (void)std::snprintf(cpuLineB, sizeof(cpuLineB), "CPU R%.1f UI%.1f SW%.1f",
                            cpu[static_cast<std::size_t>(core::ProfileSubsystem::Renderer)],
                            cpu[static_cast<std::size_t>(core::ProfileSubsystem::Ui)],
                            cpu[static_cast<std::size_t>(core::ProfileSubsystem::SwapBuffers)]);
        const std::array<const char*, 6> lines = {
            frameLine, drawLine, memoryLine, glLine, cpuLineA, cpuLineB};
        for (std::size_t line = 0; line < lines.size(); ++line) {
            ui_.label(lines[line],
                      {16.0f, 50.0f + static_cast<float>(line) * 15.0f, 196.0f, 12.0f},
                      UiAnchor::TopRight, {0.86f, 0.93f, 1.0f, 1.0f}, 0.72f);
        }
    }
    ui_.label("BOX ENGINE UI", {12.0f, 12.0f, 150.0f, 18.0f}, UiAnchor::TopLeft,
              {0.5f, 0.85f, 1.0f, 1.0f}, 0.9f);
    if (ui_.button("sample.menu", uiMenuOpen_ ? "CLOSE" : "MENU",
                    {12.0f, 36.0f, 96.0f, 36.0f}, UiAnchor::TopLeft)) {
        uiMenuOpen_ = !uiMenuOpen_;
    }
    uiSliderValue_ = ui_.slider("sample.level", uiSliderValue_, 0.0f, 1.0f,
                                {12.0f, 84.0f, 160.0f, 44.0f}, UiAnchor::TopLeft, "LEVEL");
    if (uiMenuOpen_) {
        ui_.label("TOUCH CONTROLS READY", {12.0f, 138.0f, 192.0f, 18.0f},
                  UiAnchor::TopLeft, {0.7f, 0.9f, 1.0f, 1.0f}, 0.8f);
        if (!uiConsoleOpen_) {
            (void)ui_.textInput("sample.name", uiTextInput_, {12.0f, 164.0f, 220.0f, 38.0f},
                                UiAnchor::TopLeft, "PLAYER NAME");
        }
    }
    (void)ui_.joystick("sample.joystick", {16.0f, 16.0f, 112.0f, 112.0f},
                       UiAnchor::BottomLeft);
    if (ui_.button("console.toggle", uiConsoleOpen_ ? "HIDE LOG" : "CONSOLE",
                   {12.0f, 12.0f, 92.0f, 32.0f}, UiAnchor::BottomRight)) {
        uiConsoleOpen_ = !uiConsoleOpen_;
    }
    if (uiConsoleOpen_) {
        const float consoleWidth = std::max(160.0f, std::min(260.0f, surfaceWidthDp - 24.0f));
        const float consoleHeight = std::clamp(surfaceHeightDp * 0.52f, 140.0f, 220.0f);
        const UiRect consolePanel = ui_.layout().resolve(
            {12.0f, 52.0f, consoleWidth, consoleHeight}, UiAnchor::BottomRight);
        batch_.drawSolidQuad(consolePanel.x, consolePanel.y, consolePanel.width,
                             consolePanel.height, {0.025f, 0.045f, 0.08f, 0.94f});
        ui_.label("LOG CONSOLE",
                  {16.0f, 52.0f + consoleHeight - 28.0f, consoleWidth - 32.0f, 14.0f},
                  UiAnchor::BottomRight, {0.5f, 0.85f, 1.0f, 1.0f}, 0.8f);
        const auto logCapacity = static_cast<std::size_t>(std::clamp(
            (consoleHeight - 90.0f) / 14.0f, 1.0f, 8.0f));
        const std::vector<core::LogEntry> entries = core::recentLogEntries(logCapacity);
        for (std::size_t line = 0; line < entries.size(); ++line) {
            const float verticalOffset = 52.0f + consoleHeight - 48.0f -
                                         static_cast<float>(line) * 14.0f;
            ui_.label(entries[line].message,
                      {16.0f, verticalOffset, consoleWidth - 32.0f, 12.0f},
                      UiAnchor::BottomRight, {0.82f, 0.88f, 0.95f, 1.0f}, 0.62f);
        }
        if (ui_.textInput("console.command", uiConsoleCommand_,
                          {12.0f, 52.0f, consoleWidth - 24.0f, 34.0f},
                          UiAnchor::BottomRight, "ENTER COMMAND")) {
            executeDebugCommand(uiConsoleCommand_, debugStats, previousDrawCallCount_);
            uiConsoleCommand_.clear();
        }
    }
    ui_.endFrame();
    uiProfile.stop();
    uiTrace.end();
    previousDrawCallCount_ = batch_.drawCallCount();
    uiTextInputFocusRequest_ = ui_.consumeTextInputFocusRequest();
    if (uiTextInputFocusRequest_) {
        const std::string_view focusedId = ui_.focusedTextInputId();
        if (focusedId == "console.command") uiTextInputFocusValue_ = uiConsoleCommand_;
        else if (focusedId == "sample.name") uiTextInputFocusValue_ = uiTextInput_;
        else uiTextInputFocusValue_.clear();
    }
#ifndef NDEBUG
    (void)checkGlErrors("frame render");
#endif
}

bool Renderer::reloadShaders() {
    if (!ready_) return false;
    return shader_.reload(assets_, shaderOverrideDirectory_);
}

void Renderer::setRenderScale(float scale) noexcept { requestedScale_ = clampRenderScale(scale); }

void Renderer::setAdaptiveResolutionEnabled(bool enabled) noexcept {
    adaptiveResolutionEnabled_ = enabled;
}

void Renderer::shutdown() noexcept {
    renderTarget_.destroy();
    tileAtlas_.destroy();
    checkerTexture_.destroy();
    batch_.destroy();
    shader_.destroy();
    particles_.clear();
    animation_.stop();
    ready_ = false;
}

void Renderer::abandon() noexcept {
    renderTarget_.abandon();
    tileAtlas_.abandon();
    checkerTexture_.abandon();
    batch_.abandon();
    shader_.abandon();
    ready_ = false;
}

float Renderer::renderScale() const noexcept {
    return adaptiveResolutionEnabled_ ? std::min(adaptiveController_.scale(), requestedScale_)
                                      : requestedScale_;
}

bool Renderer::consumeTextInputFocusRequest() noexcept {
    const bool requested = uiTextInputFocusRequest_;
    uiTextInputFocusRequest_ = false;
    return requested;
}

std::string Renderer::consumeTextInputFocusValue() {
    return std::exchange(uiTextInputFocusValue_, std::string{});
}

void Renderer::AdaptiveResolutionController::update(double frameMilliseconds) noexcept {
    if (frameMilliseconds <= 0.0) return;
    if (frameMilliseconds > 18.5) {
        ++slowFrames_;
        fastFrames_ = 0;
    } else if (frameMilliseconds < 13.5) {
        ++fastFrames_;
        slowFrames_ = 0;
    } else {
        slowFrames_ = 0;
        fastFrames_ = 0;
    }

    if (slowFrames_ >= 24U) {
        scale_ = std::max(0.5f, scale_ - 0.05f);
        slowFrames_ = 0;
    } else if (fastFrames_ >= 120U) {
        scale_ = std::min(1.0f, scale_ + 0.025f);
        fastFrames_ = 0;
    }
}

void Renderer::AdaptiveResolutionController::reset() noexcept {
    scale_ = 1.0f;
    slowFrames_ = 0;
    fastFrames_ = 0;
}

}  // namespace box::renderer
