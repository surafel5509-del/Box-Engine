#pragma once

#include <GLES3/gl3.h>

namespace box::renderer {

/** Off-screen RGBA8 + DEPTH_COMPONENT24 render target with an ES 3.0 framebuffer blit. */
class RenderTarget final {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    [[nodiscard]] bool resize(GLsizei width, GLsizei height);
    void bind() const noexcept;
    void blitToDefault(GLsizei destinationWidth, GLsizei destinationHeight) const noexcept;
    void destroy() noexcept;
    void abandon() noexcept;

    [[nodiscard]] GLuint framebuffer() const noexcept { return framebuffer_; }
    [[nodiscard]] GLuint colorTexture() const noexcept { return colorTexture_; }
    [[nodiscard]] GLsizei width() const noexcept { return width_; }
    [[nodiscard]] GLsizei height() const noexcept { return height_; }
    [[nodiscard]] bool valid() const noexcept { return framebuffer_ != 0; }

private:
    GLuint framebuffer_ = 0;
    GLuint colorTexture_ = 0;
    GLuint depthTexture_ = 0;
    GLsizei width_ = 0;
    GLsizei height_ = 0;
};

}  // namespace box::renderer
