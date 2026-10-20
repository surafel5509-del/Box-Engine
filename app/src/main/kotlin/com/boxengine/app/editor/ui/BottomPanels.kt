// Path: app/src/main/kotlin/com/boxengine/app/editor/ui/BottomPanels.kt
package com.boxengine.app.editor.ui

import android.graphics.BitmapFactory
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.boxengine.app.editor.model.AssetKind
import com.boxengine.app.editor.model.BottomPanel
import com.boxengine.app.editor.model.EditorController
import com.boxengine.app.editor.model.EditorPane
import com.boxengine.app.editor.model.EditorTool
import com.boxengine.app.editor.model.PlayState
import com.boxengine.app.editor.model.SpriteAsset
import com.boxengine.app.editor.model.SpriteComponent
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.Locale

/** Hosts the project browser, tile palette, sprite crop editor, animation timeline, and console. */
@Composable
fun BottomDock(controller: EditorController, onImport: () -> Unit, modifier: Modifier = Modifier) {
    Column(modifier.background(EditorColors.panel)) {
        Row(
            modifier = Modifier.fillMaxWidth().height(37.dp).horizontalScroll(rememberScrollState())
                .background(EditorColors.panel).padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            BottomPanel.entries.forEach { panel ->
                val label = when (panel) {
                    BottomPanel.ASSETS -> "Assets"
                    BottomPanel.TILE_PALETTE -> "Tile Palette"
                    BottomPanel.SPRITE_EDITOR -> "Sprite Editor"
                    BottomPanel.ANIMATION -> "Animation"
                    BottomPanel.CONSOLE -> "Console"
                }
                Surface(
                    onClick = { controller.bottomPanel = panel },
                    color = if (controller.bottomPanel == panel) EditorColors.panelSelected else Color.Transparent,
                    shape = RoundedCornerShape(6.dp),
                ) {
                    Text(
                        label,
                        Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                        color = if (controller.bottomPanel == panel) EditorColors.text else EditorColors.muted,
                        fontSize = 10.sp,
                        fontWeight = if (controller.bottomPanel == panel) FontWeight.SemiBold else FontWeight.Normal,
                    )
                }
            }
        }
        Surface(Modifier.fillMaxWidth().height(1.dp), color = EditorColors.borderSoft) {}
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (controller.bottomPanel) {
                BottomPanel.ASSETS -> AssetsPanel(controller, onImport, Modifier.fillMaxSize(), compact = true)
                BottomPanel.TILE_PALETTE -> TilePalettePanel(controller, Modifier.fillMaxSize(), compact = true)
                BottomPanel.SPRITE_EDITOR -> SpriteEditorPanel(controller, onImport, Modifier.fillMaxSize(), compact = true)
                BottomPanel.ANIMATION -> AnimationPanel(controller, Modifier.fillMaxSize(), compact = true)
                BottomPanel.CONSOLE -> ConsolePanel(controller, Modifier.fillMaxSize(), compact = true)
            }
        }
    }
}

@Composable
fun AssetsPanel(
    controller: EditorController,
    onImport: () -> Unit,
    modifier: Modifier = Modifier,
    compact: Boolean = false,
) {
    Column(modifier.background(EditorColors.panel)) {
        if (!compact) {
            PanelHeaderRow("PROJECT / ASSETS", "${controller.document.assets.size} files", actionLabel = "Import", onAction = onImport)
        }
        if (controller.document.assets.isEmpty()) {
            Column(
                Modifier.fillMaxSize().padding(16.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Text("No imported assets yet", color = EditorColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                Text("Import images, audio, JSON scenes, or glTF models from Android storage.", color = EditorColors.muted, fontSize = 10.sp)
                if (compact) Text("Use the Import action in the project menu.", color = EditorColors.subtle, fontSize = 9.sp)
                else Text("Files are copied into this project’s private Assets folders.", color = EditorColors.subtle, fontSize = 9.sp)
            }
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxSize().padding(horizontal = 8.dp, vertical = if (compact) 4.dp else 8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                items(controller.document.assets, key = { it.id }) { asset ->
                    AssetRow(controller, asset)
                }
            }
        }
    }
}

@Composable
private fun AssetRow(controller: EditorController, asset: SpriteAsset) {
    val selected = controller.selectedAssetId == asset.id
    val tint = when (asset.kind) {
        AssetKind.SPRITE -> EditorColors.accent
        AssetKind.AUDIO -> EditorColors.amber
        AssetKind.SCRIPT -> EditorColors.purple
        AssetKind.SCENE -> EditorColors.blue
        AssetKind.MODEL -> EditorColors.green
        AssetKind.OTHER -> EditorColors.muted
    }
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp))
            .background(if (selected) EditorColors.panelSelected else EditorColors.panelRaised)
            .clickable { controller.selectAsset(asset.id) }
            .padding(horizontal = 9.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(Modifier.size(28.dp).clip(RoundedCornerShape(6.dp)).background(tint.copy(alpha = 0.14f)), contentAlignment = Alignment.Center) {
            Text(assetGlyph(asset.kind), color = tint, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }
        Column(Modifier.weight(1f)) {
            Text(asset.name, color = EditorColors.text, fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            val details = buildString {
                append(asset.kind.name.lowercase(Locale.US))
                if (asset.width > 0 && asset.height > 0) append(" · ${asset.width}×${asset.height}")
                append(" · ")
                append(formatAssetSize(asset.byteCount))
            }
            Text(details, color = EditorColors.muted, fontSize = 8.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        if (asset.kind == AssetKind.SPRITE && controller.selectedObject != null) {
            TextButton(onClick = { controller.assignSelectedSprite(asset.id) }) {
                Text("Assign", color = EditorColors.accent, fontSize = 9.sp)
            }
        }
    }
}

@Composable
fun TilePalettePanel(controller: EditorController, modifier: Modifier = Modifier, compact: Boolean = false) {
    val palette = listOf(
        0xFF568A53L to "Grass",
        0xFF6A4E3DL to "Soil",
        0xFF596C80L to "Stone",
        0xFF36CBB4L to "Mint",
        0xFFE36C73L to "Coral",
        0xFFFFC85AL to "Gold",
        0xFF74A8EEL to "Sky",
        0xFF8ACC75L to "Leaf",
    )
    Column(modifier.background(EditorColors.panel).padding(horizontal = 10.dp, vertical = if (compact) 5.dp else 10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        if (!compact) PanelHeaderRow("TILE PALETTE", "1 unit / cell", actionLabel = "Paint mode", onAction = {
            controller.tool = EditorTool.PAINT
            controller.pane = EditorPane.SCENE
        })
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Text("Brush", color = EditorColors.muted, fontSize = 9.sp)
            palette.forEach { (argb, label) ->
                Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Surface(
                        onClick = {
                            controller.setTileColor(argb)
                            controller.tool = EditorTool.PAINT
                            controller.pane = EditorPane.SCENE
                        },
                        modifier = Modifier.size(if (compact) 25.dp else 30.dp),
                        color = Color(argb.toInt()),
                        shape = RoundedCornerShape(6.dp),
                        border = if (controller.selectedTileColor == argb && controller.selectedTileAssetId == null)
                            androidx.compose.foundation.BorderStroke(2.dp, EditorColors.text)
                        else androidx.compose.foundation.BorderStroke(1.dp, EditorColors.border),
                    ) {}
                    if (!compact) Text(label, color = EditorColors.muted, fontSize = 7.sp)
                }
            }
            Spacer(Modifier.width(3.dp))
            Text("Grid", color = EditorColors.muted, fontSize = 9.sp)
            listOf(0.5f, 1f, 2f).forEach { size ->
                Surface(
                    onClick = { controller.setGridSize(size) },
                    color = if (controller.document.gridSize == size) EditorColors.accentDark else EditorColors.panelSelected,
                    shape = RoundedCornerShape(5.dp),
                ) {
                    Text("${size.toIntOrHalf()}", Modifier.padding(horizontal = 7.dp, vertical = 5.dp), color = EditorColors.text, fontSize = 9.sp)
                }
            }
        }
        val spriteAssets = controller.document.assets.filter { it.kind == AssetKind.SPRITE }
        if (spriteAssets.isNotEmpty()) {
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Textures", color = EditorColors.muted, fontSize = 9.sp, modifier = Modifier.align(Alignment.CenterVertically))
                spriteAssets.forEach { asset ->
                    Surface(
                        onClick = {
                            controller.setTileAsset(asset.id)
                            controller.tool = EditorTool.PAINT
                            controller.pane = EditorPane.SCENE
                        },
                        color = if (controller.selectedTileAssetId == asset.id) EditorColors.accentDark else EditorColors.panelSelected,
                        shape = RoundedCornerShape(6.dp),
                    ) {
                        Text(asset.name, Modifier.padding(horizontal = 8.dp, vertical = 5.dp), color = EditorColors.text, fontSize = 9.sp)
                    }
                }
            }
        }
        if (!compact) Text("Drag across the scene to paint. Use the Erase tool to remove cells.", color = EditorColors.subtle, fontSize = 9.sp)
    }
}

@Composable
fun SpriteEditorPanel(
    controller: EditorController,
    onImport: () -> Unit,
    modifier: Modifier = Modifier,
    compact: Boolean = false,
) {
    val selectedObject = controller.selectedObject
    val sprite = selectedObject?.sprite
    val selectedAsset = controller.document.assets.firstOrNull { it.id == sprite?.assetId }
        ?: controller.selectedAsset?.takeIf { it.kind == AssetKind.SPRITE }
    val image = produceState<ImageBitmap?>(null, selectedAsset?.path) {
        value = withContext(Dispatchers.IO) { selectedAsset?.let(::decodeSprite) }
    }.value
    Column(modifier.background(EditorColors.panel)) {
        if (!compact) PanelHeaderRow("SPRITE EDITOR", selectedAsset?.name ?: "No sprite selected", actionLabel = "Import", onAction = onImport)
        if (selectedAsset == null || image == null) {
            Column(
                Modifier.fillMaxSize().padding(14.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Text("Choose an image sprite to edit its crop.", color = EditorColors.text, fontSize = 11.sp)
                Text("Import a texture or select one from the Assets panel.", color = EditorColors.muted, fontSize = 9.sp)
                if (selectedAsset != null && selectedObject != null) {
                    Text("This file could not be decoded as an image.", color = EditorColors.red, fontSize = 9.sp)
                }
            }
        } else {
            Row(Modifier.fillMaxSize().padding(horizontal = 10.dp, vertical = 7.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Canvas(Modifier.weight(1f).fillMaxSize().clip(RoundedCornerShape(8.dp)).background(Color(0xFF0A111A))) {
                    val cell = 14f
                    var y = 0f
                    while (y < size.height) {
                        var x = 0f
                        while (x < size.width) {
                            drawRect(if ((x / cell).toInt() % 2 == (y / cell).toInt() % 2) Color(0xFF172331) else Color(0xFF111B27), Offset(x, y), Size(cell, cell))
                            x += cell
                        }
                        y += cell
                    }
                    val cropWidth = sprite?.sourceWidth?.takeIf { it > 0 } ?: image.width
                    val cropHeight = sprite?.sourceHeight?.takeIf { it > 0 } ?: image.height
                    val factor = minOf(size.width / cropWidth, size.height / cropHeight)
                    val drawWidth = cropWidth * factor
                    val drawHeight = cropHeight * factor
                    val offsetX = ((size.width - drawWidth) * 0.5f).toInt()
                    val offsetY = ((size.height - drawHeight) * 0.5f).toInt()
                    val sourceX = sprite?.sourceX?.coerceIn(0, image.width - 1) ?: 0
                    val sourceY = sprite?.sourceY?.coerceIn(0, image.height - 1) ?: 0
                    val safeWidth = cropWidth.coerceAtMost(image.width - sourceX).coerceAtLeast(1)
                    val safeHeight = cropHeight.coerceAtMost(image.height - sourceY).coerceAtLeast(1)
                    drawImage(
                        image,
                        srcOffset = androidx.compose.ui.unit.IntOffset(sourceX, sourceY),
                        srcSize = androidx.compose.ui.unit.IntSize(safeWidth, safeHeight),
                        dstOffset = androidx.compose.ui.unit.IntOffset(offsetX, offsetY),
                        dstSize = androidx.compose.ui.unit.IntSize(drawWidth.toInt().coerceAtLeast(1), drawHeight.toInt().coerceAtLeast(1)),
                    )
                    drawRect(
                        color = EditorColors.accent,
                        topLeft = Offset(offsetX.toFloat(), offsetY.toFloat()),
                        size = Size(drawWidth, drawHeight),
                        style = Stroke(1.5f, pathEffect = PathEffect.dashPathEffect(floatArrayOf(6f, 4f))),
                    )
                }
                Column(
                    Modifier.width(if (compact) 160.dp else 205.dp).fillMaxSize().verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Text(selectedAsset.name, color = EditorColors.text, fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text("${image.width} × ${image.height} px", color = EditorColors.muted, fontSize = 9.sp)
                    if (sprite != null && sprite.assetId == selectedAsset.id) {
                        CropField("X", sprite.sourceX) { value -> updateCrop(controller, selectedObject?.id, sprite) { it.copy(sourceX = value) } }
                        CropField("Y", sprite.sourceY) { value -> updateCrop(controller, selectedObject?.id, sprite) { it.copy(sourceY = value) } }
                        CropField("Width", sprite.sourceWidth.takeIf { it > 0 } ?: image.width) { value -> updateCrop(controller, selectedObject?.id, sprite) { it.copy(sourceWidth = value.coerceAtMost(image.width)) } }
                        CropField("Height", sprite.sourceHeight.takeIf { it > 0 } ?: image.height) { value -> updateCrop(controller, selectedObject?.id, sprite) { it.copy(sourceHeight = value.coerceAtMost(image.height)) } }
                    } else {
                        Button(
                            onClick = {
                                val targetId = selectedObject?.id
                                    ?: controller.addObject(com.boxengine.app.editor.model.EditorObjectKind.SPRITE)
                                controller.assignSpriteAsset(targetId, selectedAsset.id)
                            },
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(8.dp),
                            colors = ButtonDefaults.buttonColors(containerColor = EditorColors.accent, contentColor = Color(0xFF071512)),
                        ) {
                            Text(
                                if (selectedObject == null) "Create sprite from texture" else "Assign texture to selected",
                                fontSize = 9.sp,
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun AnimationPanel(controller: EditorController, modifier: Modifier = Modifier, compact: Boolean = false) {
    val selected = controller.selectedObject
    val keys = controller.document.keyframes.sortedBy { it.timeSeconds }
    Column(modifier.background(EditorColors.panel).padding(horizontal = 10.dp, vertical = if (compact) 4.dp else 10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(7.dp)) {
            if (!compact) Text("ANIMATION TIMELINE", color = EditorColors.muted, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.1.sp)
            Text(selected?.name ?: "No object selected", color = EditorColors.text, fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.weight(1f))
            Text("${String.format(Locale.US, "%.2f", controller.timelineTime)} s", color = EditorColors.muted, fontSize = 9.sp)
            TextButton(onClick = { controller.addAnimationKeyframe() }, enabled = selected != null) {
                Text("+ Key", color = EditorColors.accent, fontSize = 10.sp)
            }
            TextButton(onClick = { controller.setAnimationPreviewState(!controller.animationPreviewPlaying) }, enabled = keys.isNotEmpty()) {
                Text(if (controller.animationPreviewPlaying) "Ⅱ" else "▶", color = EditorColors.accent, fontSize = 11.sp)
            }
        }
        Slider(
            value = controller.timelineTime.coerceIn(0f, maxOf(controller.animationDuration, 1f)),
            onValueChange = { controller.timelineTime = it },
            valueRange = 0f..maxOf(controller.animationDuration, 1f),
            modifier = Modifier.fillMaxWidth().height(if (compact) 24.dp else 34.dp),
        )
        if (keys.isEmpty()) {
            Text("Add a transform keyframe for the selected object. Position, rotation, and scale are interpolated between keys.", color = EditorColors.subtle, fontSize = 9.sp)
        } else {
            LazyColumn(Modifier.weight(1f).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                items(keys, key = { it.id }) { key ->
                    val objectName = controller.document.objects.firstOrNull { it.id == key.objectId }?.name ?: "Deleted object"
                    Row(
                        Modifier.fillMaxWidth().clip(RoundedCornerShape(6.dp)).background(EditorColors.panelRaised)
                            .clickable { controller.selectObject(key.objectId); controller.timelineTime = key.timeSeconds }
                            .padding(horizontal = 8.dp, vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(7.dp),
                    ) {
                        Text("◆", color = EditorColors.amber, fontSize = 10.sp)
                        Text(String.format(Locale.US, "%.2f s", key.timeSeconds), color = EditorColors.text, fontSize = 9.sp)
                        Text(objectName, color = EditorColors.muted, fontSize = 9.sp, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text("X ${String.format(Locale.US, "%.1f", key.transform.x)} · Y ${String.format(Locale.US, "%.1f", key.transform.y)}", color = EditorColors.subtle, fontSize = 8.sp)
                        Text("×", modifier = Modifier.clickable { controller.removeAnimationKeyframe(key.id) }.padding(horizontal = 5.dp), color = EditorColors.red, fontSize = 13.sp)
                    }
                }
            }
        }
    }
}

@Composable
fun ConsolePanel(controller: EditorController, modifier: Modifier = Modifier, compact: Boolean = false) {
    var command by remember { mutableStateOf("") }
    Column(modifier.background(EditorColors.panel).padding(horizontal = 9.dp, vertical = if (compact) 4.dp else 9.dp)) {
        Row(Modifier.fillMaxWidth().height(30.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(if (compact) "Console" else "CONSOLE / LOG", color = EditorColors.muted, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.1.sp)
            Spacer(Modifier.weight(1f))
            TextButton(onClick = { controller.consoleLines.clear() }) { Text("Clear", color = EditorColors.muted, fontSize = 9.sp) }
        }
        LazyColumn(
            modifier = Modifier.weight(1f).fillMaxWidth(),
            reverseLayout = true,
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            items(controller.consoleLines.reversed().size) { index ->
                val line = controller.consoleLines[controller.consoleLines.lastIndex - index]
                Text(
                    line,
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 1.dp),
                    color = when {
                        line.startsWith(">") -> EditorColors.accent
                        line.contains("failed", true) || line.contains("error", true) -> EditorColors.red
                        else -> EditorColors.muted
                    },
                    fontSize = 9.sp,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(5.dp)) {
            OutlinedTextField(
                value = command,
                onValueChange = { command = it.take(240) },
                modifier = Modifier.weight(1f).height(52.dp),
                singleLine = true,
                placeholder = { Text("Type a command…", color = EditorColors.subtle, fontSize = 10.sp) },
                textStyle = androidx.compose.ui.text.TextStyle(color = EditorColors.text, fontSize = 10.sp),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = { submitConsole(controller, command) { command = it } }),
                colors = androidx.compose.material3.TextFieldDefaults.colors(
                    focusedContainerColor = EditorColors.panelRaised,
                    unfocusedContainerColor = EditorColors.panelRaised,
                    focusedTextColor = EditorColors.text,
                    unfocusedTextColor = EditorColors.text,
                    focusedIndicatorColor = EditorColors.accent,
                    unfocusedIndicatorColor = EditorColors.border,
                    cursorColor = EditorColors.accent,
                ),
            )
            Button(
                onClick = { submitConsole(controller, command) { command = it } },
                modifier = Modifier.height(40.dp),
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.buttonColors(containerColor = EditorColors.accentDark),
            ) { Text("Run", fontSize = 9.sp, color = EditorColors.text) }
        }
    }
}

@Composable
private fun PanelHeaderRow(title: String, subtitle: String, actionLabel: String, onAction: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(37.dp).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        Text(title, color = EditorColors.muted, fontSize = 9.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.9.sp)
        Text(subtitle, color = EditorColors.subtle, fontSize = 8.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Spacer(Modifier.weight(1f))
        TextButton(onClick = onAction) { Text(actionLabel, color = EditorColors.accent, fontSize = 9.sp) }
    }
}

@Composable
private fun CropField(label: String, value: Int, onCommit: (Int) -> Unit) {
    var text by remember(value) { mutableStateOf(value.toString()) }
    var focused by remember { mutableStateOf(false) }
    fun commit() {
        val parsed = text.toIntOrNull()
        if (parsed == null) text = value.toString() else {
            val safe = parsed.coerceAtLeast(0)
            text = safe.toString()
            if (safe != value) onCommit(safe)
        }
    }
    OutlinedTextField(
        value = text,
        onValueChange = { if (it.length <= 6 && it.all(Char::isDigit)) text = it },
        modifier = Modifier.fillMaxWidth().height(53.dp).onFocusChanged {
            if (focused && !it.isFocused) commit()
            focused = it.isFocused
        },
        label = { Text(label, fontSize = 8.sp) },
        singleLine = true,
        textStyle = androidx.compose.ui.text.TextStyle(color = EditorColors.text, fontSize = 10.sp),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(onDone = { commit() }),
        colors = androidx.compose.material3.TextFieldDefaults.colors(
            focusedContainerColor = EditorColors.panelSelected,
            unfocusedContainerColor = EditorColors.panelSelected,
            focusedTextColor = EditorColors.text,
            unfocusedTextColor = EditorColors.text,
            focusedIndicatorColor = EditorColors.accent,
            unfocusedIndicatorColor = EditorColors.border,
            cursorColor = EditorColors.accent,
        ),
    )
}

private fun updateCrop(
    controller: EditorController,
    objectId: String?,
    sprite: SpriteComponent?,
    change: (SpriteComponent) -> SpriteComponent,
) {
    if (objectId == null || sprite == null) return
    controller.updateSprite(objectId, change)
}

private fun decodeSprite(asset: SpriteAsset): ImageBitmap? = try {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(asset.path, bounds)
    var sample = 1
    while (bounds.outWidth / sample > 2048 || bounds.outHeight / sample > 2048) sample *= 2
    BitmapFactory.decodeFile(asset.path, BitmapFactory.Options().apply { inSampleSize = sample })?.asImageBitmap()
} catch (_: Exception) {
    null
}

private fun submitConsole(controller: EditorController, command: String, update: (String) -> Unit) {
    if (command.isBlank()) return
    controller.executeConsole(command)
    update("")
}

private fun formatAssetSize(value: Long): String = when {
    value >= 1024L * 1024L -> "%.1f MiB".format(Locale.US, value / (1024f * 1024f))
    value >= 1024L -> "%.1f KiB".format(Locale.US, value / 1024f)
    else -> "$value B"
}

private fun assetGlyph(kind: AssetKind): String = when (kind) {
    AssetKind.SPRITE -> "▧"
    AssetKind.AUDIO -> "♫"
    AssetKind.SCRIPT -> "{}"
    AssetKind.SCENE -> "◈"
    AssetKind.MODEL -> "⬡"
    AssetKind.OTHER -> "·"
}

private fun Float.toIntOrHalf(): String = if (this == 0.5f) "½" else toInt().toString()
