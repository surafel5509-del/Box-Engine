// Path: app/src/main/kotlin/com/boxengine/app/MainActivity.kt
package com.boxengine.app

import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.Gravity
import android.view.KeyEvent
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.FrameLayout
import androidx.activity.ComponentActivity
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import com.boxengine.app.editor.model.EditorController
import com.boxengine.app.editor.ui.BoxEngineEditorApp
import com.boxengine.engine.BoxEngine

/** Hosts the native GLES runtime beside the Android-native Compose game editor. */
class MainActivity : ComponentActivity() {
    private var engineInitialized = false
    private lateinit var editorController: EditorController
    private lateinit var editorHost: ComposeView
    private lateinit var nativeTextEditor: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = android.graphics.Color.rgb(9, 14, 22)
        window.navigationBarColor = android.graphics.Color.rgb(9, 14, 22)
        window.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)

        BoxEngine.initialize(
            assets = assets,
            density = resources.displayMetrics.density.coerceAtLeast(0.1f),
            shaderOverrideDirectory = filesDir.absolutePath,
        )
        engineInitialized = true
        editorController = EditorController(applicationContext)

        val root = FrameLayout(this)
        editorHost = ComposeView(this).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            isFocusableInTouchMode = true
        }
        editorHost.setContent {
            BoxEngineEditorApp(
                controller = editorController,
                onNativeTextInputRequested = ::requestNativeTextInput,
            )
        }
        root.addView(
            editorHost,
            FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT),
        )

        nativeTextEditor = EditText(this).apply {
            alpha = 0.01f
            setBackgroundColor(android.graphics.Color.TRANSPARENT)
            isSingleLine = true
            maxLines = 1
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = EditorInfo.IME_ACTION_DONE
            layoutParams = FrameLayout.LayoutParams(1, 1, Gravity.TOP or Gravity.START)
            clearFocus()
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit
                override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) = Unit
                override fun afterTextChanged(editable: Editable?) {
                    BoxEngine.dispatchUiText(text = editable?.toString().orEmpty(), replaceText = true)
                }
            })
            setOnEditorActionListener { editor, actionId, event ->
                val submitted = actionId == EditorInfo.IME_ACTION_DONE ||
                    (event?.let { it.keyCode == KeyEvent.KEYCODE_ENTER && it.action == KeyEvent.ACTION_UP } == true)
                if (!submitted) {
                    false
                } else {
                    BoxEngine.dispatchUiText(submit = true)
                    val inputManager = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
                    inputManager.hideSoftInputFromWindow(editor.windowToken, 0)
                    editor.clearFocus()
                    editorHost.requestFocus()
                    true
                }
            }
        }
        root.addView(nativeTextEditor)
        setContentView(root)
        editorHost.requestFocus()
    }

    override fun onResume() {
        super.onResume()
        if (engineInitialized) BoxEngine.resume()
    }

    override fun onPause() {
        if (engineInitialized) BoxEngine.pause()
        super.onPause()
    }

    override fun onStop() {
        if (::editorController.isInitialized && editorController.isDirty) editorController.saveProject()
        super.onStop()
    }

    override fun onDestroy() {
        if (engineInitialized) {
            BoxEngine.shutdown()
            engineInitialized = false
        }
        super.onDestroy()
    }

    private fun requestNativeTextInput(initialText: String) {
        nativeTextEditor.setText(initialText)
        nativeTextEditor.setSelection(nativeTextEditor.text.length)
        nativeTextEditor.requestFocus()
        val inputManager = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
        nativeTextEditor.post { inputManager.showSoftInput(nativeTextEditor, InputMethodManager.SHOW_IMPLICIT) }
    }
}
