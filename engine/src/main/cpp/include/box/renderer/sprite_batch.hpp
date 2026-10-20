// Path: engine/src/main/cpp/include/box/renderer/sprite_batch.hpp
#pragma once

#include <GLES3/gl3.h>

#include <array>
#include <cstddef>
#include <vector>

#include "box/math/math.hpp"
#include "box/renderer/shader_program.hpp"
#include "box/renderer/texture.hpp"

namespace box::renderer {

/** Interleaved, streaming vertex format used by colored triangles and batched sprites. */
struct SpriteVertex {
    float x;
    float y;
    float u;
    float v;
    float red;
    float green;
    float blue;
    float alpha;
};

/** OpenGL texture rectangle; values may be descending for vertically top-down image sources. */
struct UvRect {
    float left = 0.0f;
    float bottom = 0.0f;
    float right = 1.0f;
    float top = 1.0f;
};

/** Batches consecutive sprites that share a texture into one GLES draw call. */
class SpriteBatch final {
public:
    explicit SpriteBatch(std::size_t maximumSprites = 4096);
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    [[nodiscard]] bool initialize();
    void begin(const ShaderProgram& shader, const math::Mat4& projection);
    void drawTriangle(const std::array<math::Vec2, 3>& points,
                      const std::array<math::Vec4, 3>& colors);
    void drawSprite(const Texture2D& texture, float x, float y, float width, float height,
                    math::Vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});
    void drawSpriteRegion(const Texture2D& texture, float x, float y, float width, float height,
                          UvRect uv,
                          math::Vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});
    void drawSolidQuad(float x, float y, float width, float height, math::Vec4 color);
    void flush();
    void resetDrawCallCount() noexcept { drawCallCount_ = 0U; }
    [[nodiscard]] std::size_t drawCallCount() const noexcept { return drawCallCount_; }
    void destroy() noexcept;
    void abandon() noexcept;

private:
    enum class BatchMode { None, Solid, Textured };

    void setMode(BatchMode mode, GLuint texture, std::size_t verticesNeeded);
    void append(const SpriteVertex& vertex);

    const ShaderProgram* shader_ = nullptr;
    std::vector<SpriteVertex> vertices_;
    std::size_t maximumVertices_ = 0;
    std::size_t drawCallCount_ = 0U;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ibo_ = 0;
    GLuint batchTexture_ = 0;
    BatchMode mode_ = BatchMode::None;
};

}  // namespace box::renderer
