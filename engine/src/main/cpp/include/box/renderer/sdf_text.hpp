#pragma once

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <android/asset_manager.h>

#include "box/math/math.hpp"
#include "box/renderer/sprite_batch.hpp"
#include "box/renderer/texture.hpp"

namespace box::renderer {

/** Converts top-to-bottom 8-bit coverage pixels into a normalized R8 signed-distance field. */
[[nodiscard]] bool coverageToSignedDistanceField(const std::uint8_t* coverage, int width,
                                                  int height, int spreadPixels,
                                                  std::vector<std::uint8_t>& output);

/** Pre-rasterized SDF glyph with top-to-bottom R8 pixels and baseline-relative bearings. */
struct GlyphBitmap {
    std::uint32_t codepoint = 0;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advance = 0;
    std::vector<std::uint8_t> signedDistancePixels;
};

struct SdfGlyph {
    std::uint32_t codepoint = 0;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advance = 0;
    UvRect uv{};
};

/** Runtime-populated R8 atlas for pre-rasterized signed-distance-field glyphs. */
class DynamicFontAtlas final {
public:
    [[nodiscard]] bool initialize(int width, int height, int lineHeight, int padding = 2);
    [[nodiscard]] bool addGlyph(const GlyphBitmap& bitmap);
    [[nodiscard]] const SdfGlyph* find(std::uint32_t codepoint) const noexcept;
    void clear() noexcept;
    void abandon() noexcept;

    [[nodiscard]] const Texture2D& texture() const noexcept { return texture_; }
    [[nodiscard]] int lineHeight() const noexcept { return lineHeight_; }
    [[nodiscard]] std::size_t glyphCount() const noexcept { return glyphs_.size(); }

private:
    Texture2D texture_;
    std::unordered_map<std::uint32_t, SdfGlyph> glyphs_;
    int width_ = 0;
    int height_ = 0;
    int lineHeight_ = 0;
    int padding_ = 2;
    int cursorX_ = 0;
    int cursorY_ = 0;
    int rowHeight_ = 0;
};

/** GLES 3.0 SDF glyph renderer with UTF-8 decoding and streamed quad batches. */
class SdfTextRenderer final {
public:
    explicit SdfTextRenderer(std::size_t maximumGlyphs = 2048U);
    ~SdfTextRenderer();

    SdfTextRenderer(const SdfTextRenderer&) = delete;
    SdfTextRenderer& operator=(const SdfTextRenderer&) = delete;

    [[nodiscard]] bool initialize(AAssetManager* assets,
                                  const std::string& shaderOverrideDirectory = {});
    [[nodiscard]] bool reloadShaders(AAssetManager* assets,
                                     const std::string& shaderOverrideDirectory = {});
    void draw(const DynamicFontAtlas& font, std::string_view utf8Text, float x,
              float baselineY, float scale, math::Vec4 color,
              const math::Mat4& projection, float smoothing = 0.08f);
    void destroy() noexcept;
    void abandon() noexcept;

private:
    struct TextVertex {
        float x;
        float y;
        float u;
        float v;
    };

    [[nodiscard]] bool compileProgram(const std::string& vertexSource,
                                      const std::string& fragmentSource);
    void flush(const DynamicFontAtlas& font, const math::Mat4& projection,
               math::Vec4 color, float smoothing, const std::vector<TextVertex>& vertices);

    std::size_t maximumGlyphs_;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint projectionLocation_ = -1;
    GLint colorLocation_ = -1;
    GLint smoothingLocation_ = -1;
    GLint textureLocation_ = -1;
};

}  // namespace box::renderer
