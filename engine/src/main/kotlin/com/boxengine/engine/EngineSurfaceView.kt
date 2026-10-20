// Path: engine/src/main/kotlin/com/boxengine/engine/EngineSurfaceView.kt
package com.boxengine.engine

import android.content.Context
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowInsets

/** Owns the Android window surface callbacks and forwards them to the native render loop. */
class EngineSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : SurfaceView(context, attrs), SurfaceHolder.Callback {
    private val displayDensity = resources.displayMetrics.density.coerceAtLeast(0.1f)
    private var activePointerId = MotionEvent.INVALID_POINTER_ID
    var onUiTextInputFocusRequested: ((String) -> Unit)? = null

    private val mainHandler = Handler(Looper.getMainLooper())
    private val textInputFocusPoller = object : Runnable {
        override fun run() {
            val focusCallback = onUiTextInputFocusRequested
            if (isAttachedToWindow && focusCallback != null) {
                val initialText = BoxEngine.consumeUiTextInputFocusRequest()
                if (initialText != null) focusCallback(initialText)
            }
            if (isAttachedToWindow) mainHandler.postDelayed(this, 80L)
        }
    }

    init {
        holder.setFormat(PixelFormat.RGBA_8888)
        holder.addCallback(this)
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        mainHandler.removeCallbacks(textInputFocusPoller)
        mainHandler.post(textInputFocusPoller)
    }

    override fun onDetachedFromWindow() {
        mainHandler.removeCallbacks(textInputFocusPoller)
        super.onDetachedFromWindow()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        BoxEngine.attachSurface(holder.surface)
        sendScreenInfo(width, height)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        sendScreenInfo(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        // Android requires rendering to be detached before this callback returns.
        BoxEngine.attachSurface(null, waitUntilDetached = true)
    }

    override fun onApplyWindowInsets(insets: WindowInsets): WindowInsets {
        val safeInsets = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            insets.getInsets(
                WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout(),
            ).let { values -> intArrayOf(values.left, values.top, values.right, values.bottom) }
        } else {
            @Suppress("DEPRECATION")
            intArrayOf(
                insets.systemWindowInsetLeft,
                insets.systemWindowInsetTop,
                insets.systemWindowInsetRight,
                insets.systemWindowInsetBottom,
            )
        }
        BoxEngine.updateWindowInsets(safeInsets[0], safeInsets[1], safeInsets[2], safeInsets[3])
        return super.onApplyWindowInsets(insets)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                activePointerId = event.getPointerId(0)
                requestFocus()
                dispatchPointer(1, event.getX(0), event.getY(0))
            }
            MotionEvent.ACTION_MOVE -> {
                val pointerIndex = event.findPointerIndex(activePointerId)
                if (pointerIndex >= 0) {
                    dispatchPointer(2, event.getX(pointerIndex), event.getY(pointerIndex))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val pointerIndex = event.actionIndex
                if (event.getPointerId(pointerIndex) == activePointerId) {
                    dispatchPointer(3, event.getX(pointerIndex), event.getY(pointerIndex))
                    activePointerId = MotionEvent.INVALID_POINTER_ID
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                dispatchPointer(4, 0.0f, 0.0f)
                activePointerId = MotionEvent.INVALID_POINTER_ID
            }
        }
        return true
    }

    private fun dispatchPointer(action: Int, x: Float, y: Float) {
        BoxEngine.dispatchUiPointer(action, activePointerId, x, y)
    }

    private fun sendScreenInfo(width: Int, height: Int) {
        BoxEngine.updateScreenInfo(
            width = width.coerceAtLeast(1),
            height = height.coerceAtLeast(1),
            density = displayDensity,
        )
    }
}
