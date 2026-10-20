// Path: app/src/main/kotlin/com/boxengine/app/editor/ui/EditorTheme.kt
package com.boxengine.app.editor.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object EditorColors {
    val background = Color(0xFF090E16)
    val panel = Color(0xFF101722)
    val panelRaised = Color(0xFF151F2C)
    val panelSelected = Color(0xFF1C2A38)
    val border = Color(0xFF263547)
    val borderSoft = Color(0xFF1D2937)
    val text = Color(0xFFF1F5FA)
    val muted = Color(0xFF8292A5)
    val subtle = Color(0xFF596A7E)
    val accent = Color(0xFF4BD6C1)
    val accentDark = Color(0xFF1B796F)
    val blue = Color(0xFF68A9F0)
    val amber = Color(0xFFFFC46B)
    val red = Color(0xFFEF7781)
    val purple = Color(0xFFB49AFF)
    val green = Color(0xFF86D58C)
}

/** Theme colors used by all editor panels and native preview tools. */
private val BoxEngineDarkScheme = darkColorScheme(
    primary = EditorColors.accent,
    onPrimary = Color(0xFF06110F),
    primaryContainer = Color(0xFF174A45),
    onPrimaryContainer = Color(0xFFD8FFF8),
    secondary = EditorColors.blue,
    onSecondary = Color(0xFF07111D),
    tertiary = EditorColors.amber,
    background = EditorColors.background,
    onBackground = EditorColors.text,
    surface = EditorColors.panel,
    onSurface = EditorColors.text,
    surfaceVariant = EditorColors.panelRaised,
    onSurfaceVariant = EditorColors.muted,
    outline = EditorColors.border,
    error = EditorColors.red,
)

@Composable
fun BoxEngineEditorTheme(content: @Composable () -> Unit) {
    val scheme = if (isSystemInDarkTheme()) BoxEngineDarkScheme else BoxEngineDarkScheme
    MaterialTheme(colorScheme = scheme, content = content)
}
