#include "box/renderer/sdf_text.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <vector>

#include "box/core/log.hpp"
#include "box/renderer/asset_io.hpp"
#include "box/renderer/gl_utils.hpp"

namespace box::renderer {
namespace {

constexpr char kFallbackVertex[] = R"glsl(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
uniform mat4 uProjection;
out vec2 vTexCoord;
void main() {
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)glsl";

constexpr char kFallbackFragment[] = R"glsl(#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uFontAtlas;
uniform vec4 uTextColor;
uniform float uSmoothing;
out vec4 fragColor;
void main() {
    float distanceValue = texture(uFontAtlas, vTexCoord).r;
    float alpha = smoothstep(0.5 - uSmoothing, 0.5 + uSmoothing, distanceValue);
    fragColor = vec4(uTextColor.rgb, uTextColor.a * alpha);
}
)glsl";

[[nodiscard]] GLuint compileStage(GLenum type, const std::string& source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    const GLchar* sourcePointer = source.c_str();
    const GLint sourceLength = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &sourcePointer, &sourceLength);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return shader;
    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<GLchar> message(static_cast<std::size_t>(std::max(logLength, 1)));
    glGetShaderInfoLog(shader, static_cast<GLsizei>(message.size()), nullptr, message.data());
    BOX_LOG_ERROR("SDF text shader failed: %s", message.data());
    glDeleteShader(shader);
    return 0;
}

[[nodiscard]] std::uint32_t decodeUtf8(std::string_view text, std::size_t& offset) {
    const auto byteAt = [&text](std::size_t index) {
        return static_cast<std::uint8_t>(text[index]);
    };
    const std::uint8_t lead = byteAt(offset++);
    if (lead < 0x80U) return lead;
    std::uint32_t codepoint = 0;
    std::size_t continuationCount = 0;
    if ((lead & 0xE0U) == 0xC0U) {
        codepoint = lead & 0x1FU;
        continuationCount = 1U;
    } else if ((lead & 0xF0U) == 0xE0U) {
        codepoint = lead & 0x0FU;
        continuationCount = 2U;
    } else if ((lead & 0xF8U) == 0xF0U) {
        codepoint = lead & 0x07U;
        continuationCount = 3U;
    } else {
        return 0xFFFDU;
    }
    if (offset + continuationCount > text.size()) {
        offset = text.size();
        return 0xFFFDU;
    }
    for (std::size_t index = 0; index < continuationCount; ++index) {
        const std::uint8_t continuation = byteAt(offset);
        if ((continuation & 0xC0U) != 0x80U) return 0xFFFDU;
        ++offset;
        codepoint = (codepoint << 6U) | (continuation & 0x3FU);
    }
    if ((continuationCount == 1U && codepoint < 0x80U) ||
        (continuationCount == 2U && codepoint < 0x800U) ||
        (continuationCount == 3U && codepoint < 0x10000U) ||
        codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        return 0xFFFDU;
    }
    return codepoint;
}

constexpr float kDistanceInfinity = 1.0e20f;

void distanceTransform1D(const float* input, float* output, int length,
                          std::vector<int>& sites, std::vector<double>& boundaries) {
    int firstSite = -1;
    for (int index = 0; index < length; ++index) {
        if (input[index] < kDistanceInfinity * 0.5f) {
            firstSite = index;
            break;
        }
    }
    if (firstSite < 0) {
        std::fill_n(output, length, kDistanceInfinity);
        return;
    }

    int envelopeSize = 0;
    sites[0] = firstSite;
    boundaries[0] = -std::numeric_limits<double>::infinity();
    boundaries[1] = std::numeric_limits<double>::infinity();
    for (int index = firstSite + 1; index < length; ++index) {
        if (input[index] >= kDistanceInfinity * 0.5f) continue;
        double intersection = 0.0;
        while (true) {
            const int site = sites[static_cast<std::size_t>(envelopeSize)];
            const double indexPosition = static_cast<double>(index);
            const double sitePosition = static_cast<double>(site);
            intersection =
                (static_cast<double>(input[index]) + indexPosition * indexPosition -
                 static_cast<double>(input[site]) - sitePosition * sitePosition) /
                (2.0 * (indexPosition - sitePosition));
            if (intersection > boundaries[static_cast<std::size_t>(envelopeSize)] ||
                envelopeSize == 0) {
                break;
            }
            --envelopeSize;
        }
        ++envelopeSize;
        sites[static_cast<std::size_t>(envelopeSize)] = index;
        boundaries[static_cast<std::size_t>(envelopeSize)] = intersection;
        boundaries[static_cast<std::size_t>(envelopeSize + 1)] =
            std::numeric_limits<double>::infinity();
    }

    int activeSite = 0;
    for (int index = 0; index < length; ++index) {
        while (boundaries[static_cast<std::size_t>(activeSite + 1)] <
               static_cast<double>(index)) {
            ++activeSite;
        }
        const int site = sites[static_cast<std::size_t>(activeSite)];
        const double difference = static_cast<double>(index - site);
        output[index] = input[site] + static_cast<float>(difference * difference);
    }
}

void distanceTransform2D(std::vector<float>& distances, std::size_t width, std::size_t height,
                         std::vector<float>& line, std::vector<float>& transformed,
                         std::vector<int>& sites, std::vector<double>& boundaries) {
    for (std::size_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = y * width;
        std::copy_n(distances.data() + rowOffset, width, line.data());
        distanceTransform1D(line.data(), transformed.data(), static_cast<int>(width), sites,
                            boundaries);
        std::copy_n(transformed.data(), width, distances.data() + rowOffset);
    }
    for (std::size_t x = 0; x < width; ++x) {
        for (std::size_t y = 0; y < height; ++y) {
            line[y] = distances[y * width + x];
        }
        distanceTransform1D(line.data(), transformed.data(), static_cast<int>(height), sites,
                            boundaries);
        for (std::size_t y = 0; y < height; ++y) {
            distances[y * width + x] = transformed[y];
        }
    }
}

}  // namespace

bool coverageToSignedDistanceField(const std::uint8_t* coverage, int width, int height,
                                   int spreadPixels, std::vector<std::uint8_t>& output) {
    if (coverage == nullptr || width <= 0 || height <= 0 || spreadPixels <= 0) return false;
    const std::size_t widthSize = static_cast<std::size_t>(width);
    const std::size_t heightSize = static_cast<std::size_t>(height);
    if (widthSize > std::numeric_limits<std::size_t>::max() / heightSize ||
        width > std::numeric_limits<int>::max() - 2 ||
        height > std::numeric_limits<int>::max() - 2) {
        return false;
    }
    const std::size_t pixelCount = widthSize * heightSize;
    bool hasInside = false;
    for (std::size_t index = 0; index < pixelCount; ++index) {
        hasInside = hasInside || coverage[index] >= 128U;
    }
    output.resize(pixelCount);
    if (!hasInside) {
        std::fill(output.begin(), output.end(), 0U);
        return true;
    }
    const std::size_t paddedWidth = widthSize + 2U;
    const std::size_t paddedHeight = heightSize + 2U;
    if (paddedWidth > std::numeric_limits<std::size_t>::max() / paddedHeight) return false;
    const std::size_t paddedPixelCount = paddedWidth * paddedHeight;
    std::vector<float> distanceToInside(paddedPixelCount, kDistanceInfinity);
    std::vector<float> distanceToOutside(paddedPixelCount, kDistanceInfinity);
    for (std::size_t y = 0; y < paddedHeight; ++y) {
        distanceToOutside[y * paddedWidth] = 0.0f;
        distanceToOutside[y * paddedWidth + paddedWidth - 1U] = 0.0f;
    }
    for (std::size_t x = 0; x < paddedWidth; ++x) {
        distanceToOutside[x] = 0.0f;
        distanceToOutside[(paddedHeight - 1U) * paddedWidth + x] = 0.0f;
    }
    for (std::size_t y = 0; y < heightSize; ++y) {
        for (std::size_t x = 0; x < widthSize; ++x) {
            const std::size_t sourceIndex = y * widthSize + x;
            const std::size_t paddedIndex = (y + 1U) * paddedWidth + x + 1U;
            if (coverage[sourceIndex] >= 128U) distanceToInside[paddedIndex] = 0.0f;
            else distanceToOutside[paddedIndex] = 0.0f;
        }
    }
    const std::size_t workspaceSize = std::max(paddedWidth, paddedHeight);
    std::vector<float> line(workspaceSize);
    std::vector<float> transformed(workspaceSize);
    std::vector<int> sites(workspaceSize);
    std::vector<double> boundaries(workspaceSize + 1U);
    distanceTransform2D(distanceToInside, paddedWidth, paddedHeight, line, transformed, sites,
                        boundaries);
    distanceTransform2D(distanceToOutside, paddedWidth, paddedHeight, line, transformed, sites,
                        boundaries);

    const float spread = static_cast<float>(spreadPixels);
    for (std::size_t y = 0; y < heightSize; ++y) {
        for (std::size_t x = 0; x < widthSize; ++x) {
            const std::size_t outputIndex = y * widthSize + x;
            const std::size_t paddedIndex = (y + 1U) * paddedWidth + x + 1U;
            const float insideDistance = std::sqrt(distanceToInside[paddedIndex]);
            const float outsideDistance = std::sqrt(distanceToOutside[paddedIndex]);
            const float signedDistance = outsideDistance - insideDistance;
            const float normalized =
                std::clamp(0.5f + signedDistance / (2.0f * spread), 0.0f, 1.0f);
            output[outputIndex] = static_cast<std::uint8_t>(std::lround(normalized * 255.0f));
        }
    }
    return true;
}

bool DynamicFontAtlas::initialize(int width, int height, int lineHeight, int padding) {
    clear();
    if (width <= 0 || height <= 0 || lineHeight <= 0) return false;
    if (static_cast<std::size_t>(width) >
        std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height)) {
        return false;
    }
    width_ = width;
    height_ = height;
    lineHeight_ = lineHeight;
    padding_ = std::clamp(padding, 0, 16);
    cursorX_ = padding_;
    cursorY_ = padding_;
    rowHeight_ = 0;
    std::vector<std::uint8_t> emptyPixels(
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0U);
    if (!texture_.uploadR8(width_, height_, emptyPixels.data(), GL_LINEAR)) {
        width_ = 0;
        height_ = 0;
        lineHeight_ = 0;
        return false;
    }
    return true;
}

bool DynamicFontAtlas::addGlyph(const GlyphBitmap& bitmap) {
    if (texture_.id() == 0 || glyphs_.contains(bitmap.codepoint) ||
        bitmap.width < 0 || bitmap.height < 0 || bitmap.advance < 0) {
        return false;
    }
    const std::size_t glyphWidth = static_cast<std::size_t>(bitmap.width);
    const std::size_t glyphHeight = static_cast<std::size_t>(bitmap.height);
    if (glyphHeight != 0U && glyphWidth > std::numeric_limits<std::size_t>::max() / glyphHeight) {
        return false;
    }
    const std::size_t requiredPixels = glyphWidth * glyphHeight;
    if (bitmap.signedDistancePixels.size() != requiredPixels) return false;

    SdfGlyph glyph{};
    glyph.codepoint = bitmap.codepoint;
    glyph.width = bitmap.width;
    glyph.height = bitmap.height;
    glyph.bearingX = bitmap.bearingX;
    glyph.bearingY = bitmap.bearingY;
    glyph.advance = bitmap.advance;

    if (bitmap.width > 0 && bitmap.height > 0) {
        if (bitmap.width > width_ - padding_ * 2 || bitmap.height > height_ - padding_ * 2) {
            return false;
        }
        if (cursorX_ + bitmap.width + padding_ > width_) {
            cursorX_ = padding_;
            cursorY_ += rowHeight_ + padding_;
            rowHeight_ = 0;
        }
        if (cursorY_ + bitmap.height + padding_ > height_) return false;
        if (!texture_.updateR8(cursorX_, cursorY_, bitmap.width, bitmap.height,
                               bitmap.signedDistancePixels.data())) {
            return false;
        }
        glyph.uv = {static_cast<float>(cursorX_) / static_cast<float>(width_),
                    static_cast<float>(cursorY_ + bitmap.height) / static_cast<float>(height_),
                    static_cast<float>(cursorX_ + bitmap.width) / static_cast<float>(width_),
                    static_cast<float>(cursorY_) / static_cast<float>(height_)};
        cursorX_ += bitmap.width + padding_;
        rowHeight_ = std::max(rowHeight_, bitmap.height);
    }
    glyphs_.emplace(bitmap.codepoint, glyph);
    return true;
}

const SdfGlyph* DynamicFontAtlas::find(std::uint32_t codepoint) const noexcept {
    const auto found = glyphs_.find(codepoint);
    return found == glyphs_.end() ? nullptr : &found->second;
}

void DynamicFontAtlas::clear() noexcept {
    texture_.destroy();
    glyphs_.clear();
    width_ = 0;
    height_ = 0;
    lineHeight_ = 0;
    cursorX_ = 0;
    cursorY_ = 0;
    rowHeight_ = 0;
}

void DynamicFontAtlas::abandon() noexcept {
    texture_.abandon();
    glyphs_.clear();
    width_ = 0;
    height_ = 0;
    lineHeight_ = 0;
    cursorX_ = 0;
    cursorY_ = 0;
    rowHeight_ = 0;
}

SdfTextRenderer::SdfTextRenderer(std::size_t maximumGlyphs)
    : maximumGlyphs_(std::max<std::size_t>(maximumGlyphs, 1U)) {}

SdfTextRenderer::~SdfTextRenderer() { destroy(); }

bool SdfTextRenderer::initialize(AAssetManager* assets,
                                 const std::string& shaderOverrideDirectory) {
    if (!reloadShaders(assets, shaderOverrideDirectory)) return false;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0) {
        destroy();
        return false;
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const std::size_t maximumVertices = maximumGlyphs_ * 6U;
    const GLsizeiptr bufferBytes = static_cast<GLsizeiptr>(maximumVertices * sizeof(TextVertex));
    glBufferData(GL_ARRAY_BUFFER, bufferBytes, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                          reinterpret_cast<const void*>(sizeof(float) * 2U));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return !checkGlErrors("SDF text renderer initialization");
}

bool SdfTextRenderer::reloadShaders(AAssetManager* assets,
                                    const std::string& shaderOverrideDirectory) {
    std::string vertex = readTextAsset(assets, "shaders/sdf_text.vert", shaderOverrideDirectory);
    std::string fragment = readTextAsset(assets, "shaders/sdf_text.frag", shaderOverrideDirectory);
    if (vertex.empty()) vertex = kFallbackVertex;
    if (fragment.empty()) fragment = kFallbackFragment;
    return compileProgram(vertex, fragment);
}

void SdfTextRenderer::draw(const DynamicFontAtlas& font, std::string_view utf8Text, float x,
                           float baselineY, float scale, math::Vec4 color,
                           const math::Mat4& projection, float smoothing) {
    if (program_ == 0 || vao_ == 0 || vbo_ == 0 || font.texture().id() == 0 || scale <= 0.0f) {
        return;
    }

    const std::size_t maximumVertices = maximumGlyphs_ * 6U;
    std::vector<TextVertex> vertices;
    vertices.reserve(maximumVertices);
    float cursorX = x;
    float cursorY = baselineY;
    std::size_t offset = 0;
    const auto flushBatch = [&]() {
        if (vertices.empty()) return;
        flush(font, projection, color, smoothing, vertices);
        vertices.clear();
    };

    while (offset < utf8Text.size()) {
        const std::uint32_t codepoint = decodeUtf8(utf8Text, offset);
        if (codepoint == '\r') continue;
        if (codepoint == '\n') {
            cursorX = x;
            cursorY -= static_cast<float>(font.lineHeight()) * scale;
            continue;
        }
        const SdfGlyph* glyph = font.find(codepoint);
        if (glyph == nullptr) glyph = font.find(static_cast<std::uint32_t>('?'));
        if (glyph == nullptr) continue;

        if (glyph->width > 0 && glyph->height > 0) {
            if (vertices.size() + 6U > maximumVertices) flushBatch();
            const float left = cursorX + static_cast<float>(glyph->bearingX) * scale;
            const float bottom = cursorY - static_cast<float>(glyph->height - glyph->bearingY) * scale;
            const float right = left + static_cast<float>(glyph->width) * scale;
            const float top = bottom + static_cast<float>(glyph->height) * scale;
            const UvRect uv = glyph->uv;
            const TextVertex quad[6] = {
                {left, bottom, uv.left, uv.bottom}, {right, bottom, uv.right, uv.bottom},
                {right, top, uv.right, uv.top}, {left, bottom, uv.left, uv.bottom},
                {right, top, uv.right, uv.top}, {left, top, uv.left, uv.top},
            };
            vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
        }
        cursorX += static_cast<float>(glyph->advance) * scale;
    }
    flushBatch();
}

void SdfTextRenderer::destroy() noexcept {
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (program_ != 0) glDeleteProgram(program_);
    abandon();
}

void SdfTextRenderer::abandon() noexcept {
    program_ = 0;
    vao_ = 0;
    vbo_ = 0;
    projectionLocation_ = -1;
    colorLocation_ = -1;
    smoothingLocation_ = -1;
    textureLocation_ = -1;
}

bool SdfTextRenderer::compileProgram(const std::string& vertexSource,
                                     const std::string& fragmentSource) {
    const GLuint vertexShader = compileStage(GL_VERTEX_SHADER, vertexSource);
    if (vertexShader == 0) return false;
    const GLuint fragmentShader = compileStage(GL_FRAGMENT_SHADER, fragmentSource);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return false;
    }
    const GLuint candidate = glCreateProgram();
    if (candidate == 0) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    glAttachShader(candidate, vertexShader);
    glAttachShader(candidate, fragmentShader);
    glLinkProgram(candidate);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(candidate, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(candidate, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> message(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetProgramInfoLog(candidate, static_cast<GLsizei>(message.size()), nullptr, message.data());
        BOX_LOG_ERROR("SDF text program link failed: %s", message.data());
        glDeleteProgram(candidate);
        return false;
    }

    const GLint projection = glGetUniformLocation(candidate, "uProjection");
    const GLint color = glGetUniformLocation(candidate, "uTextColor");
    const GLint smoothing = glGetUniformLocation(candidate, "uSmoothing");
    const GLint texture = glGetUniformLocation(candidate, "uFontAtlas");
    if (projection < 0 || color < 0 || smoothing < 0 || texture < 0) {
        glDeleteProgram(candidate);
        return false;
    }
    if (program_ != 0) glDeleteProgram(program_);
    program_ = candidate;
    projectionLocation_ = projection;
    colorLocation_ = color;
    smoothingLocation_ = smoothing;
    textureLocation_ = texture;
    return true;
}

void SdfTextRenderer::flush(const DynamicFontAtlas& font, const math::Mat4& projection,
                            math::Vec4 color, float smoothing,
                            const std::vector<TextVertex>& vertices) {
    if (vertices.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, projection.values.data());
    glUniform4f(colorLocation_, color.x, color.y, color.z, color.w);
    glUniform1f(smoothingLocation_, std::clamp(smoothing, 0.001f, 0.25f));
    glUniform1i(textureLocation_, 0);
    font.texture().bind(0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizeiptr byteCount = static_cast<GLsizeiptr>(vertices.size() * sizeof(TextVertex));
    glBufferSubData(GL_ARRAY_BUFFER, 0, byteCount, vertices.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
}

}  // namespace box::renderer
