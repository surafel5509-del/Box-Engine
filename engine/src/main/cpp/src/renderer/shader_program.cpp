#include "box/renderer/shader_program.hpp"

#include <vector>

#include "box/core/log.hpp"
#include "box/renderer/asset_io.hpp"

namespace box::renderer {
namespace {

constexpr char kFallbackVertexShader[] = R"glsl(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vTexCoord;
out vec4 vColor;
void main() {
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)glsl";

constexpr char kFallbackFragmentShader[] = R"glsl(#version 300 es
precision mediump float;
in vec2 vTexCoord;
in vec4 vColor;
uniform sampler2D uTexture;
uniform int uUseTexture;
out vec4 fragColor;
void main() {
    vec4 texel = (uUseTexture == 1) ? texture(uTexture, vTexCoord) : vec4(1.0);
    fragColor = texel * vColor;
}
)glsl";

[[nodiscard]] GLuint compileShader(GLenum shaderType, const std::string& source) {
    const GLuint shader = glCreateShader(shaderType);
    if (shader == 0) {
        BOX_LOG_ERROR("glCreateShader failed for type 0x%04X", static_cast<unsigned int>(shaderType));
        return 0;
    }

    const GLchar* sourcePointer = source.c_str();
    const GLint sourceLength = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &sourcePointer, &sourceLength);
    glCompileShader(shader);

    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_TRUE) return shader;

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<GLchar> log(static_cast<std::size_t>(logLength > 1 ? logLength : 1));
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    BOX_LOG_ERROR("Shader compilation failed: %s", log.data());
    glDeleteShader(shader);
    return 0;
}

}  // namespace

ShaderProgram::~ShaderProgram() { destroy(); }

bool ShaderProgram::load(AAssetManager* assets, const std::string& overrideDirectory) {
    std::string vertexSource = readTextAsset(assets, "shaders/sprite.vert", overrideDirectory);
    std::string fragmentSource = readTextAsset(assets, "shaders/sprite.frag", overrideDirectory);
    if (vertexSource.empty()) vertexSource = kFallbackVertexShader;
    if (fragmentSource.empty()) fragmentSource = kFallbackFragmentShader;
    return compileAndInstall(vertexSource, fragmentSource);
}

bool ShaderProgram::reload(AAssetManager* assets, const std::string& overrideDirectory) {
    std::string vertexSource = readTextAsset(assets, "shaders/sprite.vert", overrideDirectory);
    std::string fragmentSource = readTextAsset(assets, "shaders/sprite.frag", overrideDirectory);
    if (vertexSource.empty() || fragmentSource.empty()) {
        BOX_LOG_WARN("Shader reload skipped because an asset or override is empty.");
        return false;
    }
    return compileAndInstall(vertexSource, fragmentSource);
}

bool ShaderProgram::compileAndInstall(const std::string& vertexSource,
                                      const std::string& fragmentSource) {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (vertexShader == 0) return false;
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return false;
    }

    const GLuint candidateProgram = glCreateProgram();
    if (candidateProgram == 0) {
        BOX_LOG_ERROR("glCreateProgram failed.");
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    glAttachShader(candidateProgram, vertexShader);
    glAttachShader(candidateProgram, fragmentShader);
    glLinkProgram(candidateProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(candidateProgram, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(candidateProgram, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> log(static_cast<std::size_t>(logLength > 1 ? logLength : 1));
        glGetProgramInfoLog(candidateProgram, static_cast<GLsizei>(log.size()), nullptr, log.data());
        BOX_LOG_ERROR("Shader program link failed: %s", log.data());
        glDeleteProgram(candidateProgram);
        return false;
    }

    const GLint projectionLocation = glGetUniformLocation(candidateProgram, "uProjection");
    const GLint texturedLocation = glGetUniformLocation(candidateProgram, "uUseTexture");
    const GLint textureLocation = glGetUniformLocation(candidateProgram, "uTexture");
    if (projectionLocation < 0 || texturedLocation < 0 || textureLocation < 0) {
        BOX_LOG_ERROR("Shader program is missing a required uniform.");
        glDeleteProgram(candidateProgram);
        return false;
    }

    if (program_ != 0) glDeleteProgram(program_);
    program_ = candidateProgram;
    projectionLocation_ = projectionLocation;
    texturedLocation_ = texturedLocation;
    textureLocation_ = textureLocation;
    BOX_LOG_INFO("GLES 3.0 sprite shader compiled and linked.");
    return true;
}

void ShaderProgram::destroy() noexcept {
    if (program_ != 0) glDeleteProgram(program_);
    abandon();
}

void ShaderProgram::abandon() noexcept {
    program_ = 0;
    projectionLocation_ = -1;
    texturedLocation_ = -1;
    textureLocation_ = -1;
}

void ShaderProgram::use() const noexcept {
    if (program_ != 0) glUseProgram(program_);
}

void ShaderProgram::setProjection(const math::Mat4& projection) const noexcept {
    if (projectionLocation_ >= 0) {
        glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, projection.values.data());
    }
}

void ShaderProgram::setTextured(bool textured) const noexcept {
    if (texturedLocation_ >= 0) glUniform1i(texturedLocation_, textured ? 1 : 0);
}

void ShaderProgram::setTextureUnit(GLint unit) const noexcept {
    if (textureLocation_ >= 0) glUniform1i(textureLocation_, unit);
}

}  // namespace box::renderer
