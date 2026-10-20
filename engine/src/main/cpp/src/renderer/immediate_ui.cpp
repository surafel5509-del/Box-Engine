// Path: engine/src/main/cpp/src/renderer/immediate_ui.cpp
#include "box/renderer/immediate_ui.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace box::renderer {
namespace {

constexpr float kPanelPaddingDp = 8.0f;
constexpr float kGlyphWidth = 5.0f;
constexpr float kGlyphHeight = 7.0f;
constexpr float kGlyphAdvance = 6.0f;
constexpr math::Vec4 kPanelColor{0.08f, 0.12f, 0.2f, 0.88f};
constexpr math::Vec4 kPanelActiveColor{0.12f, 0.34f, 0.56f, 0.95f};
constexpr math::Vec4 kAccentColor{0.2f, 0.72f, 1.0f, 1.0f};
constexpr math::Vec4 kTrackColor{0.22f, 0.28f, 0.38f, 1.0f};
constexpr math::Vec4 kTextColor{0.93f, 0.96f, 1.0f, 1.0f};

[[nodiscard]] std::array<std::uint8_t, 5> glyphColumns(char rawCharacter) noexcept {
    const unsigned char character = static_cast<unsigned char>(rawCharacter);
    const char value = static_cast<char>(std::toupper(character));
    switch (value) {
        case 'A': return {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU};
        case 'B': return {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U};
        case 'C': return {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U};
        case 'D': return {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU};
        case 'E': return {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U};
        case 'F': return {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U};
        case 'G': return {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU};
        case 'H': return {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU};
        case 'I': return {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U};
        case 'J': return {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U};
        case 'K': return {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U};
        case 'L': return {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U};
        case 'M': return {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU};
        case 'N': return {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU};
        case 'O': return {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU};
        case 'P': return {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U};
        case 'Q': return {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU};
        case 'R': return {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U};
        case 'S': return {0x46U, 0x49U, 0x49U, 0x49U, 0x31U};
        case 'T': return {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U};
        case 'U': return {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU};
        case 'V': return {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU};
        case 'W': return {0x7FU, 0x20U, 0x18U, 0x20U, 0x7FU};
        case 'X': return {0x63U, 0x14U, 0x08U, 0x14U, 0x63U};
        case 'Y': return {0x03U, 0x04U, 0x78U, 0x04U, 0x03U};
        case 'Z': return {0x61U, 0x51U, 0x49U, 0x45U, 0x43U};
        case '0': return {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU};
        case '1': return {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U};
        case '2': return {0x62U, 0x51U, 0x49U, 0x49U, 0x46U};
        case '3': return {0x22U, 0x41U, 0x49U, 0x49U, 0x36U};
        case '4': return {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U};
        case '5': return {0x2FU, 0x49U, 0x49U, 0x49U, 0x31U};
        case '6': return {0x3EU, 0x49U, 0x49U, 0x49U, 0x32U};
        case '7': return {0x01U, 0x71U, 0x09U, 0x05U, 0x03U};
        case '8': return {0x36U, 0x49U, 0x49U, 0x49U, 0x36U};
        case '9': return {0x26U, 0x49U, 0x49U, 0x49U, 0x3EU};
        case '-': return {0x08U, 0x08U, 0x08U, 0x08U, 0x08U};
        case '_': return {0x40U, 0x40U, 0x40U, 0x40U, 0x40U};
        case '.': return {0x00U, 0x60U, 0x60U, 0x00U, 0x00U};
        case ',': return {0x00U, 0x40U, 0x20U, 0x00U, 0x00U};
        case ':': return {0x00U, 0x36U, 0x36U, 0x00U, 0x00U};
        case ';': return {0x00U, 0x56U, 0x36U, 0x00U, 0x00U};
        case '/': return {0x20U, 0x10U, 0x08U, 0x04U, 0x02U};
        case '\\': return {0x02U, 0x04U, 0x08U, 0x10U, 0x20U};
        case '!': return {0x00U, 0x00U, 0x5FU, 0x00U, 0x00U};
        case '?': return {0x02U, 0x01U, 0x51U, 0x09U, 0x06U};
        case '+': return {0x08U, 0x08U, 0x3EU, 0x08U, 0x08U};
        case '=': return {0x14U, 0x14U, 0x14U, 0x14U, 0x14U};
        case '%': return {0x63U, 0x13U, 0x08U, 0x64U, 0x63U};
        case '(': return {0x00U, 0x1CU, 0x22U, 0x41U, 0x00U};
        case ')': return {0x00U, 0x41U, 0x22U, 0x1CU, 0x00U};
        case '[': return {0x00U, 0x7FU, 0x41U, 0x41U, 0x00U};
        case ']': return {0x00U, 0x41U, 0x41U, 0x7FU, 0x00U};
        case '<': return {0x08U, 0x14U, 0x22U, 0x41U, 0x00U};
        case '>': return {0x00U, 0x41U, 0x22U, 0x14U, 0x08U};
        case '\'': return {0x00U, 0x03U, 0x01U, 0x00U, 0x00U};
        case '"': return {0x00U, 0x07U, 0x00U, 0x07U, 0x00U};
        case ' ': return {0U, 0U, 0U, 0U, 0U};
        default: return {0x02U, 0x01U, 0x51U, 0x09U, 0x06U};
    }
}

[[nodiscard]] float clampFinite(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

}  // namespace

void UiLayout::configure(float widthDp, float heightDp, UiInsets safeInsetsDp) noexcept {
    widthDp_ = std::max(clampFinite(widthDp, 1.0f), 1.0f);
    heightDp_ = std::max(clampFinite(heightDp, 1.0f), 1.0f);
    safeInsetsDp.left = std::clamp(clampFinite(safeInsetsDp.left, 0.0f), 0.0f, widthDp_);
    safeInsetsDp.right = std::clamp(clampFinite(safeInsetsDp.right, 0.0f), 0.0f,
                                    widthDp_ - safeInsetsDp.left);
    safeInsetsDp.top = std::clamp(clampFinite(safeInsetsDp.top, 0.0f), 0.0f, heightDp_);
    safeInsetsDp.bottom = std::clamp(clampFinite(safeInsetsDp.bottom, 0.0f), 0.0f,
                                     heightDp_ - safeInsetsDp.top);
    safeInsetsDp_ = safeInsetsDp;
}

UiRect UiLayout::resolve(UiRect rectangle, UiAnchor anchor) const noexcept {
    rectangle.width = std::max(clampFinite(rectangle.width, 0.0f), 0.0f);
    rectangle.height = std::max(clampFinite(rectangle.height, 0.0f), 0.0f);
    rectangle.x = clampFinite(rectangle.x, 0.0f);
    rectangle.y = clampFinite(rectangle.y, 0.0f);
    const float left = safeInsetsDp_.left;
    const float top = safeInsetsDp_.top;
    const float safeWidth = std::max(widthDp_ - left - safeInsetsDp_.right, 0.0f);
    const float safeHeight = std::max(heightDp_ - top - safeInsetsDp_.bottom, 0.0f);
    rectangle.width = std::min(rectangle.width, safeWidth);
    rectangle.height = std::min(rectangle.height, safeHeight);
    switch (anchor) {
        case UiAnchor::TopLeft:
            rectangle.x += left;
            rectangle.y += top;
            break;
        case UiAnchor::TopCenter:
            rectangle.x += left + (safeWidth - rectangle.width) * 0.5f;
            rectangle.y += top;
            break;
        case UiAnchor::TopRight:
            rectangle.x = left + safeWidth - rectangle.width - rectangle.x;
            rectangle.y += top;
            break;
        case UiAnchor::CenterLeft:
            rectangle.x += left;
            rectangle.y += top + (safeHeight - rectangle.height) * 0.5f;
            break;
        case UiAnchor::Center:
            rectangle.x += left + (safeWidth - rectangle.width) * 0.5f;
            rectangle.y += top + (safeHeight - rectangle.height) * 0.5f;
            break;
        case UiAnchor::CenterRight:
            rectangle.x = left + safeWidth - rectangle.width - rectangle.x;
            rectangle.y += top + (safeHeight - rectangle.height) * 0.5f;
            break;
        case UiAnchor::BottomLeft:
            rectangle.x += left;
            rectangle.y = top + safeHeight - rectangle.height - rectangle.y;
            break;
        case UiAnchor::BottomCenter:
            rectangle.x += left + (safeWidth - rectangle.width) * 0.5f;
            rectangle.y = top + safeHeight - rectangle.height - rectangle.y;
            break;
        case UiAnchor::BottomRight:
            rectangle.x = left + safeWidth - rectangle.width - rectangle.x;
            rectangle.y = top + safeHeight - rectangle.height - rectangle.y;
            break;
    }
    const float maximumX = left + safeWidth - rectangle.width;
    const float maximumY = top + safeHeight - rectangle.height;
    rectangle.x = std::clamp(rectangle.x, left, maximumX);
    rectangle.y = std::clamp(rectangle.y, top, maximumY);
    return rectangle;
}

bool UiLayout::contains(UiRect rectangle, float xDp, float yDp) const noexcept {
    return xDp >= rectangle.x && xDp <= rectangle.x + rectangle.width &&
           yDp >= rectangle.y && yDp <= rectangle.y + rectangle.height;
}

void ImmediateUi::beginFrame(SpriteBatch& batch, float widthPixels, float heightPixels,
                             float density, UiInsets safeInsetsPixels, UiFrameInput input) {
    if (frameActive_) endFrame();
    density_ = std::max(clampFinite(density, 1.0f), 0.1f);
    const float safeDensity = density_;
    safeInsetsPixels.left /= safeDensity;
    safeInsetsPixels.top /= safeDensity;
    safeInsetsPixels.right /= safeDensity;
    safeInsetsPixels.bottom /= safeDensity;
    layout_.configure(std::max(widthPixels, 1.0f) / safeDensity,
                      std::max(heightPixels, 1.0f) / safeDensity, safeInsetsPixels);
    batch_ = &batch;
    input_ = std::move(input);
    pointerXdp_ = input_.pointerXpx / safeDensity;
    pointerYdp_ = input_.pointerYpx / safeDensity;
    frameActive_ = true;
}

void ImmediateUi::endFrame() {
    if (!frameActive_) return;
    if (batch_ != nullptr) batch_->flush();
    batch_ = nullptr;
    frameActive_ = false;
    input_ = {};
}

void ImmediateUi::label(std::string_view text, UiRect rectangle, UiAnchor anchor,
                        math::Vec4 color, float textScale) {
    if (!frameActive_ || batch_ == nullptr) return;
    drawText(text, layout_.resolve(rectangle, anchor), color, textScale, false);
}

bool ImmediateUi::button(std::string_view id, std::string_view text, UiRect rectangle,
                         UiAnchor anchor) {
    if (!frameActive_ || batch_ == nullptr || id.empty()) return false;
    const UiRect resolved = layout_.resolve(rectangle, anchor);
    startCapture(id, resolved);
    const bool pressed = pointerCaptured(id);
    drawPanel(resolved, pressed ? kPanelActiveColor : kPanelColor);
    drawText(text, resolved, kTextColor, 1.0f, true);
    return finishCapture(id, resolved);
}

float ImmediateUi::slider(std::string_view id, float value, float minimum, float maximum,
                          UiRect rectangle, UiAnchor anchor, std::string_view labelText) {
    if (!frameActive_ || batch_ == nullptr || id.empty() ||
        !std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum) {
        return value;
    }
    const UiRect resolved = layout_.resolve(rectangle, anchor);
    startCapture(id, resolved);
    const bool captured = pointerCaptured(id);
    if (captured && (input_.pointerAction == UiPointerAction::Down ||
                     input_.pointerAction == UiPointerAction::Move ||
                     input_.pointerAction == UiPointerAction::Up)) {
        const float thumbRadius = std::max(resolved.height * 0.22f, 5.0f);
        const float trackLeft = resolved.x + thumbRadius;
        const float trackWidth = std::max(resolved.width - thumbRadius * 2.0f, 1.0f);
        const float normalized = std::clamp((pointerXdp_ - trackLeft) / trackWidth, 0.0f, 1.0f);
        value = minimum + normalized * (maximum - minimum);
    }
    (void)finishCapture(id, resolved);
    value = std::clamp(clampFinite(value, minimum), minimum, maximum);
    const float thumbRadius = std::max(resolved.height * 0.22f, 5.0f);
    const float trackHeight = std::max(resolved.height * 0.14f, 3.0f);
    const float trackLeft = resolved.x + thumbRadius;
    const float trackWidth = std::max(resolved.width - thumbRadius * 2.0f, 1.0f);
    const float normalized = (value - minimum) / (maximum - minimum);
    const float trackY = resolved.y + resolved.height * 0.63f;
    if (!labelText.empty()) {
        drawText(labelText, {resolved.x, resolved.y, resolved.width, resolved.height * 0.45f},
                 kTextColor, 0.85f, false);
    }
    drawPanel({trackLeft, trackY, trackWidth, trackHeight}, kTrackColor);
    drawPanel({trackLeft, trackY, trackWidth * normalized, trackHeight}, kAccentColor);
    drawCircle({trackLeft + trackWidth * normalized, trackY + trackHeight * 0.5f},
               thumbRadius, kTextColor);
    return value;
}

math::Vec2 ImmediateUi::joystick(std::string_view id, UiRect rectangle, UiAnchor anchor) {
    if (!frameActive_ || batch_ == nullptr || id.empty()) return {};
    const UiRect resolved = layout_.resolve(rectangle, anchor);
    startCapture(id, resolved);
    const bool captured = pointerCaptured(id);
    const float radius = std::max(std::min(resolved.width, resolved.height) * 0.43f, 1.0f);
    const math::Vec2 center{resolved.x + resolved.width * 0.5f,
                            resolved.y + resolved.height * 0.5f};
    if (captured && (input_.pointerAction == UiPointerAction::Down ||
                     input_.pointerAction == UiPointerAction::Move ||
                     input_.pointerAction == UiPointerAction::Up)) {
        math::Vec2 offset{pointerXdp_ - center.x, center.y - pointerYdp_};
        const float distance = math::length(offset);
        if (distance > radius) offset = offset * (radius / distance);
        joystickValue_ = offset / radius;
    }
    const bool released = finishCapture(id, resolved);
    if (released || input_.pointerAction == UiPointerAction::Cancel) joystickValue_ = {};
    drawCircle(center, radius, kPanelColor);
    drawCircle({center.x + joystickValue_.x * radius * 0.72f,
                center.y - joystickValue_.y * radius * 0.72f},
               radius * 0.34f, captured ? kAccentColor : kTextColor);
    return joystickValue_;
}

bool ImmediateUi::textInput(std::string_view id, std::string& value, UiRect rectangle,
                            UiAnchor anchor, std::string_view placeholder) {
    if (!frameActive_ || batch_ == nullptr || id.empty()) return false;
    const UiRect resolved = layout_.resolve(rectangle, anchor);
    const bool activated = input_.pointerAction == UiPointerAction::Up && pointerInside(resolved);
    if (activated) {
        if (focusedId_ != id) focusedId_.assign(id);
        focusRequestPending_ = true;
    }
    if (focusedId_ == id) appendTextInput(value);
    const bool submitted = focusedId_ == id && input_.submit;
    const bool focused = focusedId_ == id;
    drawPanel(resolved, focused ? kPanelActiveColor : kPanelColor);
    const float border = std::max(1.0f / density_, 1.0f);
    if (focused) {
        drawPanel({resolved.x, resolved.y, resolved.width, border}, kAccentColor);
        drawPanel({resolved.x, resolved.y + resolved.height - border,
                   resolved.width, border}, kAccentColor);
        drawPanel({resolved.x, resolved.y, border, resolved.height}, kAccentColor);
        drawPanel({resolved.x + resolved.width - border, resolved.y,
                   border, resolved.height}, kAccentColor);
    }
    const std::string_view visibleText = value.empty() ? placeholder : std::string_view(value);
    const math::Vec4 visibleColor = value.empty()
                                        ? math::Vec4{0.58f, 0.65f, 0.75f, 1.0f}
                                        : kTextColor;
    const UiRect textRectangle{resolved.x + kPanelPaddingDp, resolved.y,
                               std::max(resolved.width - kPanelPaddingDp * 2.0f, 0.0f),
                               resolved.height};
    drawText(visibleText, textRectangle, visibleColor, 1.0f, false);
    if (submitted) {
        focusedId_.clear();
        input_.submit = false;
    }
    return submitted;
}

bool ImmediateUi::consumeTextInputFocusRequest() noexcept {
    const bool requested = focusRequestPending_;
    focusRequestPending_ = false;
    return requested;
}

bool ImmediateUi::pointerInside(UiRect rectangle) const noexcept {
    return layout_.contains(rectangle, pointerXdp_, pointerYdp_);
}

void ImmediateUi::startCapture(std::string_view id, UiRect rectangle) {
    if (input_.pointerAction != UiPointerAction::Down || !pointerInside(rectangle)) return;
    if (activeId_.empty() || activeId_ == id) {
        activeId_.assign(id);
        activePointerId_ = input_.pointerId;
    }
}

bool ImmediateUi::pointerCaptured(std::string_view id) const noexcept {
    return activeId_ == id && activePointerId_ == input_.pointerId && activePointerId_ >= 0;
}

bool ImmediateUi::finishCapture(std::string_view id, UiRect rectangle) {
    if (input_.pointerAction == UiPointerAction::Cancel && activeId_ == id) {
        activeId_.clear();
        activePointerId_ = -1;
        return false;
    }
    if (!pointerCaptured(id)) return false;
    if (input_.pointerAction != UiPointerAction::Up) return false;
    const bool clicked = pointerInside(rectangle);
    activeId_.clear();
    activePointerId_ = -1;
    return clicked;
}

void ImmediateUi::drawPanel(UiRect rectangle, math::Vec4 color) {
    if (batch_ == nullptr || rectangle.width <= 0.0f || rectangle.height <= 0.0f) return;
    batch_->drawSolidQuad(rectangle.x, rectangle.y, rectangle.width, rectangle.height, color);
}

void ImmediateUi::drawText(std::string_view text, UiRect rectangle, math::Vec4 color,
                           float textScale, bool centered) {
    if (batch_ == nullptr || rectangle.width <= 0.0f || rectangle.height <= 0.0f || text.empty()) return;
    const float scale = std::clamp(clampFinite(textScale, 1.0f), 0.5f, 4.0f);
    const float glyphWidth = kGlyphWidth * scale;
    const float glyphHeight = kGlyphHeight * scale;
    const float advance = kGlyphAdvance * scale;
    float longestLineWidth = 0.0f;
    std::size_t lineLength = 0U;
    for (const char character : text) {
        if (character == '\n') {
            longestLineWidth = std::max(longestLineWidth,
                                        static_cast<float>(lineLength) * advance);
            lineLength = 0U;
        } else if ((static_cast<unsigned char>(character) & 0xC0U) != 0x80U) {
            ++lineLength;
        }
    }
    longestLineWidth = std::max(longestLineWidth, static_cast<float>(lineLength) * advance);
    float cursorX = rectangle.x + kPanelPaddingDp;
    float cursorY = rectangle.y + (rectangle.height - glyphHeight) * 0.5f;
    if (centered) {
        cursorX = rectangle.x + (rectangle.width - longestLineWidth) * 0.5f;
        cursorY = rectangle.y + (rectangle.height - glyphHeight) * 0.5f;
    }
    const float originX = cursorX;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const unsigned char raw = static_cast<unsigned char>(text[index]);
        if (text[index] == '\n') {
            cursorX = originX;
            cursorY += glyphHeight + 2.0f * scale;
            continue;
        }
        if ((raw & 0xC0U) == 0x80U) continue;
        char glyphCharacter = text[index];
        if ((raw & 0x80U) != 0U) {
            glyphCharacter = '?';
            while (index + 1U < text.size() &&
                   (static_cast<unsigned char>(text[index + 1U]) & 0xC0U) == 0x80U) {
                ++index;
            }
        }
        if (cursorX + glyphWidth > rectangle.x + rectangle.width) break;
        const std::array<std::uint8_t, 5> columns = glyphColumns(glyphCharacter);
        for (std::size_t column = 0; column < columns.size(); ++column) {
            for (std::size_t row = 0; row < 7U; ++row) {
                if ((columns[column] & (1U << row)) == 0U) continue;
                const float pixelX = cursorX + static_cast<float>(column) * scale;
                const float pixelY = cursorY + static_cast<float>(row) * scale;
                batch_->drawSolidQuad(pixelX, pixelY, scale, scale, color);
            }
        }
        cursorX += advance;
    }
}

void ImmediateUi::drawCircle(math::Vec2 center, float radius, math::Vec4 color) {
    if (radius <= 0.0f) return;
    constexpr int kBands = 16;
    const float bandHeight = radius * 2.0f / static_cast<float>(kBands);
    for (int band = 0; band < kBands; ++band) {
        const float normalizedY =
            (static_cast<float>(band) + 0.5f) / static_cast<float>(kBands) * 2.0f - 1.0f;
        const float halfWidth = radius * std::sqrt(std::max(1.0f - normalizedY * normalizedY, 0.0f));
        const float y = center.y - radius + static_cast<float>(band) * bandHeight;
        drawPanel({center.x - halfWidth, y, halfWidth * 2.0f, bandHeight + 0.25f}, color);
    }
}

void ImmediateUi::appendTextInput(std::string& value) {
    if (input_.replaceText) {
        std::size_t replaceLength = std::min<std::size_t>(input_.textUtf8.size(), 256U);
        while (replaceLength > 0U && replaceLength < input_.textUtf8.size() &&
               (static_cast<unsigned char>(input_.textUtf8[replaceLength]) & 0xC0U) == 0x80U) {
            --replaceLength;
        }
        value.assign(input_.textUtf8.data(), replaceLength);
        input_.textUtf8.clear();
        input_.replaceText = false;
        input_.backspace = false;
        return;
    }
    if (input_.backspace && !value.empty()) {
        std::size_t codepointStart = value.size() - 1U;
        while (codepointStart > 0U &&
               (static_cast<unsigned char>(value[codepointStart]) & 0xC0U) == 0x80U) {
            --codepointStart;
        }
        value.erase(codepointStart);
        input_.backspace = false;
    }
    constexpr std::size_t kMaximumInputBytes = 256U;
    if (!input_.textUtf8.empty() && value.size() < kMaximumInputBytes) {
        const std::size_t available = kMaximumInputBytes - value.size();
        std::size_t appendLength = std::min(available, input_.textUtf8.size());
        while (appendLength > 0U && appendLength < input_.textUtf8.size() &&
               (static_cast<unsigned char>(input_.textUtf8[appendLength]) & 0xC0U) == 0x80U) {
            --appendLength;
        }
        value.append(input_.textUtf8.data(), appendLength);
        input_.textUtf8.clear();
    }
}

}  // namespace box::renderer
