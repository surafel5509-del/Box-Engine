// Path: app/src/main/kotlin/com/boxengine/app/editor/ui/SceneViewport.kt
package com.boxengine.app.editor.ui

import android.graphics.BitmapFactory
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.produceState
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import com.boxengine.app.editor.model.EditorObject
import com.boxengine.app.editor.model.EditorObjectKind
import com.boxengine.app.editor.model.EditorTool
import com.boxengine.app.editor.model.PlayState
import com.boxengine.app.editor.model.Primitive3D
import com.boxengine.app.editor.model.SceneDimension
import com.boxengine.app.editor.model.SpriteAsset
import com.boxengine.app.editor.model.Transform3D
import com.boxengine.app.editor.model.Vec2
import com.boxengine.app.editor.model.AssetKind
import com.boxengine.app.editor.model.EditorController
import com.boxengine.app.editor.model.TileCell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/** Draws and edits the Compose-based 2D scene canvas or projected 3D wireframe. */
@Composable
fun SceneViewport(
    controller: EditorController,
    isGameView: Boolean,
    modifier: Modifier = Modifier,
) {
    if (controller.document.dimension == SceneDimension.THREE_D && !isGameView) {
        Scene3DViewport(controller, modifier)
    } else {
        Scene2DViewport(controller, isGameView, modifier)
    }
}

@Composable
private fun Scene2DViewport(
    controller: EditorController,
    isGameView: Boolean,
    modifier: Modifier = Modifier,
) {
    val document = controller.document
    val imageBitmaps = produceState<Map<String, ImageBitmap>>(emptyMap(), document.assets) {
        value = withContext(Dispatchers.IO) {
            document.assets.asSequence()
                .filter { it.kind == AssetKind.SPRITE && it.path.isNotBlank() }
                .mapNotNull { asset -> decodeAsset(asset)?.let { asset.id to it } }
                .toMap()
        }
    }.value

    val canvasModifier = if (isGameView || controller.playState != PlayState.STOPPED) {
        Modifier
    } else {
        Modifier.pointerInput(controller, isGameView) {
            val canvasSize = size
            if (canvasSize.width <= 0 || canvasSize.height <= 0) return@pointerInput
            val baseScale = min(canvasSize.width / 16f, canvasSize.height / 9f).coerceAtLeast(1f)
            fun screenToWorld(point: Offset): Vec2 = Vec2(
                (point.x - canvasSize.width * 0.5f - controller.viewportPan.x) /
                    (baseScale * controller.viewportZoom),
                -(point.y - canvasSize.height * 0.5f - controller.viewportPan.y) /
                    (baseScale * controller.viewportZoom),
            )
            var gestureBefore = controller.document
            var shouldRecordGesture = false
            var dragAction = DragAction.NONE
            var dragObjectId: String? = null
            var dragStartPosition = Vec2()
            var dragStartTransform = Transform3D()
            var lastCentroid = Offset.Zero
            var lastDistance = 0f
            var hasMultiTouch = false

            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Main)
                val startPoint = down.position
                val startWorld = screenToWorld(startPoint)
                gestureBefore = controller.beginGesture()
                shouldRecordGesture = false
                hasMultiTouch = false
                val hit = hitTest2D(controller, startWorld, isGameView = false)
                when (controller.tool) {
                    EditorTool.PAINT -> {
                        shouldRecordGesture = true
                        dragAction = DragAction.PAINT
                        controller.paintTile(startWorld.x, startWorld.y)
                    }
                    EditorTool.ERASE -> {
                        shouldRecordGesture = true
                        dragAction = DragAction.ERASE
                        controller.eraseTile(startWorld.x, startWorld.y)
                    }
                    EditorTool.PAN -> dragAction = DragAction.PAN
                    EditorTool.MOVE, EditorTool.ROTATE, EditorTool.SCALE -> {
                        if (hit != null) controller.selectObject(hit.id)
                        val selected = controller.selectedObject
                        if (selected != null && selected.kind != EditorObjectKind.CAMERA) {
                            dragObjectId = selected.id
                            dragStartTransform = selected.transform
                            dragStartPosition = startWorld
                            dragAction = when (controller.tool) {
                                EditorTool.MOVE -> DragAction.MOVE
                                EditorTool.ROTATE -> DragAction.ROTATE
                                EditorTool.SCALE -> DragAction.SCALE
                                else -> DragAction.NONE
                            }
                            shouldRecordGesture = dragAction != DragAction.NONE
                        } else {
                            dragAction = DragAction.PAN
                        }
                    }
                    EditorTool.SELECT -> {
                        controller.selectObject(hit?.id)
                        if (hit != null && hit.kind != EditorObjectKind.CAMERA) {
                            dragObjectId = hit.id
                            dragStartTransform = hit.transform
                            dragStartPosition = startWorld
                            dragAction = DragAction.MOVE
                            shouldRecordGesture = true
                        } else {
                            dragAction = DragAction.PAN
                        }
                    }
                }
                down.consume()
                var gestureActive = true
                while (gestureActive) {
                    val event = awaitPointerEvent(PointerEventPass.Main)
                    val pressed = event.changes.filter { it.pressed }
                    if (pressed.size >= 2) {
                        hasMultiTouch = true
                        val first = pressed[0].position
                        val second = pressed[1].position
                        val centroid = Offset((first.x + second.x) * 0.5f, (first.y + second.y) * 0.5f)
                        val distance = (first - second).getDistance().coerceAtLeast(1f)
                        if (lastDistance > 0f) {
                            val zoomFactor = (distance / lastDistance).coerceIn(0.82f, 1.22f)
                            val oldZoom = controller.viewportZoom
                            val newZoom = (oldZoom * zoomFactor).coerceIn(0.25f, 5f)
                            val centerX = canvasSize.width * 0.5f
                            val centerY = canvasSize.height * 0.5f
                            controller.viewportPan = Offset(
                                centroid.x - centerX - (centroid.x - centerX - controller.viewportPan.x) * (newZoom / oldZoom),
                                centroid.y - centerY - (centroid.y - centerY - controller.viewportPan.y) * (newZoom / oldZoom),
                            )
                            controller.viewportZoom = newZoom
                        }
                        if (lastCentroid != Offset.Zero) {
                            controller.viewportPan += centroid - lastCentroid
                        }
                        lastCentroid = centroid
                        lastDistance = distance
                        event.changes.forEach { it.consume() }
                    } else if (pressed.size == 1) {
                        val change = pressed.first()
                        val delta = change.position - change.previousPosition
                        if (hasMultiTouch) {
                            controller.viewportPan += delta
                        } else {
                            when (dragAction) {
                                DragAction.PAN -> controller.viewportPan += delta
                                DragAction.MOVE -> {
                                    val id = dragObjectId
                                    if (id != null) {
                                        val world = screenToWorld(change.position)
                                        val worldDelta = world - dragStartPosition
                                        val updated = dragStartTransform.copy(
                                            x = controller.snap(dragStartTransform.x + worldDelta.x),
                                            y = controller.snap(dragStartTransform.y + worldDelta.y),
                                        )
                                        controller.updateTransform(id, updated, recordUndo = false)
                                    }
                                }
                                DragAction.ROTATE -> {
                                    val id = dragObjectId
                                    if (id != null) {
                                        val degrees = (change.position.x - startPoint.x) / baseScale * 55f
                                        val updated = dragStartTransform.copy(
                                            rotationZ = controller.snap(dragStartTransform.rotationZ + degrees),
                                        )
                                        controller.updateTransform(id, updated, recordUndo = false)
                                    }
                                }
                                DragAction.SCALE -> {
                                    val id = dragObjectId
                                    if (id != null) {
                                        val changeScale = (change.position.x - startPoint.x) / baseScale
                                        val updated = dragStartTransform.copy(
                                            scaleX = (dragStartTransform.scaleX + changeScale).coerceIn(0.05f, 20f),
                                            scaleY = (dragStartTransform.scaleY + changeScale).coerceIn(0.05f, 20f),
                                        )
                                        controller.updateTransform(id, updated, recordUndo = false)
                                    }
                                }
                                DragAction.PAINT -> {
                                    val world = screenToWorld(change.position)
                                    controller.paintTile(world.x, world.y)
                                    shouldRecordGesture = true
                                }
                                DragAction.ERASE -> {
                                    val world = screenToWorld(change.position)
                                    controller.eraseTile(world.x, world.y)
                                    shouldRecordGesture = true
                                }
                                DragAction.NONE -> Unit
                            }
                        }
                        change.consume()
                        lastCentroid = Offset.Zero
                        lastDistance = 0f
                    } else {
                        lastCentroid = Offset.Zero
                        lastDistance = 0f
                        if (event.changes.none { it.pressed }) gestureActive = false
                    }
                    if (event.changes.none { it.pressed }) gestureActive = false
                }
                if (shouldRecordGesture) controller.finishGesture(gestureBefore)
                dragAction = DragAction.NONE
                dragObjectId = null
            }
        }
    }

    Box(modifier.background(EditorColors.background)) {
        Canvas(modifier = Modifier.fillMaxSize().then(canvasModifier)) {
            drawRect(EditorColors.background)
            val baseScale = min(size.width / 16f, size.height / 9f).coerceAtLeast(1f)
            val runtimeView = isGameView || controller.playState != PlayState.STOPPED
            val primaryCamera = document.objects.firstOrNull { it.kind == EditorObjectKind.CAMERA && it.camera?.primary == true }
            val cameraTransform = primaryCamera?.let { controller.worldTransform(it.id) ?: it.transform }
            val cameraSize = (primaryCamera?.camera?.orthographicSize ?: 4.5f).coerceAtLeast(0.1f)
            val aspect = (size.width / size.height.coerceAtLeast(1f)).coerceAtLeast(0.1f)
            val cameraScale = min(size.width / (2f * cameraSize * aspect), size.height / (2f * cameraSize))
            val scale = if (runtimeView) cameraScale else baseScale * controller.viewportZoom
            val pan = if (runtimeView) Offset.Zero else controller.viewportPan
            val cameraX = if (runtimeView) cameraTransform?.x ?: 0f else 0f
            val cameraY = if (runtimeView) cameraTransform?.y ?: 0f else 0f
            fun project(worldX: Float, worldY: Float): Offset = Offset(
                size.width * 0.5f + pan.x + (worldX - cameraX) * scale,
                size.height * 0.5f + pan.y - (worldY - cameraY) * scale,
            )

            drawRect(Color(0xFF0B121B))
            val gridSpacing = (document.gridSize * scale).coerceAtLeast(1f)
            if (gridSpacing >= 8f) {
                val gridColor = Color(0xFF172331)
                var x = (size.width * 0.5f + pan.x) % gridSpacing
                while (x < size.width) {
                    if (x >= 0f) drawLine(gridColor, Offset(x, 0f), Offset(x, size.height), 1f)
                    x += gridSpacing
                }
                var y = (size.height * 0.5f + pan.y) % gridSpacing
                while (y < size.height) {
                    if (y >= 0f) drawLine(gridColor, Offset(0f, y), Offset(size.width, y), 1f)
                    y += gridSpacing
                }
            }
            val origin = project(0f, 0f)
            drawLine(Color(0xFF26384B), Offset(0f, origin.y), Offset(size.width, origin.y), 1.25f)
            drawLine(Color(0xFF26384B), Offset(origin.x, 0f), Offset(origin.x, size.height), 1.25f)
            drawCircle(EditorColors.blue.copy(alpha = 0.75f), radius = 3.5f, center = origin)

            drawTiles(document.tiles, imageBitmaps, ::project, scale, controller)
            val objects = document.objects
                .asSequence()
                .filter { it.visible && it.kind != EditorObjectKind.MESH3D && it.kind != EditorObjectKind.EMPTY && it.kind != EditorObjectKind.AUDIO_SOURCE }
                .filterNot { isGameView && (it.id in controller.collectedObjectIds || it.kind == EditorObjectKind.CAMERA) }
                .sortedBy { it.sprite?.sortingOrder ?: 0 }
                .toList()
            objects.forEach { objectValue ->
                val world = if (isGameView) controller.runtimePosition(objectValue) else controller.previewTransform(objectValue).let { Vec2(it.x, it.y) }
                val transform = controller.previewTransform(objectValue)
                val center = project(world.x, world.y)
                val sprite = objectValue.sprite
                when (objectValue.kind) {
                    EditorObjectKind.CAMERA -> {
                        val halfHeight = (objectValue.camera?.orthographicSize ?: 4.5f) * scale
                        val halfWidth = halfHeight * aspect
                        drawRect(
                            color = Color(0xFF62A8E9).copy(alpha = 0.75f),
                            topLeft = Offset(center.x - halfWidth, center.y - halfHeight),
                            size = Size(halfWidth * 2f, halfHeight * 2f),
                            style = Stroke(1.2f, pathEffect = PathEffect.dashPathEffect(floatArrayOf(7f, 5f))),
                        )
                        drawLine(Color(0xFF62A8E9), center - Offset(7f, 0f), center + Offset(7f, 0f), 1.5f)
                        drawLine(Color(0xFF62A8E9), center - Offset(0f, 7f), center + Offset(0f, 7f), 1.5f)
                    }
                    EditorObjectKind.TILEMAP -> {
                        val collider = objectValue.collider
                        if (collider != null && objectValue.id == controller.selectedObjectId) {
                            val width = collider.width * scale
                            val height = collider.height * scale
                            drawRect(
                                color = EditorColors.amber.copy(alpha = 0.9f),
                                topLeft = center + Offset(collider.offsetX * scale - width * 0.5f, -collider.offsetY * scale - height * 0.5f),
                                size = Size(width, height),
                                style = Stroke(1.5f),
                            )
                        }
                    }
                    else -> {
                        val width = (sprite?.width ?: objectValue.collider?.width ?: 1f) * abs(transform.scaleX) * scale
                        val height = (sprite?.height ?: objectValue.collider?.height ?: 1f) * abs(transform.scaleY) * scale
                        val color = sprite?.let { Color(it.tintArgb.toInt()) } ?: EditorColors.accent
                        val image = sprite?.assetId?.let(imageBitmaps::get)
                        val sourceWidth = sprite?.let { if (it.sourceWidth > 0) it.sourceWidth else image?.width } ?: null
                        val sourceHeight = sprite?.let { if (it.sourceHeight > 0) it.sourceHeight else image?.height } ?: null
                        withTransform({ rotate(transform.rotationZ, center) }) {
                            if (image != null) {
                                val sourceOffset = IntOffset(
                                    (sprite?.sourceX ?: 0).coerceIn(0, max(0, image.width - 1)),
                                    (sprite?.sourceY ?: 0).coerceIn(0, max(0, image.height - 1)),
                                )
                                val srcWidth = (sourceWidth ?: image.width).coerceAtMost(image.width - sourceOffset.x).coerceAtLeast(1)
                                val srcHeight = (sourceHeight ?: image.height).coerceAtMost(image.height - sourceOffset.y).coerceAtLeast(1)
                                drawImage(
                                    image = image,
                                    srcOffset = sourceOffset,
                                    srcSize = IntSize(srcWidth, srcHeight),
                                    dstOffset = IntOffset((center.x - width * 0.5f).toInt(), (center.y - height * 0.5f).toInt()),
                                    dstSize = IntSize(width.toInt().coerceAtLeast(1), height.toInt().coerceAtLeast(1)),
                                )
                            } else if (objectValue.name.contains("coin", ignoreCase = true)) {
                                drawCircle(color, radius = min(width, height) * 0.5f, center = center)
                                drawCircle(Color(0xFFFFE2A3), radius = min(width, height) * 0.24f, center = center)
                            } else {
                                drawRoundRect(
                                    color = color,
                                    topLeft = Offset(center.x - width * 0.5f, center.y - height * 0.5f),
                                    size = Size(width, height),
                                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(min(9f, height * 0.16f)),
                                )
                            }
                            if (objectValue.collider != null && objectValue.id == controller.selectedObjectId) {
                                val colliderWidth = objectValue.collider.width * scale
                                val colliderHeight = objectValue.collider.height * scale
                                drawRect(
                                    color = if (objectValue.collider.isTrigger) EditorColors.amber else EditorColors.accent,
                                    topLeft = center + Offset(objectValue.collider.offsetX * scale - colliderWidth * 0.5f, -objectValue.collider.offsetY * scale - colliderHeight * 0.5f),
                                    size = Size(colliderWidth, colliderHeight),
                                    style = Stroke(1f, pathEffect = PathEffect.dashPathEffect(floatArrayOf(5f, 4f))),
                                )
                            }
                            if (objectValue.id == controller.selectedObjectId && !isGameView) {
                                val selectedColor = EditorColors.accent
                                drawRoundRect(
                                    color = selectedColor,
                                    topLeft = Offset(center.x - width * 0.5f - 4f, center.y - height * 0.5f - 4f),
                                    size = Size(width + 8f, height + 8f),
                                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(3f),
                                    style = Stroke(1.6f),
                                )
                                drawCircle(selectedColor, radius = 3.5f, center = Offset(center.x + width * 0.5f + 4f, center.y - height * 0.5f - 4f))
                            }
                        }
                    }
                }
            }
            if (isGameView) {
                drawRoundRect(
                    color = Color(0xFF4BD6C1).copy(alpha = 0.12f),
                    topLeft = Offset(8f, 8f),
                    size = Size(146f, 30f),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(8f),
                )
            }
        }
    }
}

private fun decodeAsset(asset: SpriteAsset): ImageBitmap? = try {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(asset.path, bounds)
    var sample = 1
    while (bounds.outWidth / sample > 2048 || bounds.outHeight / sample > 2048) sample *= 2
    BitmapFactory.decodeFile(asset.path, BitmapFactory.Options().apply { inSampleSize = sample })?.asImageBitmap()
} catch (_: Exception) {
    null
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawTiles(
    tiles: List<TileCell>,
    images: Map<String, ImageBitmap>,
    project: (Float, Float) -> Offset,
    scale: Float,
    controller: EditorController,
) {
    tiles.forEach { tile ->
        val owner = tile.ownerId?.let { ownerId -> controller.document.objects.firstOrNull { it.id == ownerId } }
        if (owner?.visible == false) return@forEach
        val transform = owner?.let { controller.worldTransform(it.id) ?: it.transform }
        val originX = transform?.x ?: 0f
        val originY = transform?.y ?: 0f
        val scaleX = transform?.scaleX ?: 1f
        val scaleY = transform?.scaleY ?: 1f
        val cellWidth = (abs(scaleX) * scale).coerceAtLeast(1f)
        val cellHeight = (abs(scaleY) * scale).coerceAtLeast(1f)
        val centerWorldX = originX + (tile.x + 0.5f) * scaleX
        val centerWorldY = originY + (tile.y + 0.5f) * scaleY
        val center = project(centerWorldX, centerWorldY)
        val topLeft = center - Offset(cellWidth * 0.5f, cellHeight * 0.5f)
        val pivot = project(originX, originY)
        val tileColor = Color(tile.colorArgb.toInt())
        val image = tile.assetId?.let(images::get)
        withTransform({ if (transform != null) rotate(-transform.rotationZ, pivot) }) {
            if (image != null) {
                drawImage(
                    image = image,
                    dstOffset = IntOffset(topLeft.x.toInt(), topLeft.y.toInt()),
                    dstSize = IntSize(cellWidth.toInt().coerceAtLeast(1), cellHeight.toInt().coerceAtLeast(1)),
                )
            } else {
                drawRect(tileColor, topLeft = topLeft, size = Size(cellWidth, cellHeight))
                drawRect(
                    Color.Black.copy(alpha = 0.11f),
                    topLeft = topLeft,
                    size = Size(cellWidth, cellHeight),
                    style = Stroke(width = 0.65f),
                )
                if (tile.y == 0) drawLine(
                    Color(0xFFB6CF78).copy(alpha = 0.55f),
                    Offset(topLeft.x, topLeft.y + 1f),
                    Offset(topLeft.x + cellWidth, topLeft.y + 1f),
                    1.2f,
                )
            }
        }
    }
}

private fun hitTest2D(controller: EditorController, position: Vec2, isGameView: Boolean): EditorObject? {
    return controller.document.objects.asReversed().firstOrNull { item ->
        if (!item.visible || item.kind == EditorObjectKind.MESH3D || item.kind == EditorObjectKind.EMPTY ||
            item.kind == EditorObjectKind.AUDIO_SOURCE) return@firstOrNull false
        if (isGameView && item.id in controller.collectedObjectIds) return@firstOrNull false
        val transform = controller.worldTransform(item.id) ?: item.transform
        if (item.kind == EditorObjectKind.TILEMAP) {
            val cells = controller.document.tiles.filter { it.ownerId == item.id }
            val radians = Math.toRadians(transform.rotationZ.toDouble())
            val dx = position.x - transform.x
            val dy = position.y - transform.y
            val localX = (dx * cos(radians) + dy * sin(radians)).toFloat()
            val localY = (-dx * sin(radians) + dy * cos(radians)).toFloat()
            val tileHit = cells.any { tile ->
                abs(localX - (tile.x + 0.5f) * transform.scaleX) <= abs(transform.scaleX) * 0.5f &&
                    abs(localY - (tile.y + 0.5f) * transform.scaleY) <= abs(transform.scaleY) * 0.5f
            }
            val collider = item.collider
            val colliderHit = collider != null &&
                abs(position.x - transform.x - collider.offsetX) <= collider.width * abs(transform.scaleX) * 0.5f &&
                abs(position.y - transform.y - collider.offsetY) <= collider.height * abs(transform.scaleY) * 0.5f
            return@firstOrNull tileHit || colliderHit
        }
        val halfWidth: Float
        val halfHeight: Float
        if (item.kind == EditorObjectKind.CAMERA) {
            val ortho = item.camera?.orthographicSize ?: 5f
            halfWidth = ortho * 1.78f
            halfHeight = ortho
        } else {
            halfWidth = (item.sprite?.width ?: item.collider?.width ?: 1f) * abs(transform.scaleX) * 0.5f
            halfHeight = (item.sprite?.height ?: item.collider?.height ?: 1f) * abs(transform.scaleY) * 0.5f
        }
        abs(position.x - transform.x) <= halfWidth && abs(position.y - transform.y) <= halfHeight
    }
}

private enum class DragAction { NONE, MOVE, ROTATE, SCALE, PAN, PAINT, ERASE }

private data class ProjectedPoint(val x: Float, val y: Float, val depth: Float)

@Composable
private fun Scene3DViewport(controller: EditorController, modifier: Modifier = Modifier) {
    val canvasModifier = Modifier.pointerInput(controller) {
        val canvasWidth = size.width.toFloat()
        val canvasHeight = size.height.toFloat()
        if (canvasWidth <= 0f || canvasHeight <= 0f) return@pointerInput
        var gestureBefore = controller.document
        var shouldRecord = false
        var selectedId: String? = null
        var startPosition = Offset.Zero
        var startTransform = Transform3D()
        var previousCentroid = Offset.Zero
        var previousDistance = 0f
        var multiTouch = false
        awaitEachGesture {
            val down = awaitFirstDown(requireUnconsumed = false)
            gestureBefore = controller.beginGesture()
            shouldRecord = false
            multiTouch = false
            previousCentroid = Offset.Zero
            previousDistance = 0f
            startPosition = down.position
            val hit = controller.document.objects.asReversed().firstOrNull { item ->
                if (item.kind != EditorObjectKind.MESH3D || !item.visible) return@firstOrNull false
                val world = controller.worldTransform(item.id) ?: item.transform
                val center = project3D(world, controller.cameraOrbit, controller.cameraZoom, canvasWidth, canvasHeight)
                (Offset(center.x, center.y) - down.position).getDistance() < 42f
            }
            if (controller.tool == EditorTool.SELECT || controller.tool == EditorTool.MOVE) {
                controller.selectObject(hit?.id)
                if (hit != null) {
                    selectedId = hit.id
                    startTransform = hit.transform
                    shouldRecord = true
                }
            } else if (controller.tool == EditorTool.ROTATE || controller.tool == EditorTool.SCALE) {
                selectedId = controller.selectedObjectId
                startTransform = controller.selectedObject?.transform ?: Transform3D()
                shouldRecord = selectedId != null
            } else {
                selectedId = null
            }
            down.consume()
            var active = true
            while (active) {
                val event = awaitPointerEvent()
                val pressed = event.changes.filter { it.pressed }
                if (pressed.size >= 2) {
                    multiTouch = true
                    val first = pressed[0].position
                    val second = pressed[1].position
                    val centroid = Offset((first.x + second.x) / 2f, (first.y + second.y) / 2f)
                    val distance = (first - second).getDistance().coerceAtLeast(1f)
                    if (previousDistance > 0f) {
                        val zoomChange = (distance / previousDistance).coerceIn(0.82f, 1.22f)
                        controller.cameraZoom = (controller.cameraZoom * zoomChange).coerceIn(0.35f, 3f)
                    }
                    if (previousCentroid != Offset.Zero) {
                        val delta = centroid - previousCentroid
                        controller.cameraOrbit = Offset(
                            controller.cameraOrbit.x + delta.x * 0.25f,
                            (controller.cameraOrbit.y + delta.y * 0.25f).coerceIn(-75f, 75f),
                        )
                    }
                    previousCentroid = centroid
                    previousDistance = distance
                    event.changes.forEach { it.consume() }
                } else if (pressed.size == 1) {
                    val change = pressed.first()
                    val delta = change.position - change.previousPosition
                    if (multiTouch || selectedId == null) {
                        controller.cameraOrbit = Offset(
                            controller.cameraOrbit.x + delta.x * 0.3f,
                            (controller.cameraOrbit.y + delta.y * 0.3f).coerceIn(-75f, 75f),
                        )
                    } else {
                        val id = selectedId
                        val totalDelta = change.position - startPosition
                        val worldDelta = totalDelta / (min(canvasWidth, canvasHeight) * 0.18f * controller.cameraZoom).coerceAtLeast(10f)
                        val transform = when (controller.tool) {
                            EditorTool.ROTATE -> startTransform.copy(rotationY = startTransform.rotationY + totalDelta.x * 0.55f)
                            EditorTool.SCALE -> {
                                val amount = totalDelta.x / 100f
                                startTransform.copy(
                                    scaleX = (startTransform.scaleX + amount).coerceIn(0.05f, 20f),
                                    scaleY = (startTransform.scaleY + amount).coerceIn(0.05f, 20f),
                                    scaleZ = (startTransform.scaleZ + amount).coerceIn(0.05f, 20f),
                                )
                            }
                            else -> startTransform.copy(
                                x = controller.snap(startTransform.x + worldDelta.x),
                                z = controller.snap(startTransform.z + worldDelta.y),
                            )
                        }
                        controller.updateTransform(id, transform, recordUndo = false)
                    }
                    change.consume()
                    previousCentroid = Offset.Zero
                    previousDistance = 0f
                } else {
                    previousCentroid = Offset.Zero
                    previousDistance = 0f
                }
                if (event.changes.none { it.pressed }) active = false
            }
            if (shouldRecord) controller.finishGesture(gestureBefore)
            selectedId = null
        }
    }
    Canvas(modifier = modifier.background(EditorColors.background).then(canvasModifier)) {
        drawRect(Color(0xFF0A111A))
        val center = Offset(size.width * 0.5f, size.height * 0.51f)
        val scale = min(size.width, size.height) * 0.18f * controller.cameraZoom
        fun projectPoint(x: Float, y: Float, z: Float) = project3D(
            Transform3D(x = x, y = y, z = z), controller.cameraOrbit, scale, center,
        )

        for (index in -12..12) {
            val startX = projectPoint(index.toFloat(), 0f, -12f)
            val endX = projectPoint(index.toFloat(), 0f, 12f)
            val startZ = projectPoint(-12f, 0f, index.toFloat())
            val endZ = projectPoint(12f, 0f, index.toFloat())
            drawLine(Color(0xFF1B2938), Offset(startX.x, startX.y), Offset(endX.x, endX.y), 0.8f)
            drawLine(Color(0xFF1B2938), Offset(startZ.x, startZ.y), Offset(endZ.x, endZ.y), 0.8f)
        }
        val axisX1 = projectPoint(-12f, 0f, 0f)
        val axisX2 = projectPoint(12f, 0f, 0f)
        val axisZ1 = projectPoint(0f, 0f, -12f)
        val axisZ2 = projectPoint(0f, 0f, 12f)
        val axisY1 = projectPoint(0f, -4f, 0f)
        val axisY2 = projectPoint(0f, 8f, 0f)
        drawLine(Color(0xFFE27479), Offset(axisX1.x, axisX1.y), Offset(axisX2.x, axisX2.y), 1.5f)
        drawLine(Color(0xFF65A9E9), Offset(axisZ1.x, axisZ1.y), Offset(axisZ2.x, axisZ2.y), 1.5f)
        drawLine(Color(0xFF72CE8C), Offset(axisY1.x, axisY1.y), Offset(axisY2.x, axisY2.y), 1.5f)

        controller.document.objects.filter { it.kind == EditorObjectKind.MESH3D && it.visible }.forEach { objectValue ->
            val t = controller.previewTransform(objectValue)
            val vertices = if (objectValue.primitive3D == Primitive3D.PLANE) {
                listOf(
                    floatArrayOf(-0.5f, 0f, -0.5f), floatArrayOf(0.5f, 0f, -0.5f),
                    floatArrayOf(0.5f, 0f, 0.5f), floatArrayOf(-0.5f, 0f, 0.5f),
                )
            } else {
                buildList {
                    for (z in listOf(-0.5f, 0.5f)) for (y in listOf(-0.5f, 0.5f)) for (x in listOf(-0.5f, 0.5f)) {
                        add(floatArrayOf(x, y, z))
                    }
                }
            }
            val worldPoints = vertices.map { vertex ->
                val rotated = rotatePoint(vertex[0] * t.scaleX, vertex[1] * t.scaleY, vertex[2] * t.scaleZ, t)
                projectPoint(t.x + rotated[0], t.y + rotated[1], t.z + rotated[2])
            }
            val color = if (objectValue.id == controller.selectedObjectId) EditorColors.accent else Color(0xFF7A95B1)
            if (objectValue.primitive3D == Primitive3D.CUBE) {
                val faces = listOf(listOf(0, 1, 3, 2), listOf(4, 5, 7, 6), listOf(0, 1, 5, 4), listOf(2, 3, 7, 6), listOf(0, 2, 6, 4), listOf(1, 3, 7, 5))
                faces.forEachIndexed { faceIndex, face ->
                    val path = Path().apply {
                        moveTo(worldPoints[face.first()].x, worldPoints[face.first()].y)
                        face.drop(1).forEach { index -> lineTo(worldPoints[index].x, worldPoints[index].y) }
                        close()
                    }
                    drawPath(color.copy(alpha = if (faceIndex % 2 == 0) 0.22f else 0.12f), path)
                }
                val edges = listOf(
                    0 to 1, 0 to 2, 0 to 4, 1 to 3, 1 to 5, 2 to 3,
                    2 to 6, 3 to 7, 4 to 5, 4 to 6, 5 to 7, 6 to 7,
                )
                edges.forEach { (a, b) ->
                    drawLine(color, Offset(worldPoints[a].x, worldPoints[a].y), Offset(worldPoints[b].x, worldPoints[b].y), if (objectValue.id == controller.selectedObjectId) 2f else 1.25f)
                }
            } else {
                for (i in 0..3) drawLine(color, Offset(worldPoints[i].x, worldPoints[i].y), Offset(worldPoints[(i + 1) % 4].x, worldPoints[(i + 1) % 4].y), 2f)
                drawLine(color.copy(alpha = 0.7f), Offset(worldPoints[0].x, worldPoints[0].y), Offset(worldPoints[2].x, worldPoints[2].y), 1f)
            }
            val projectedCenter = projectPoint(t.x, t.y, t.z)
            drawCircle(color, radius = 3.4f, center = Offset(projectedCenter.x, projectedCenter.y))
        }
    }
}

private fun project3D(
    transform: Transform3D,
    orbit: Offset,
    zoom: Float,
    width: Float,
    height: Float,
): ProjectedPoint {
    val center = Offset(width * 0.5f, height * 0.51f)
    return project3D(transform, orbit, min(width, height) * 0.18f * zoom, center)
}

private fun project3D(transform: Transform3D, orbit: Offset, focalScale: Float, center: Offset): ProjectedPoint {
    val yaw = orbit.x * PI.toFloat() / 180f
    val pitch = (22f + orbit.y) * PI.toFloat() / 180f
    val x1 = transform.x * cos(yaw) - transform.z * sin(yaw)
    val z1 = transform.x * sin(yaw) + transform.z * cos(yaw)
    val y2 = transform.y * cos(pitch) - z1 * sin(pitch)
    val z2 = transform.y * sin(pitch) + z1 * cos(pitch)
    val depth = (7.5f - z2).coerceAtLeast(1f)
    val factor = focalScale * 4.5f / depth
    return ProjectedPoint(center.x + x1 * factor, center.y - y2 * factor, depth)
}

private fun rotatePoint(x: Float, y: Float, z: Float, transform: Transform3D): FloatArray {
    val rx = transform.rotationX * PI.toFloat() / 180f
    val ry = transform.rotationY * PI.toFloat() / 180f
    val rz = transform.rotationZ * PI.toFloat() / 180f
    val y1 = y * cos(rx) - z * sin(rx)
    val z1 = y * sin(rx) + z * cos(rx)
    val x2 = x * cos(ry) + z1 * sin(ry)
    val z2 = -x * sin(ry) + z1 * cos(ry)
    val x3 = x2 * cos(rz) - y1 * sin(rz)
    val y3 = x2 * sin(rz) + y1 * cos(rz)
    return floatArrayOf(x3, y3, z2)
}
