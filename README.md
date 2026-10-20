<!-- Path: README.md -->
# Box Engine — Android GLES 3.0 game engine

An Android-only Kotlin + C++20 game-engine project. The repository carries the Phase 1 runtime foundation, Phase 2 EGL/OpenGL ES 3.0 renderer, Phase 3 ECS/scene/asset systems, Phase 4 2D rendering features, Phase 5 Oboe audio, an in-progress Phase 6 3D renderer, and in-progress UI/debug tooling. The application uses an Android `Activity` and `SurfaceView`; it does not include desktop windowing code, Vulkan, GLFW, or SDL.

## Architecture

```text
┌──────────────── Android application process ────────────────┐
│                                                             │
│  MainActivity (Kotlin) ── Activity lifecycle ───────────┐    │
│       │                                                  │    │
│       └── EngineSurfaceView ── SurfaceHolder callbacks  │    │
│                    │                                     │    │
│                    └── BoxEngine Kotlin API              │    │
│                              │ JNI                       │    │
│  ┌───────────────────────────▼────────────────────────┐  │    │
│  │ C++20 EngineRuntime (dedicated render thread)       │  │    │
│  │  ├─ TimeSystem (fixed update + variable frame time) │  │    │
│  │  ├─ Archetype/SoA ECS + reflected components        │  │    │
│  │  ├─ SystemScheduler + worker pool                   │  │    │
│  │  ├─ SceneGraph + JSON scenes/prefabs                │  │    │
│  │  ├─ Async Android AssetManager                      │  │    │
│  │  ├─ AudioEngine (Oboe output callback)               │  │    │
│  │  └─ EglContext (EGL 1.4, Android native window)     │  │    │
│  │        └─ Renderer (OpenGL ES 3.0 API surface)      │  │    │
│  │             ├─ Sprite batch, atlases, animation     │  │    │
│  │             ├─ Camera2D + tilemap + CPU particles   │  │    │
│  │             ├─ Dynamic R8 SDF text renderer         │  │    │
│  │             └─ RGBA8 + depth FBO / scaled blit      │  │    │
│  └─────────────────────────────────────────────────────┘  │    │
└─────────────────────────────────────────────────────────────┘
```

All EGL and GL work is confined to the native render thread; Oboe delivers audio from its dedicated callback thread. Surface destruction is acknowledged by the render thread before `SurfaceHolder.Callback.surfaceDestroyed` returns. Pause, surface loss, rotation, and resume manage graphics resources safely, while Activity pause/resume also pauses or resumes audio output. Native code never calls browser-facing or desktop APIs.

## Project layout

- `.github/workflows/android-apk.yml` installs Java/Android toolchains, builds the debug APK, and uploads it as a workflow artifact.
- `app/` contains the Android application, `MainActivity`, and the hidden IME editor host.
- `engine/src/main/kotlin/com/boxengine/engine/` contains the engine `SurfaceView` and Kotlin/JNI API.
- `engine/src/main/cpp/include/box/` contains the C++20 engine APIs, including the ECS, renderer, immediate UI, debug overlay, frame profiler, and Android trace marker.
- `engine/src/main/cpp/src/` contains the corresponding runtime, renderer, bounded log stream, and subsystem implementations; `CMakeLists.txt` registers the native sources.
- `docs/profiling.md` documents Android Studio Profiler, Perfetto, and Systrace workflows.
- `tools/build_atlas.py` builds sprite atlases for the 2D renderer.

## Build and run

1. Install Android Studio with JDK 17, Android SDK Platform 34, Android NDK `26.1.10909125`, and CMake `3.22.1`.
2. Open this repository as a project in Android Studio and allow the Gradle sync/NDK components to install.
3. Select an Android device or emulator that advertises OpenGL ES 3.0 (API 24 or newer), then run the `app` configuration.
4. Command-line debug build: `./gradlew :app:assembleDebug`.
5. Install from the command line: `./gradlew :app:installDebug` (with `adb` and a connected device).

The **Android Debug APK** GitHub Actions workflow builds `:app:assembleDebug` on pushes to `arena/7e89a722-box-engine`, pull requests targeting `main`, and manual dispatch. Download its `box-engine-debug-apk` artifact from the completed workflow run; artifacts are retained for 14 days.

The sample app currently needs no runtime permissions. The manifest requires an OpenGL ES 3.0-capable device. Both `arm64-v8a` and `armeabi-v7a` are configured; `arm64-v8a` is required and is included.

## Phase 1 runtime and core

- Kotlin `MainActivity` wires create/resume/pause/destroy to the native runtime.
- `EngineSurfaceView` forwards surface and size/density changes through JNI.
- C++ logging writes to Android logcat (`BoxEngine` tag) and a thread-safe 256-entry native log stream used by the in-game console; `BOX_ASSERT` logs and aborts on invariant failure.
- `ArenaAllocator` provides monotonic allocations; `PoolAllocator` provides fixed-size O(1) blocks.
- `math.hpp` includes `Vec2/3/4`, column-major `Mat4`, `Quat`, `AABB`, `Ray`, `Frustum`, and `Transform`.
- `TimeSystem` clamps variable delta time and runs a bounded fixed-step accumulator.

## Phase 2 renderer and GLES 3.0 audit

The renderer requests an EGL window config with the GLES 3 renderable bit and an EGL 1.4 client context of version 3.0 when `EGL_KHR_create_context` is available. On legacy EGL implementations it requests client version 3. Source-level GL calls and GLSL are restricted to the OpenGL ES 3.0 / GLSL ES 3.00 feature set; the renderer does not call ES 3.1/3.2 or Vulkan APIs.

Implemented ES 3.0 paths include VAO/VBO/IBO geometry, shader compile/link and logs, 2D/cubemap/ETC2 texture uploads, sampler objects, RGBA8 + `DEPTH_COMPONENT24` framebuffer render targets, `glBlitFramebuffer`, a colored triangle, textured quads, and same-texture sprite batching. Dynamic resolution scales the off-screen target between 0.5 and 1.0 and composites it to the Android surface. Screen coordinates use Android density-independent pixels. The demo view is intentionally a small renderer validation scene, not the Phase 10 game.

Shaders are loaded from `engine/src/main/assets/shaders/`. `BoxEngine.reloadShaders()` requests a render-thread reload. For development overrides, the loader first checks app-private `filesDir/shaders/sprite.vert` and `filesDir/shaders/sprite.frag`, then falls back to the packaged assets. A failed reload keeps the last valid program installed.

## Phase 3 ECS, scenes, and assets

- `ecs::World` stores entities in sorted archetypes with 64-entity chunks and one typed SoA column per component. Structural mutation is intended for the main thread; worker systems must partition or otherwise synchronize their own data access.
- `ComponentRegistry` exposes component names, runtime IDs, sizes, alignments, JSON serializers/deserializers, and component inspection metadata. `SceneSerializer` writes versioned JSON scenes and single-entity prefabs, remaps entity handles on load, and validates scene-graph references before replacing the world.
- `SystemScheduler` runs main-thread systems in deterministic order and submits worker-pool systems at each order phase; later phases wait for worker jobs. `EngineRuntime` invokes it with the frame delta and updates scene-graph world transforms.
- `assets::AssetManager` asynchronously reads Android packaged assets, shares duplicate in-flight requests, and caches immutable byte blobs tagged as texture, audio, font, shader, glTF, or binary. Format-specific decoding is performed by consumers. `JobSystem` is also available for other CPU-bound jobs.

## Phase 4 2D rendering systems

- `TextureAtlasBuilder` packs RGBA8 images with edge-extruded padding at runtime. `tools/build_atlas.py` builds PNG atlases and JSON region metadata offline. Input image rows are top-to-bottom; UV rectangles are reported in OpenGL `left, bottom, right, top` order.
- `SpriteAnimationPlayer` supports variable-duration frames, looping, playback time, and cross-fade samples. The renderer demo exercises camera transforms, tilemaps, an animated atlas sprite, and CPU particles.
- `Camera2D` provides normalized viewports, pan, clamped zoom, shake, and pixel snapping. `Tilemap2D` stores atlas region names, flip flags, and grid data; `TilemapRenderer` culls against optional world bounds.
- `coverageToSignedDistanceField` converts coverage bitmaps to R8 SDF glyphs. `DynamicFontAtlas` accepts glyphs at runtime and `SdfTextRenderer` batches UTF-8 text through the GLES 3.0 SDF shaders. The caller supplies rasterized coverage/glyph metrics; this module does not parse TTF/OTF files.
- Renderer components for sprites, animation, cameras, tilemaps, SDF text, and particle emitters have JSON reflection metadata. `ParticleSystem` simulates a bounded set of particles on the CPU.

Example offline atlas build:

```sh
python3 tools/build_atlas.py assets/sprites/*.png \
  --output app/src/main/assets/atlas.png \
  --metadata app/src/main/assets/atlas.json
```

## Phase 5 Android audio

- Oboe `1.9.3` is consumed as an Android Prefab dependency. CMake links `oboe::oboe`; the engine opens a shared, low-latency float stereo output stream when playback is requested.
- The audio callback mixes up to 32 voices from a fixed pool. Audio commands cross from Kotlin/loader threads through a bounded atomic queue; the callback performs no file I/O, dynamic allocation, or mutex locking. Oboe disconnection is logged and the stream is reopened on a worker.
- `AudioEngine::playWavAsset` asynchronously reads packaged assets through the Phase 3 asset manager, decodes WAV off the callback thread, and caches up to 128 clips. Supported input is little-endian RIFF/WAVE PCM 8/16/24/32-bit or IEEE float32, mono or stereo, 8–192 kHz. Decoded clips are limited to 64 MiB each and the total PCM cache to 128 MiB; playback uses linear sample-rate conversion, gain, pan, looping, and playback-rate controls.
- `AudioEngine::playTone` provides a short sine test tone. Kotlin `BoxEngine` exposes `playWavAsset`, `playTone`, `setAudioMasterVolume`, and `stopAllAudio`; engine lifecycle pause/resume also pauses/resumes the Oboe stream.

## Phase 6 mobile 3D renderer — in progress

- The first implementation slice adds a right-handed perspective `Camera3D`, CPU interleaved `Vertex3D`/mesh data, scene-node world transforms, conservative transformed-AABB frustum culling, and a CPU glTF 2.0 JSON/GLB loader.
- The loader reads embedded base64 data, GLB binary chunks, and bounded relative Android asset URIs. It supports triangle primitives with float POSITION/NORMAL/TANGENT/TEXCOORD_0 accessors, uint8/uint16/uint32 indices, generated normals/tangents when absent, node TRS/matrix hierarchies, default scenes, metallic-roughness factors, texture references, samplers, and retained encoded image bytes. Safety limits are 128 MiB per buffer, 32 MiB per image, two million total vertices, and four million total indices.
- Sparse accessors, required extensions, non-triangle topologies, animation/skin data, and nonzero texture-coordinate sets are rejected or unsupported. GLES mesh upload/instancing, PBR lighting/shadows, and bloom/vignette/tonemapping are the remaining Phase 6 work.

## UI + debug tooling — in progress

- `ImmediateUi` draws labels and widgets as GLES 3.0 solid quads through the streaming `SpriteBatch`. It currently includes buttons, sliders, a touch joystick, and an editable text field with a compact built-in ASCII bitmap face.
- `UiLayout` resolves nine anchors in density-independent units inside safe insets. `EngineSurfaceView` forwards single-pointer touch events and Android system-bar/display-cutout insets to the render thread. A hidden Android `EditText` requests the soft keyboard and streams text back into the native widget.
- The toggleable on-device overlay displays paced FPS/frame time, SpriteBatch draw calls, ECS entity count, process RSS, current GLES program/framebuffer/depth/blend state, and per-subsystem CPU timings. A bounded native log stream powers the in-game console (`help`, `clear`, `echo`, `stats`, `entities`). Native `ATrace` sections are exposed to Perfetto/Systrace. See [`docs/profiling.md`](docs/profiling.md) for Android Studio Profiler, Perfetto, and Systrace instructions.

### Validation status

Host C++20 strict-warning syntax checks with temporary Android/EGL/GLES/JNI stubs passed for the updated runtime, renderer, UI, sprite batch, ECS header/source, JNI bridge, and log stream. Temporary behavior harnesses passed for immediate-UI layout/widgets/UTF-8 input replacement, frame-profiler timing/snapshots, and bounded log-stream reads; earlier ECS/scene/prefab, scheduler, SDF, atlas, WAV, and glTF tests also passed. The Oboe audio engine was syntax-checked against upstream Oboe 1.9.3 headers. An Android APK/device build has not been run in this environment: no Java runtime, Android SDK/NDK, or `adb` is installed, so the Gradle wrapper cannot start.

### GLES calls used

`glCreateShader`, `glShaderSource`, `glCompileShader`, `glGetShaderiv`, `glGetShaderInfoLog`, `glCreateProgram`, `glAttachShader`, `glLinkProgram`, `glGetProgramiv`, `glGetProgramInfoLog`, `glGetUniformLocation`, `glUseProgram`, `glUniform*`, `glGenVertexArrays`, `glBindVertexArray`, `glGenBuffers`, `glBindBuffer`, `glBufferData`, `glBufferSubData`, `glVertexAttribPointer`, `glEnableVertexAttribArray`, `glDrawElements`, `glDrawArrays`, `glGenTextures`, `glTexImage2D`, `glTexSubImage2D`, `glCompressedTexImage2D`, `glTexParameteri`, `glPixelStorei`, `glGenSamplers`, `glSamplerParameteri`, `glBindSampler`, `glGenFramebuffers`, `glFramebufferTexture2D`, `glCheckFramebufferStatus`, `glDrawBuffers`, `glBlitFramebuffer`, `glViewport`, `glClear*`, `glEnable`, `glDisable`, `glBlendFunc`, `glGetString`, `glGetError`, and corresponding ES 3.0 resource deletion calls.
