#pragma once

#include <GLES3/gl3.h>

#include <array>
#include <cstdint>

namespace box::renderer {

/** GLES 3.0 2D texture supporting uncompressed RGBA8 and ETC2 compressed uploads. */
class Texture2D final {
public:
    Texture2D() = default;
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    [[nodiscard]] bool uploadRgba8(GLsizei width, GLsizei height, const void* pixels,
                                   GLint minFilter = GL_LINEAR,
                                   GLint magFilter = GL_LINEAR);
    [[nodiscard]] bool uploadR8(GLsizei width, GLsizei height, const std::uint8_t* pixels,
                                GLint minFilter = GL_LINEAR);
    [[nodiscard]] bool updateR8(GLint x, GLint y, GLsizei width, GLsizei height,
                                const std::uint8_t* pixels);
    [[nodiscard]] bool uploadEtc2(GLsizei width, GLsizei height, GLenum internalFormat,
                                  GLsizei imageSize, const void* data,
                                  GLint minFilter = GL_LINEAR);
    void destroy() noexcept;
    void abandon() noexcept;
    void bind(GLuint unit = 0) const noexcept;

    [[nodiscard]] GLuint id() const noexcept { return texture_; }
    [[nodiscard]] GLsizei width() const noexcept { return width_; }
    [[nodiscard]] GLsizei height() const noexcept { return height_; }

private:
    GLuint texture_ = 0;
    GLsizei width_ = 0;
    GLsizei height_ = 0;
};

struct CubeFaceData {
    const void* pixels = nullptr;
    GLsizei width = 0;
    GLsizei height = 0;
};

/** Six-face RGBA8 cubemap texture. Face order follows GLES cube-map target order. */
class CubeMapTexture final {
public:
    CubeMapTexture() = default;
    ~CubeMapTexture();

    CubeMapTexture(const CubeMapTexture&) = delete;
    CubeMapTexture& operator=(const CubeMapTexture&) = delete;

    [[nodiscard]] bool uploadRgba8(const std::array<CubeFaceData, 6>& faces,
                                   GLint minFilter = GL_LINEAR,
                                   GLint magFilter = GL_LINEAR);
    void destroy() noexcept;
    void abandon() noexcept;
    void bind(GLuint unit = 0) const noexcept;
    [[nodiscard]] GLuint id() const noexcept { return texture_; }

private:
    GLuint texture_ = 0;
};

/** GLES 3.0 sampler object; texture state remains independent of sampler state. */
class SamplerObject final {
public:
    SamplerObject() = default;
    ~SamplerObject();

    SamplerObject(const SamplerObject&) = delete;
    SamplerObject& operator=(const SamplerObject&) = delete;

    [[nodiscard]] bool initialize(GLint minFilter = GL_LINEAR,
                                  GLint magFilter = GL_LINEAR,
                                  GLint wrapS = GL_CLAMP_TO_EDGE,
                                  GLint wrapT = GL_CLAMP_TO_EDGE,
                                  GLint wrapR = GL_CLAMP_TO_EDGE);
    void destroy() noexcept;
    void abandon() noexcept;
    void bind(GLuint unit) const noexcept;
    [[nodiscard]] GLuint id() const noexcept { return sampler_; }

private:
    GLuint sampler_ = 0;
};

}  // namespace box::renderer
