#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>

namespace box::renderer {

/** Owns an EGL 1.4 display, GLES 3.x-compatible context, and current Android window surface. */
class EglContext final {
public:
    EglContext() = default;
    ~EglContext();

    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;

    [[nodiscard]] bool initialize(ANativeWindow* window);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool swapBuffers() noexcept;

    [[nodiscard]] EGLDisplay display() const noexcept { return display_; }
    [[nodiscard]] EGLContext context() const noexcept { return context_; }
    [[nodiscard]] EGLSurface surface() const noexcept { return surface_; }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
};

}  // namespace box::renderer
