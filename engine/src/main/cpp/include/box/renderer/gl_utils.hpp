#pragma once

#include <GLES3/gl3.h>

#include "box/core/log.hpp"

namespace box::renderer {

/** Drains and logs the OpenGL ES error queue. Returns true when an error was found. */
inline bool checkGlErrors(const char* operation) {
    bool foundError = false;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        BOX_LOG_ERROR("OpenGL ES error 0x%04X after %s", static_cast<unsigned int>(error), operation);
        foundError = true;
    }
    return foundError;
}

}  // namespace box::renderer
