// Path: engine/src/main/cpp/src/jni_bridge.cpp
#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "box/core/log.hpp"
#include "box/engine_runtime.hpp"

namespace {

std::mutex gEngineMutex;
std::shared_ptr<box::EngineRuntime> gEngine;
jobject gAssetManagerReference = nullptr;

[[nodiscard]] std::shared_ptr<box::EngineRuntime> acquireEngine() {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    return gEngine;
}

[[nodiscard]] std::string fromJavaString(JNIEnv* environment, jstring value) {
    if (value == nullptr) return {};
    const char* characters = environment->GetStringUTFChars(value, nullptr);
    if (characters == nullptr) return {};
    std::string result(characters);
    environment->ReleaseStringUTFChars(value, characters);
    return result;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_boxengine_engine_BoxEngine_nativeCreate(JNIEnv* environment, jobject /* receiver */,
                                                  jobject javaAssetManager, jfloat density,
                                                  jstring shaderOverrideDirectory) {
    if (javaAssetManager == nullptr) {
        BOX_LOG_ERROR("Cannot initialize Box Engine without an Android AssetManager.");
        return JNI_FALSE;
    }

    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (gEngine != nullptr) return JNI_TRUE;

    jobject assetManagerReference = environment->NewGlobalRef(javaAssetManager);
    if (assetManagerReference == nullptr) {
        BOX_LOG_ERROR("Could not retain the Android AssetManager reference.");
        return JNI_FALSE;
    }
    AAssetManager* assetManager = AAssetManager_fromJava(environment, javaAssetManager);
    if (assetManager == nullptr) {
        environment->DeleteGlobalRef(assetManagerReference);
        BOX_LOG_ERROR("AAssetManager_fromJava returned null.");
        return JNI_FALSE;
    }

    const std::string overrideDirectory = fromJavaString(environment, shaderOverrideDirectory);
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
        environment->DeleteGlobalRef(assetManagerReference);
        BOX_LOG_ERROR("Could not read the shader override directory from Java.");
        return JNI_FALSE;
    }

    try {
        auto engine = std::make_shared<box::EngineRuntime>(assetManager, density, overrideDirectory);
        engine->start();
        gAssetManagerReference = assetManagerReference;
        gEngine = std::move(engine);
    } catch (const std::exception& exception) {
        environment->DeleteGlobalRef(assetManagerReference);
        BOX_LOG_ERROR("Could not start the native engine: %s", exception.what());
        return JNI_FALSE;
    } catch (...) {
        environment->DeleteGlobalRef(assetManagerReference);
        BOX_LOG_ERROR("Could not start the native engine due to an unknown native exception.");
        return JNI_FALSE;
    }
    BOX_LOG_INFO("Box Engine native handshake completed (density %.2f).", density);
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeSetSurface(JNIEnv* environment, jobject /* receiver */,
                                                      jobject javaSurface,
                                                      jboolean waitUntilApplied) {
    ANativeWindow* window = javaSurface != nullptr
                                ? ANativeWindow_fromSurface(environment, javaSurface)
                                : nullptr;
    std::shared_ptr<box::EngineRuntime> engine = acquireEngine();
    if (engine != nullptr) {
        engine->setSurface(window, waitUntilApplied == JNI_TRUE);
    } else if (window != nullptr) {
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeSetScreenInfo(JNIEnv* /* environment */,
                                                         jobject /* receiver */, jint width,
                                                         jint height, jfloat density) {
    if (const auto engine = acquireEngine()) {
        engine->setScreenInfo(width, height, density);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeSetWindowInsets(JNIEnv* /* environment */,
                                                           jobject /* receiver */, jint left,
                                                           jint top, jint right, jint bottom) {
    if (const auto engine = acquireEngine()) {
        engine->setWindowInsetsPixels(left, top, right, bottom);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeDispatchUiPointer(JNIEnv* /* environment */,
                                                             jobject /* receiver */,
                                                             jint action, jint pointerId,
                                                             jfloat xPixels, jfloat yPixels) {
    box::renderer::UiPointerAction pointerAction = box::renderer::UiPointerAction::None;
    switch (action) {
        case 1: pointerAction = box::renderer::UiPointerAction::Down; break;
        case 2: pointerAction = box::renderer::UiPointerAction::Move; break;
        case 3: pointerAction = box::renderer::UiPointerAction::Up; break;
        case 4: pointerAction = box::renderer::UiPointerAction::Cancel; break;
        default: return;
    }
    if (const auto engine = acquireEngine()) {
        engine->enqueueUiPointer(pointerAction, pointerId, xPixels, yPixels);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeDispatchUiText(JNIEnv* environment,
                                                          jobject /* receiver */, jstring text,
                                                          jboolean replaceText, jboolean backspace,
                                                          jboolean submit) {
    std::string value;
    if (text != nullptr) {
        value = fromJavaString(environment, text);
        if (environment->ExceptionCheck()) {
            environment->ExceptionClear();
            return;
        }
    }
    if (const auto engine = acquireEngine()) {
        engine->enqueueUiText(std::move(value), replaceText == JNI_TRUE,
                              backspace == JNI_TRUE, submit == JNI_TRUE);
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_boxengine_engine_BoxEngine_nativeConsumeUiTextInputFocusRequest(
    JNIEnv* environment, jobject /* receiver */) {
    const auto engine = acquireEngine();
    if (engine == nullptr) return nullptr;
    const std::optional<std::string> initialValue = engine->consumeUiTextInputFocusRequest();
    if (!initialValue.has_value()) return nullptr;
    jstring result = environment->NewStringUTF(initialValue->c_str());
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
        return nullptr;
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeResume(JNIEnv* /* environment */, jobject /* receiver */) {
    if (const auto engine = acquireEngine()) engine->setPaused(false);
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativePause(JNIEnv* /* environment */, jobject /* receiver */) {
    if (const auto engine = acquireEngine()) engine->setPaused(true);
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeDestroy(JNIEnv* environment, jobject /* receiver */) {
    std::shared_ptr<box::EngineRuntime> engine;
    jobject assetManagerReference = nullptr;
    {
        std::lock_guard<std::mutex> lock(gEngineMutex);
        engine = std::move(gEngine);
        assetManagerReference = gAssetManagerReference;
        gAssetManagerReference = nullptr;
    }
    if (engine != nullptr) engine->stop();
    if (assetManagerReference != nullptr) environment->DeleteGlobalRef(assetManagerReference);
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeSetRenderScale(JNIEnv* /* environment */,
                                                          jobject /* receiver */, jfloat scale) {
    if (const auto engine = acquireEngine()) engine->setRenderScale(scale);
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeSetAdaptiveResolution(JNIEnv* /* environment */,
                                                                 jobject /* receiver */,
                                                                 jboolean enabled) {
    if (const auto engine = acquireEngine()) engine->setAdaptiveResolution(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_boxengine_engine_BoxEngine_nativeReloadShaders(JNIEnv* /* environment */,
                                                         jobject /* receiver */) {
    if (const auto engine = acquireEngine()) engine->requestShaderReload();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_boxengine_engine_BoxEngine_nativePlayWavAsset(JNIEnv* environment,
                                                        jobject /* receiver */,
                                                        jstring javaAssetPath, jfloat gain,
                                                        jfloat pan, jboolean looping,
                                                        jfloat playbackRate) {
    if (javaAssetPath == nullptr) return JNI_FALSE;
    const std::string assetPath = fromJavaString(environment, javaAssetPath);
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
        return JNI_FALSE;
    }
    const auto engine = acquireEngine();
    if (engine == nullptr) return JNI_FALSE;
    return engine->audio().playWavAsset(assetPath, gain, pan, looping == JNI_TRUE,
                                        playbackRate)
               ? JNI_TRUE
               : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_boxengine_engine_BoxEngine_nativePlayTone(JNIEnv* /* environment */,
                                                   jobject /* receiver */,
                                                   jfloat frequencyHz,
                                                   jfloat durationSeconds, jfloat gain,
                                                   jfloat pan) {
    const auto engine = acquireEngine();
    if (engine == nullptr) return JNI_FALSE;
    return engine->audio().playTone(frequencyHz, durationSeconds, gain, pan) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_boxengine_engine_BoxEngine_nativeSetAudioMasterVolume(JNIEnv* /* environment */,
                                                                jobject /* receiver */,
                                                                jfloat volume) {
    const auto engine = acquireEngine();
    if (engine == nullptr) return JNI_FALSE;
    return engine->audio().setMasterVolume(volume) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_boxengine_engine_BoxEngine_nativeStopAllAudio(JNIEnv* /* environment */,
                                                        jobject /* receiver */) {
    const auto engine = acquireEngine();
    if (engine == nullptr) return JNI_FALSE;
    return engine->audio().stopAll() ? JNI_TRUE : JNI_FALSE;
}
