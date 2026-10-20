// Path: engine/src/main/cpp/include/box/renderer/immediate_ui.hpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "box/math/math.hpp"
#include "box/renderer/sprite_batch.hpp"
#include "box/renderer/ui_types.hpp"

namespace box::renderer {

/** Resolves DP rectangles against the safe content area for all orientations and cutouts. */
class UiLayout final {
public:
    void configure(float widthDp, float heightDp, UiInsets safeInsetsDp) noexcept;
    [[nodiscard]] UiRect resolve(UiRect rectangle, UiAnchor anchor) const noexcept;
    [[nodiscard]] bool contains(UiRect rectangle, float xDp, float yDp) const noexcept;

    [[nodiscard]] float widthDp() const noexcept { return widthDp_; }
    [[nodiscard]] float heightDp() const noexcept { return heightDp_; }
    [[nodiscard]] UiInsets safeInsetsDp() const noexcept { return safeInsetsDp_; }

private:
    float widthDp_ = 1.0f;
    float heightDp_ = 1.0f;
    UiInsets safeInsetsDp_{};
};

/** Immediate-mode GLES quad UI with safe-area anchors, touch widgets, and a compact bitmap face. */
class ImmediateUi final {
public:
    void beginFrame(SpriteBatch& batch, float widthPixels, float heightPixels, float density,
                    UiInsets safeInsetsPixels, UiFrameInput input = {});
    void endFrame();

    void label(std::string_view text, UiRect rectangle, UiAnchor anchor = UiAnchor::TopLeft,
               math::Vec4 color = {0.92f, 0.95f, 1.0f, 1.0f}, float textScale = 1.0f);
    [[nodiscard]] bool button(std::string_view id, std::string_view text, UiRect rectangle,
                              UiAnchor anchor = UiAnchor::TopLeft);
    [[nodiscard]] float slider(std::string_view id, float value, float minimum, float maximum,
                               UiRect rectangle, UiAnchor anchor = UiAnchor::TopLeft,
                               std::string_view labelText = {});
    [[nodiscard]] math::Vec2 joystick(std::string_view id, UiRect rectangle,
                                      UiAnchor anchor = UiAnchor::BottomLeft);
    [[nodiscard]] bool textInput(std::string_view id, std::string& value, UiRect rectangle,
                                 UiAnchor anchor = UiAnchor::TopLeft,
                                 std::string_view placeholder = "ENTER TEXT");

    [[nodiscard]] bool consumeTextInputFocusRequest() noexcept;
    [[nodiscard]] bool textInputFocused() const noexcept { return !focusedId_.empty(); }
    [[nodiscard]] std::string_view focusedTextInputId() const noexcept { return focusedId_; }
    [[nodiscard]] const UiLayout& layout() const noexcept { return layout_; }

private:
    [[nodiscard]] bool pointerInside(UiRect rectangle) const noexcept;
    void startCapture(std::string_view id, UiRect rectangle);
    [[nodiscard]] bool pointerCaptured(std::string_view id) const noexcept;
    [[nodiscard]] bool finishCapture(std::string_view id, UiRect rectangle);
    void drawPanel(UiRect rectangle, math::Vec4 color);
    void drawText(std::string_view text, UiRect rectangle, math::Vec4 color,
                  float textScale, bool centered);
    void drawCircle(math::Vec2 center, float radius, math::Vec4 color);
    void appendTextInput(std::string& value);

    UiLayout layout_;
    SpriteBatch* batch_ = nullptr;
    UiFrameInput input_{};
    std::string activeId_;
    std::string focusedId_;
    std::int32_t activePointerId_ = -1;
    math::Vec2 joystickValue_{};
    float density_ = 1.0f;
    float pointerXdp_ = 0.0f;
    float pointerYdp_ = 0.0f;
    bool focusRequestPending_ = false;
    bool frameActive_ = false;
};

}  // namespace box::renderer
