#pragma once

#include <GLES3/gl3.h>

#include <string>

#include <android/asset_manager.h>

#include "box/math/math.hpp"

namespace box::renderer {

/** Compiles GLES 3.0 shaders and swaps programs only after a successful reload. */
class ShaderProgram final {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    [[nodiscard]] bool load(AAssetManager* assets, const std::string& overrideDirectory);
    [[nodiscard]] bool reload(AAssetManager* assets, const std::string& overrideDirectory);
    void destroy() noexcept;
    void abandon() noexcept;

    void use() const noexcept;
    void setProjection(const math::Mat4& projection) const noexcept;
    void setTextured(bool textured) const noexcept;
    void setTextureUnit(GLint unit) const noexcept;
    [[nodiscard]] GLuint id() const noexcept { return program_; }

private:
    [[nodiscard]] bool compileAndInstall(const std::string& vertexSource,
                                         const std::string& fragmentSource);
    GLuint program_ = 0;
    GLint projectionLocation_ = -1;
    GLint texturedLocation_ = -1;
    GLint textureLocation_ = -1;
};

}  // namespace box::renderer
