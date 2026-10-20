// Path: app/src/main/kotlin/com/boxengine/app/editor/ui/NativeEnginePanel.kt
package com.boxengine.app.editor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.boxengine.engine.BoxEngine
import com.boxengine.engine.EngineSurfaceView

/** Embeds the retained GLES SurfaceView for direct native-renderer validation. */
@Composable
fun NativeEnginePanel(onTextInputRequested: (String) -> Unit) {
    Column(Modifier.fillMaxSize().background(Color(0xFF060A10))) {
        Row(
            modifier = Modifier.fillMaxWidth().height(42.dp).padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Column(Modifier.weight(1f)) {
                Text("Native GLES 3.0 runtime", color = EditorColors.text, fontSize = 11.sp)
                Text("Existing Kotlin + C++ engine surface", color = EditorColors.muted, fontSize = 9.sp)
            }
            Button(
                onClick = BoxEngine::reloadShaders,
                modifier = Modifier.height(32.dp),
                colors = ButtonDefaults.buttonColors(containerColor = EditorColors.panelSelected),
            ) { Text("Reload shaders", color = EditorColors.accent, fontSize = 9.sp) }
        }
        Surface(Modifier.fillMaxSize().padding(horizontal = 8.dp, vertical = 4.dp), color = Color(0xFF05080C)) {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { context ->
                    EngineSurfaceView(context).apply {
                        onUiTextInputFocusRequested = onTextInputRequested
                    }
                },
                update = { view -> view.onUiTextInputFocusRequested = onTextInputRequested },
            )
        }
    }
}
