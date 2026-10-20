#include "box/renderer/egl_context.hpp"

#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cstring>

#include "box/core/log.hpp"

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION_KHR
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif

namespace box::renderer {
namespace {

[[nodiscard]] bool hasExtension(const char* extensionList, const char* extension) {
    if (extensionList == nullptr || extension == nullptr || *extension == '\0') return false;
    const std::size_t requestedLength = std::strlen(extension);
    const char* current = extensionList;
    while ((current = std::strstr(current, extension)) != nullptr) {
        const bool startsAtBoundary = current == extensionList || current[-1] == ' ';
        const char following = current[requestedLength];
        if (startsAtBoundary && (following == ' ' || following == '\0')) return true;
        current += requestedLength;
    }
    return false;
}

}  // namespace

EglContext::~EglContext() { shutdown(); }

bool EglContext::initialize(ANativeWindow* window) {
    shutdown();
    if (window == nullptr) {
        BOX_LOG_ERROR("EGL initialization requested without an Android window.");
        return false;
    }

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        BOX_LOG_ERROR("eglGetDisplay failed: 0x%04X", eglGetError());
        shutdown();
        return false;
    }

    EGLint majorVersion = 0;
    EGLint minorVersion = 0;
    if (eglInitialize(display_, &majorVersion, &minorVersion) != EGL_TRUE) {
        BOX_LOG_ERROR("eglInitialize failed: 0x%04X", eglGetError());
        shutdown();
        return false;
    }
    BOX_LOG_INFO("Initialized EGL %d.%d.", majorVersion, minorVersion);

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        BOX_LOG_ERROR("eglBindAPI(EGL_OPENGL_ES_API) failed: 0x%04X", eglGetError());
        shutdown();
        return false;
    }

    constexpr EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE,
    };
    EGLint configCount = 0;
    if (eglChooseConfig(display_, configAttributes, &config_, 1, &configCount) != EGL_TRUE ||
        configCount == 0) {
        BOX_LOG_ERROR("No EGL window config supporting OpenGL ES 3.0 was found: 0x%04X",
                      eglGetError());
        shutdown();
        return false;
    }

    const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
    constexpr EGLint exactGles30Attributes[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 0,
        EGL_NONE,
    };
    constexpr EGLint legacyGles3Attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    const bool supportsCreateContext = hasExtension(extensions, "EGL_KHR_create_context");
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT,
                                supportsCreateContext ? exactGles30Attributes : legacyGles3Attributes);
    if (context_ == EGL_NO_CONTEXT) {
        BOX_LOG_ERROR("eglCreateContext for GLES 3.0 failed: 0x%04X", eglGetError());
        shutdown();
        return false;
    }

    surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        BOX_LOG_ERROR("eglCreateWindowSurface failed: 0x%04X", eglGetError());
        shutdown();
        return false;
    }

    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
        BOX_LOG_ERROR("eglMakeCurrent failed: 0x%04X", eglGetError());
        shutdown();
        return false;
    }

    if (eglSwapInterval(display_, 1) != EGL_TRUE) {
        BOX_LOG_WARN("Could not set EGL swap interval to one: 0x%04X", eglGetError());
    }

    const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    BOX_LOG_INFO("GLES context ready. Version: %s; renderer: %s",
                 glVersion != nullptr ? glVersion : "unknown",
                 glRenderer != nullptr ? glRenderer : "unknown");
    return true;
}

void EglContext::shutdown() noexcept {
    if (display_ != EGL_NO_DISPLAY) {
        if (context_ != EGL_NO_CONTEXT) {
            (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (surface_ != EGL_NO_SURFACE) {
            (void)eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        if (context_ != EGL_NO_CONTEXT) {
            (void)eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        (void)eglTerminate(display_);
        (void)eglReleaseThread();
    }
    display_ = EGL_NO_DISPLAY;
    config_ = nullptr;
}

bool EglContext::ready() const noexcept {
    return display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT &&
           surface_ != EGL_NO_SURFACE;
}

bool EglContext::swapBuffers() noexcept {
    if (!ready()) return false;
    if (eglSwapBuffers(display_, surface_) == EGL_TRUE) return true;
    const EGLint error = eglGetError();
    BOX_LOG_WARN("eglSwapBuffers failed: 0x%04X", error);
    return false;
}

}  // namespace box::renderer
