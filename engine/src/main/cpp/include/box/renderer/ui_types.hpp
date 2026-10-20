// Path: engine/src/main/cpp/include/box/renderer/ui_types.hpp
#pragma once

#include <cstdint>
#include <string>

namespace box::renderer {

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct UiInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class UiAnchor : std::uint8_t {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

enum class UiPointerAction : std::uint8_t { None, Down, Move, Up, Cancel };

struct UiFrameInput {
    UiPointerAction pointerAction = UiPointerAction::None;
    std::int32_t pointerId = -1;
    float pointerXpx = 0.0f;
    float pointerYpx = 0.0f;
    std::string textUtf8;
    bool replaceText = false;
    bool backspace = false;
    bool submit = false;
};

}  // namespace box::renderer
