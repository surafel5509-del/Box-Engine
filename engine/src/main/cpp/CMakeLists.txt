# Path: engine/src/main/cpp/CMakeLists.txt
cmake_minimum_required(VERSION 3.22.1)
project(boxengine LANGUAGES CXX)

add_library(boxengine SHARED
    src/jni_bridge.cpp
    src/engine_runtime.cpp
    src/core/log.cpp
    src/audio/audio_engine.cpp
    src/audio/wav_decoder.cpp
    src/assets/asset_manager.cpp
    src/ecs/ecs.cpp
    src/ecs/job_system.cpp
    src/ecs/json.cpp
    src/ecs/system_scheduler.cpp
    src/scene/scene_graph.cpp
    src/scene/scene_serializer.cpp
    src/renderer/asset_io.cpp
    src/renderer/camera2d.cpp
    src/renderer/egl_context.cpp
    src/renderer/gltf_loader.cpp
    src/renderer/immediate_ui.cpp
    src/renderer/mesh3d.cpp
    src/renderer/particle_system.cpp
    src/renderer/render_target.cpp
    src/renderer/renderer.cpp
    src/renderer/renderer_components.cpp
    src/renderer/sdf_text.cpp
    src/renderer/shader_program.cpp
    src/renderer/sprite_animation.cpp
    src/renderer/sprite_batch.cpp
    src/renderer/texture.cpp
    src/renderer/texture_atlas.cpp
    src/renderer/tilemap.cpp
)

target_include_directories(boxengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(boxengine PRIVATE cxx_std_20)
set_target_properties(boxengine PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_compile_options(boxengine PRIVATE
    -fexceptions
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
)

target_compile_definitions(boxengine PRIVATE BOX_ENGINE_GLES_VERSION=300)

find_package(oboe REQUIRED CONFIG)

find_library(android-lib android REQUIRED)
find_library(log-lib log REQUIRED)
find_library(egl-lib EGL REQUIRED)
find_library(gles3-lib GLESv3 REQUIRED)

target_link_libraries(boxengine PRIVATE
    ${android-lib}
    ${log-lib}
    ${egl-lib}
    ${gles3-lib}
    oboe::oboe
)
