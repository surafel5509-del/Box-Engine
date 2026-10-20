#include "box/renderer/render_target.hpp"

#include "box/core/log.hpp"
#include "box/renderer/gl_utils.hpp"

namespace box::renderer {

RenderTarget::~RenderTarget() { destroy(); }

bool RenderTarget::resize(GLsizei width, GLsizei height) {
    if (width <= 0 || height <= 0) return false;
    if (valid() && width_ == width && height_ == height) return true;

    destroy();
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &colorTexture_);
    glGenTextures(1, &depthTexture_);
    if (framebuffer_ == 0 || colorTexture_ == 0 || depthTexture_ == 0) {
        BOX_LOG_ERROR("Failed to allocate framebuffer resources.");
        destroy();
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, depthTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           colorTexture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depthTexture_, 0);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        BOX_LOG_ERROR("Framebuffer is incomplete (status 0x%04X).",
                      static_cast<unsigned int>(status));
        destroy();
        return false;
    }
    if (checkGlErrors("render target allocation")) {
        destroy();
        return false;
    }

    width_ = width;
    height_ = height;
    return true;
}

void RenderTarget::bind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
}

void RenderTarget::blitToDefault(GLsizei destinationWidth,
                                 GLsizei destinationHeight) const noexcept {
    if (!valid() || destinationWidth <= 0 || destinationHeight <= 0) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, destinationWidth, destinationHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::destroy() noexcept {
    if (depthTexture_ != 0) glDeleteTextures(1, &depthTexture_);
    if (colorTexture_ != 0) glDeleteTextures(1, &colorTexture_);
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    abandon();
}

void RenderTarget::abandon() noexcept {
    framebuffer_ = 0;
    colorTexture_ = 0;
    depthTexture_ = 0;
    width_ = 0;
    height_ = 0;
}

}  // namespace box::renderer
