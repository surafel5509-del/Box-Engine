// Path: engine/src/main/kotlin/com/boxengine/engine/BoxEngine.kt
package com.boxengine.engine

import android.content.res.AssetManager
import android.view.Surface

/** Kotlin entry points for the native engine runtime. */
object BoxEngine {
    init {
        System.loadLibrary("boxengine")
    }

    @Volatile
    private var initialized = false

    /** Starts the native render thread. The thread waits until both a surface and resume signal arrive. */
    @Synchronized
    fun initialize(
        assets: AssetManager,
        density: Float,
        shaderOverrideDirectory: String,
    ) {
        if (initialized) return
        check(nativeCreate(assets, density, shaderOverrideDirectory)) {
            "Box Engine native initialization failed. See logcat for details."
        }
        initialized = true
    }

    /** Notifies the native loop that the Activity is visible and interactive. */
    fun resume() {
        if (initialized) nativeResume()
    }

    /** Pauses rendering and tears down the EGL context without blocking the UI thread. */
    fun pause() {
        if (initialized) nativePause()
    }

    /** Stops and joins the render thread. Safe to call more than once. */
    @Synchronized
    fun shutdown() {
        if (!initialized) return
        nativeDestroy()
        initialized = false
    }

    /** Sets the user render scale. Values are clamped to the supported range [0.5, 1.0]. */
    fun setRenderScale(scale: Float) {
        if (initialized) nativeSetRenderScale(scale.coerceIn(0.5f, 1.0f))
    }

    /** Enables the frame-time-driven render-scale controller. */
    fun setAdaptiveResolutionEnabled(enabled: Boolean) {
        if (initialized) nativeSetAdaptiveResolution(enabled)
    }

    /** Requests a shader reload on the render thread. */
    fun reloadShaders() {
        if (initialized) nativeReloadShaders()
    }

    /** Asynchronously decodes and plays a packaged PCM WAV asset, for example `audio/jump.wav`. */
    fun playWavAsset(
        assetPath: String,
        gain: Float = 1.0f,
        pan: Float = 0.0f,
        looping: Boolean = false,
        playbackRate: Float = 1.0f,
    ): Boolean = initialized &&
        nativePlayWavAsset(assetPath, gain, pan, looping, playbackRate)

    /** Queues a short sine tone on the Oboe output stream. */
    fun playTone(
        frequencyHz: Float = 440.0f,
        durationSeconds: Float = 0.2f,
        gain: Float = 0.25f,
        pan: Float = 0.0f,
    ): Boolean = initialized && nativePlayTone(frequencyHz, durationSeconds, gain, pan)

    /** Sets the output mix volume without blocking the audio callback. */
    fun setAudioMasterVolume(volume: Float): Boolean =
        initialized && nativeSetAudioMasterVolume(volume)

    /** Stops all currently playing voices. */
    fun stopAllAudio(): Boolean = initialized && nativeStopAllAudio()

    internal fun attachSurface(surface: Surface?, waitUntilDetached: Boolean = false) {
        if (initialized) nativeSetSurface(surface, waitUntilDetached)
    }

    internal fun updateScreenInfo(width: Int, height: Int, density: Float) {
        if (initialized) nativeSetScreenInfo(width, height, density)
    }

    internal fun updateWindowInsets(left: Int, top: Int, right: Int, bottom: Int) {
        if (initialized) nativeSetWindowInsets(left, top, right, bottom)
    }

    internal fun dispatchUiPointer(action: Int, pointerId: Int, xPixels: Float, yPixels: Float) {
        if (initialized) nativeDispatchUiPointer(action, pointerId, xPixels, yPixels)
    }

    fun dispatchUiText(
        text: String = "",
        replaceText: Boolean = false,
        backspace: Boolean = false,
        submit: Boolean = false,
    ) {
        if (initialized) nativeDispatchUiText(text, replaceText, backspace, submit)
    }

    internal fun consumeUiTextInputFocusRequest(): String? =
        if (initialized) nativeConsumeUiTextInputFocusRequest() else null

    private external fun nativeCreate(
        assets: AssetManager,
        density: Float,
        shaderOverrideDirectory: String,
    ): Boolean

    private external fun nativeSetSurface(surface: Surface?, waitUntilApplied: Boolean)
    private external fun nativeSetScreenInfo(width: Int, height: Int, density: Float)
    private external fun nativeSetWindowInsets(left: Int, top: Int, right: Int, bottom: Int)
    private external fun nativeDispatchUiPointer(action: Int, pointerId: Int, xPixels: Float, yPixels: Float)
    private external fun nativeDispatchUiText(
        text: String,
        replaceText: Boolean,
        backspace: Boolean,
        submit: Boolean,
    )
    private external fun nativeConsumeUiTextInputFocusRequest(): String?
    private external fun nativeResume()
    private external fun nativePause()
    private external fun nativeDestroy()
    private external fun nativeSetRenderScale(scale: Float)
    private external fun nativeSetAdaptiveResolution(enabled: Boolean)
    private external fun nativeReloadShaders()
    private external fun nativePlayWavAsset(
        assetPath: String,
        gain: Float,
        pan: Float,
        looping: Boolean,
        playbackRate: Float,
    ): Boolean
    private external fun nativePlayTone(
        frequencyHz: Float,
        durationSeconds: Float,
        gain: Float,
        pan: Float,
    ): Boolean
    private external fun nativeSetAudioMasterVolume(volume: Float): Boolean
    private external fun nativeStopAllAudio(): Boolean
}
