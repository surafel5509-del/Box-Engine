// Path: engine/src/main/cpp/src/renderer/sprite_batch.cpp
#include "box/renderer/sprite_batch.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>

#include "box/core/log.hpp"
#include "box/renderer/gl_utils.hpp"

namespace box::renderer {

SpriteBatch::SpriteBatch(std::size_t maximumSprites)
    : maximumVertices_(std::max<std::size_t>(maximumSprites, 1U) * 6U) {
    vertices_.reserve(maximumVertices_);
}

SpriteBatch::~SpriteBatch() { destroy(); }

bool SpriteBatch::initialize() {
    if (vao_ != 0 && vbo_ != 0 && ibo_ != 0) return true;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    if (vao_ == 0 || vbo_ == 0 || ibo_ == 0) {
        BOX_LOG_ERROR("Could not allocate sprite batch VAO/VBO/IBO.");
        destroy();
        return false;
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizeiptr bufferBytes =
        static_cast<GLsizeiptr>(maximumVertices_ * sizeof(SpriteVertex));
    glBufferData(GL_ARRAY_BUFFER, bufferBytes, nullptr, GL_STREAM_DRAW);

    std::vector<GLuint> indices(maximumVertices_);
    for (std::size_t index = 0; index < indices.size(); ++index) {
        indices[index] = static_cast<GLuint>(index);
    }
    const GLsizeiptr indexBufferBytes =
        static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBufferBytes, indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<const void*>(offsetof(SpriteVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<const void*>(offsetof(SpriteVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<const void*>(offsetof(SpriteVertex, red)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return !checkGlErrors("sprite batch initialization");
}

void SpriteBatch::begin(const ShaderProgram& shader, const math::Mat4& projection) {
    flush();
    shader_ = &shader;
    shader_->use();
    shader_->setProjection(projection);
    shader_->setTextureUnit(0);
    mode_ = BatchMode::None;
    batchTexture_ = 0;
}

void SpriteBatch::drawTriangle(const std::array<math::Vec2, 3>& points,
                               const std::array<math::Vec4, 3>& colors) {
    setMode(BatchMode::Solid, 0, 3U);
    for (std::size_t index = 0; index < points.size(); ++index) {
        append({points[index].x, points[index].y, 0.0f, 0.0f,
                colors[index].x, colors[index].y, colors[index].z, colors[index].w});
    }
}

void SpriteBatch::drawSprite(const Texture2D& texture, float x, float y, float width,
                             float height, math::Vec4 tint) {
    drawSpriteRegion(texture, x, y, width, height, {}, tint);
}

void SpriteBatch::drawSpriteRegion(const Texture2D& texture, float x, float y, float width,
                                   float height, UvRect uv, math::Vec4 tint) {
    if (texture.id() == 0 || width <= 0.0f || height <= 0.0f) return;
    setMode(BatchMode::Textured, texture.id(), 6U);
    const float right = x + width;
    const float top = y + height;
    const SpriteVertex vertices[6] = {
        {x, y, uv.left, uv.bottom, tint.x, tint.y, tint.z, tint.w},
        {right, y, uv.right, uv.bottom, tint.x, tint.y, tint.z, tint.w},
        {right, top, uv.right, uv.top, tint.x, tint.y, tint.z, tint.w},
        {x, y, uv.left, uv.bottom, tint.x, tint.y, tint.z, tint.w},
        {right, top, uv.right, uv.top, tint.x, tint.y, tint.z, tint.w},
        {x, top, uv.left, uv.top, tint.x, tint.y, tint.z, tint.w},
    };
    for (const SpriteVertex& vertex : vertices) append(vertex);
}

void SpriteBatch::drawSolidQuad(float x, float y, float width, float height,
                                math::Vec4 color) {
    if (width <= 0.0f || height <= 0.0f) return;
    setMode(BatchMode::Solid, 0, 6U);
    const float right = x + width;
    const float top = y + height;
    const SpriteVertex vertices[6] = {
        {x, y, 0.0f, 0.0f, color.x, color.y, color.z, color.w},
        {right, y, 0.0f, 0.0f, color.x, color.y, color.z, color.w},
        {right, top, 0.0f, 0.0f, color.x, color.y, color.z, color.w},
        {x, y, 0.0f, 0.0f, color.x, color.y, color.z, color.w},
        {right, top, 0.0f, 0.0f, color.x, color.y, color.z, color.w},
        {x, top, 0.0f, 0.0f, color.x, color.y, color.z, color.w},
    };
    for (const SpriteVertex& vertex : vertices) append(vertex);
}

void SpriteBatch::flush() {
    if (vertices_.empty()) return;
    if (shader_ == nullptr || vao_ == 0 || vbo_ == 0 || ibo_ == 0) {
        vertices_.clear();
        mode_ = BatchMode::None;
        return;
    }

    shader_->use();
    const bool textured = mode_ == BatchMode::Textured;
    shader_->setTextured(textured);
    if (textured) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, batchTexture_);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    const GLsizeiptr byteCount =
        static_cast<GLsizeiptr>(vertices_.size() * sizeof(SpriteVertex));
    glBufferSubData(GL_ARRAY_BUFFER, 0, byteCount, vertices_.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(vertices_.size()), GL_UNSIGNED_INT, nullptr);
    ++drawCallCount_;
    glBindVertexArray(0);

    vertices_.clear();
    mode_ = BatchMode::None;
    batchTexture_ = 0;
}

void SpriteBatch::destroy() noexcept {
    if (ibo_ != 0) glDeleteBuffers(1, &ibo_);
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    abandon();
}

void SpriteBatch::abandon() noexcept {
    vao_ = 0;
    vbo_ = 0;
    ibo_ = 0;
    vertices_.clear();
    shader_ = nullptr;
    batchTexture_ = 0;
    mode_ = BatchMode::None;
    drawCallCount_ = 0U;
}

void SpriteBatch::setMode(BatchMode mode, GLuint texture, std::size_t verticesNeeded) {
    const bool textureChanged = mode == BatchMode::Textured && batchTexture_ != texture;
    if (mode_ != mode || textureChanged || vertices_.size() + verticesNeeded > maximumVertices_) {
        flush();
        mode_ = mode;
        batchTexture_ = texture;
    }
}

void SpriteBatch::append(const SpriteVertex& vertex) {
    if (vertices_.size() >= maximumVertices_) flush();
    vertices_.push_back(vertex);
}

}  // namespace box::renderer
