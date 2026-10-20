#include "box/renderer/texture.hpp"

#include "box/core/log.hpp"
#include "box/renderer/gl_utils.hpp"

namespace box::renderer {
namespace {

void setTextureParameters(GLenum target, GLint minFilter, GLint magFilter) {
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (target == GL_TEXTURE_CUBE_MAP) {
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
}

[[nodiscard]] bool isEtc2Format(GLenum format) {
    return format == GL_COMPRESSED_RGB8_ETC2 ||
           format == GL_COMPRESSED_SRGB8_ETC2 ||
           format == GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 ||
           format == GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2 ||
           format == GL_COMPRESSED_RGBA8_ETC2_EAC ||
           format == GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC ||
           format == GL_COMPRESSED_R11_EAC ||
           format == GL_COMPRESSED_SIGNED_R11_EAC ||
           format == GL_COMPRESSED_RG11_EAC ||
           format == GL_COMPRESSED_SIGNED_RG11_EAC;
}

}  // namespace

Texture2D::~Texture2D() { destroy(); }

bool Texture2D::uploadRgba8(GLsizei width, GLsizei height, const void* pixels,
                            GLint minFilter, GLint magFilter) {
    if (width <= 0 || height <= 0 || pixels == nullptr) {
        BOX_LOG_ERROR("Invalid RGBA8 texture upload dimensions or pixel pointer.");
        return false;
    }
    destroy();
    glGenTextures(1, &texture_);
    if (texture_ == 0) return false;

    glBindTexture(GL_TEXTURE_2D, texture_);
    setTextureParameters(GL_TEXTURE_2D, minFilter, magFilter);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (checkGlErrors("RGBA8 texture upload")) {
        destroy();
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

bool Texture2D::uploadR8(GLsizei width, GLsizei height, const std::uint8_t* pixels,
                         GLint minFilter) {
    if (width <= 0 || height <= 0 || pixels == nullptr) {
        BOX_LOG_ERROR("Invalid R8 texture upload dimensions or pixel pointer.");
        return false;
    }
    destroy();
    glGenTextures(1, &texture_);
    if (texture_ == 0) return false;

    glBindTexture(GL_TEXTURE_2D, texture_);
    setTextureParameters(GL_TEXTURE_2D, minFilter, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
                 GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (checkGlErrors("R8 texture upload")) {
        destroy();
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

bool Texture2D::updateR8(GLint x, GLint y, GLsizei width, GLsizei height,
                         const std::uint8_t* pixels) {
    if (texture_ == 0 || width <= 0 || height <= 0 || pixels == nullptr ||
        x < 0 || y < 0 || width > width_ || height > height_ ||
        x > width_ - width || y > height_ - height) {
        BOX_LOG_ERROR("Invalid R8 texture sub-image update.");
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RED,
                    GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return !checkGlErrors("R8 texture sub-image update");
}

bool Texture2D::uploadEtc2(GLsizei width, GLsizei height, GLenum internalFormat,
                           GLsizei imageSize, const void* data, GLint minFilter) {
    if (width <= 0 || height <= 0 || imageSize <= 0 || data == nullptr ||
        !isEtc2Format(internalFormat)) {
        BOX_LOG_ERROR("Invalid ETC2 texture upload parameters or unsupported ETC2 format 0x%04X.",
                      static_cast<unsigned int>(internalFormat));
        return false;
    }
    destroy();
    glGenTextures(1, &texture_);
    if (texture_ == 0) return false;

    glBindTexture(GL_TEXTURE_2D, texture_);
    setTextureParameters(GL_TEXTURE_2D, minFilter, GL_LINEAR);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                           imageSize, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (checkGlErrors("ETC2 compressed texture upload")) {
        destroy();
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void Texture2D::destroy() noexcept {
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    abandon();
}

void Texture2D::abandon() noexcept {
    texture_ = 0;
    width_ = 0;
    height_ = 0;
}

void Texture2D::bind(GLuint unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture_);
}

CubeMapTexture::~CubeMapTexture() { destroy(); }

bool CubeMapTexture::uploadRgba8(const std::array<CubeFaceData, 6>& faces,
                                 GLint minFilter, GLint magFilter) {
    const GLsizei width = faces[0].width;
    const GLsizei height = faces[0].height;
    if (width <= 0 || height <= 0 || width != height) {
        BOX_LOG_ERROR("Cubemap faces must have positive, square dimensions.");
        return false;
    }
    for (const CubeFaceData& face : faces) {
        if (face.pixels == nullptr || face.width != width || face.height != height) {
            BOX_LOG_ERROR("Cubemap faces must all contain non-null, equal-size RGBA8 images.");
            return false;
        }
    }

    destroy();
    glGenTextures(1, &texture_);
    if (texture_ == 0) return false;
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
    setTextureParameters(GL_TEXTURE_CUBE_MAP, minFilter, magFilter);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(faceIndex), 0,
                     GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     faces[faceIndex].pixels);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    if (checkGlErrors("cubemap RGBA8 upload")) {
        destroy();
        return false;
    }
    return true;
}

void CubeMapTexture::destroy() noexcept {
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    abandon();
}

void CubeMapTexture::abandon() noexcept { texture_ = 0; }

void CubeMapTexture::bind(GLuint unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
}

SamplerObject::~SamplerObject() { destroy(); }

bool SamplerObject::initialize(GLint minFilter, GLint magFilter, GLint wrapS, GLint wrapT,
                               GLint wrapR) {
    destroy();
    glGenSamplers(1, &sampler_);
    if (sampler_ == 0) return false;
    glSamplerParameteri(sampler_, GL_TEXTURE_MIN_FILTER, minFilter);
    glSamplerParameteri(sampler_, GL_TEXTURE_MAG_FILTER, magFilter);
    glSamplerParameteri(sampler_, GL_TEXTURE_WRAP_S, wrapS);
    glSamplerParameteri(sampler_, GL_TEXTURE_WRAP_T, wrapT);
    glSamplerParameteri(sampler_, GL_TEXTURE_WRAP_R, wrapR);
    if (checkGlErrors("sampler object initialization")) {
        destroy();
        return false;
    }
    return true;
}

void SamplerObject::destroy() noexcept {
    if (sampler_ != 0) glDeleteSamplers(1, &sampler_);
    abandon();
}

void SamplerObject::abandon() noexcept { sampler_ = 0; }

void SamplerObject::bind(GLuint unit) const noexcept { glBindSampler(unit, sampler_); }

}  // namespace box::renderer
