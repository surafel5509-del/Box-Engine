<!-- Path: engine/src/main/cpp/README.md -->
# Native module notes

This directory is built as `libboxengine.so` by the Android Gradle plugin and CMake. It uses Android NDK APIs, EGL 1.4, and GLES 3.0. JNI owns the lifecycle boundary; the render thread owns EGL/GLES resources. The project adds an archetype/SoA ECS, reflected components, main-thread/job-pool scheduling, a scene graph, versioned JSON scene/prefab serialization, and asynchronous Android asset reads.

Phase 4 renderer utilities include a 2D camera, runtime texture-atlas packer, sprite animation timelines, tilemap renderer, CPU particles, and an R8 SDF text pipeline with dynamic glyph insertion. The sprite batch uses VAO/VBO/IBO buffers. `tools/build_atlas.py` is the standard-library-only offline PNG atlas builder. GL resource owners expose shutdown/abandon paths so context loss does not delete stale names.

Phase 5 uses Oboe 1.9.3 through Android Prefab. The `AudioEngine` opens a low-latency float output stream on demand, performs bounded voice mixing in the audio callback, and sends asset reads/WAV decoding to worker threads. Lifecycle pause/resume is wired through `EngineRuntime`.

Phase 6 3D rendering is in progress. The CPU foundation currently includes a perspective camera, glTF 2.0 JSON/GLB loading for static triangle meshes and node hierarchies, material/texture metadata, generated vertex bases, and transformed-AABB frustum culling. GLES instancing, PBR lighting/shadows, and post-processing remain to be added.

The UI/debug phase is also in progress. `ImmediateUi` draws labels, buttons, sliders, joysticks, and text inputs through `SpriteBatch`; `UiLayout` uses density-independent safe-area anchors. `EngineSurfaceView` queues touch events/insets to the render thread and hosts a transparent Android IME editor for native text entry. The toggleable overlay shows FPS/frame time, SpriteBatch draw calls, ECS entity count, process RSS, GLES state, and CPU timings for runtime subsystems. A bounded log stream feeds a console with `help`, `clear`, `echo`, `stats`, and `entities` commands. `FrameProfiler` provides numeric scopes and `TraceSection` emits Android `ATrace` markers; see `docs/profiling.md` for Android Studio Profiler, Perfetto, and Systrace workflows. The demo game remains deferred.

The renderer loads `shaders/sprite.vert`, `shaders/sprite.frag`, `shaders/sdf_text.vert`, and `shaders/sdf_text.frag` through `AAssetManager`. Development overrides for the sprite shaders in the app-private `files/shaders/` directory are read first. The GLES renderer uses GLSL ES 3.00 and never calls OpenGL ES 3.1/3.2 entry points. Native validation in this sandbox uses host compilation with temporary stubs; an Android APK/device build still requires the Android toolchain.
