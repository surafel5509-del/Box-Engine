// Path: app/src/main/kotlin/com/boxengine/app/MainActivity.kt
package com.boxengine.app

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.FrameLayout
import com.boxengine.engine.BoxEngine
import com.boxengine.engine.EngineSurfaceView

/** Android lifecycle host for the GLES 3.0 engine surface and native in-game text entry. */
class MainActivity : Activity() {
    private var engineInitialized = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        BoxEngine.initialize(
            assets = assets,
            density = resources.displayMetrics.density.coerceAtLeast(0.1f),
            shaderOverrideDirectory = filesDir.absolutePath,
        )
        engineInitialized = true

        val surfaceView = EngineSurfaceView(this)
        val textEditor = EditText(this).apply {
            alpha = 0.01f
            setBackgroundColor(android.graphics.Color.TRANSPARENT)
            isSingleLine = true
            maxLines = 1
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = EditorInfo.IME_ACTION_DONE
            layoutParams = FrameLayout.LayoutParams(1, 1, Gravity.TOP or Gravity.START)
            clearFocus()
        }
        textEditor.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(editable: Editable?) {
                BoxEngine.dispatchUiText(text = editable?.toString().orEmpty(), replaceText = true)
            }
        })
        textEditor.setOnEditorActionListener { editor, actionId, event ->
            val submitted = actionId == EditorInfo.IME_ACTION_DONE ||
                (event?.let { it.keyCode == KeyEvent.KEYCODE_ENTER && it.action == KeyEvent.ACTION_UP } == true)
            if (!submitted) {
                false
            } else {
                BoxEngine.dispatchUiText(submit = true)
                val inputManager = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
                inputManager.hideSoftInputFromWindow(editor.windowToken, 0)
                editor.clearFocus()
                surfaceView.requestFocus()
                true
            }
        }
        surfaceView.onUiTextInputFocusRequested = { initialText ->
            textEditor.setText(initialText)
            textEditor.setSelection(textEditor.text.length)
            textEditor.requestFocus()
            val inputManager = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
            textEditor.post { inputManager.showSoftInput(textEditor, InputMethodManager.SHOW_IMPLICIT) }
        }

        val root = FrameLayout(this)
        root.addView(surfaceView, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT,
        ))
        root.addView(textEditor)
        setContentView(root)
        surfaceView.requestFocus()
        applyImmersiveMode()
    }

    override fun onResume() {
        super.onResume()
        if (engineInitialized) BoxEngine.resume()
        applyImmersiveMode()
    }

    override fun onPause() {
        if (engineInitialized) BoxEngine.pause()
        super.onPause()
    }

    override fun onDestroy() {
        if (engineInitialized) {
            BoxEngine.shutdown()
            engineInitialized = false
        }
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyImmersiveMode()
    }

    @Suppress("DEPRECATION")
    private fun applyImmersiveMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                controller.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                )
        }
    }
}
