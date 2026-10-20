// Path: app/src/main/kotlin/com/boxengine/app/editor/model/EditorController.kt
package com.boxengine.app.editor.model

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.graphics.BitmapFactory
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.ArrayDeque
import java.util.Locale
import java.util.UUID
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/** Owns editor state, scene history, project persistence, importing, and the small 2D runtime. */
class EditorController(context: Context) {
    private val appContext = context.applicationContext
    private val projectRoot = File(appContext.filesDir, "BoxEngineProjects/Starter2D")
    private val sceneFile = File(projectRoot, "Scenes/SampleScene.json")
    private val undoHistory = ArrayDeque<SceneDocument>()
    private val redoHistory = ArrayDeque<SceneDocument>()
    private val runtimeStartPositions = mutableMapOf<String, Vec2>()
    private var jumpQueued = false

    var document by mutableStateOf(loadSavedScene() ?: createStarterScene())
        private set
    var selectedObjectId by mutableStateOf(document.objects.firstOrNull { it.name == "Player" }?.id)
        private set
    var selectedAssetId by mutableStateOf<String?>(null)
        private set
    var pane by mutableStateOf(EditorPane.SCENE)
    var bottomPanel by mutableStateOf(BottomPanel.ASSETS)
    var tool by mutableStateOf(EditorTool.SELECT)
    var playState by mutableStateOf(PlayState.STOPPED)
        private set
    var viewportZoom by mutableFloatStateOf(1f)
    var viewportPan by mutableStateOf(Offset.Zero)
    var isDirty by mutableStateOf(false)
        private set
    var statusMessage by mutableStateOf("All changes saved")
        private set
    var gameInput by mutableIntStateOf(0)
        private set
    var runtimeBodies by mutableStateOf<Map<String, RuntimeBody>>(emptyMap())
        private set
    var collectedObjectIds by mutableStateOf<Set<String>>(emptySet())
        private set
    var gameTime by mutableFloatStateOf(0f)
        private set
    var timelineTime by mutableFloatStateOf(0f)
    var animationPreviewPlaying by mutableStateOf(false)
        private set
    var selectedTileColor by mutableStateOf(0xFF568A53L)
        private set
    var selectedTileAssetId by mutableStateOf<String?>(null)
        private set
    var cameraOrbit by mutableStateOf(Offset(0f, 0f))
    var cameraZoom by mutableFloatStateOf(1f)
    val consoleLines = mutableStateListOf("Box Engine editor ready.", "Type help to list console commands.")

    val selectedObject: EditorObject?
        get() = document.objects.firstOrNull { it.id == selectedObjectId }

    val selectedAsset: SpriteAsset?
        get() = document.assets.firstOrNull { it.id == selectedAssetId }

    val canUndo: Boolean
        get() = undoHistory.isNotEmpty()

    val canRedo: Boolean
        get() = redoHistory.isNotEmpty()

    val animationDuration: Float
        get() = document.keyframes.maxOfOrNull { it.timeSeconds } ?: 0f

    fun selectObject(id: String?) {
        selectedObjectId = id?.takeIf { candidate -> document.objects.any { it.id == candidate } }
    }

    fun newScene() {
        stopPlay()
        val camera = EditorObject(
            name = "Main Camera",
            kind = EditorObjectKind.CAMERA,
            transform = Transform3D(z = -10f),
            camera = CameraComponent(primary = true),
        )
        mutateDocument("Create scene") {
            SceneDocument(name = "Untitled Scene", objects = listOf(camera))
        }
        selectedObjectId = document.objects.firstOrNull()?.id
        pane = EditorPane.SCENE
        log("Created an empty scene.")
    }

    fun setCameraPrimary(id: String, primary: Boolean) {
        mutateDocument("Change primary camera") { current ->
            current.copy(objects = current.objects.map { item ->
                if (item.camera == null) item else item.copy(camera = item.camera.copy(
                    primary = if (item.id == id) primary else if (primary) false else item.camera.primary,
                ))
            })
        }
    }

    fun selectAsset(id: String?) {
        selectedAssetId = id?.takeIf { candidate -> document.assets.any { it.id == candidate } }
    }

    fun setDimension(dimension: SceneDimension) {
        if (document.dimension == dimension) return
        if (playState != PlayState.STOPPED) stopPlay()
        if (dimension == SceneDimension.THREE_D && tool in setOf(EditorTool.PAINT, EditorTool.ERASE)) {
            tool = EditorTool.SELECT
        }
        mutateDocument("Change scene view") { current ->
            val additions = if (dimension == SceneDimension.THREE_D &&
                current.objects.none { it.kind == EditorObjectKind.MESH3D }) {
                createStarter3DObjects()
            } else {
                emptyList()
            }
            val cameras = current.objects.map { item ->
                if (item.camera == null) item else item.copy(camera = item.camera.copy(
                    primary = if (dimension == SceneDimension.THREE_D) {
                        item.name == "Main Camera 3D"
                    } else {
                        item.name == "Main Camera"
                    },
                ))
            }
            current.copy(dimension = dimension, objects = cameras + additions)
        }
        selectedObjectId = document.objects.firstOrNull {
            if (dimension == SceneDimension.THREE_D) it.kind == EditorObjectKind.MESH3D
            else it.kind != EditorObjectKind.MESH3D
        }?.id
        log("Scene view set to ${if (dimension == SceneDimension.TWO_D) "2D" else "3D"}.")
    }

    fun addObject(kind: EditorObjectKind, parentId: String? = null): String {
        val nameBase = when (kind) {
            EditorObjectKind.EMPTY -> "GameObject"
            EditorObjectKind.SPRITE -> "Sprite"
            EditorObjectKind.TILEMAP -> "Tilemap"
            EditorObjectKind.CAMERA -> "Camera"
            EditorObjectKind.MESH3D -> "Cube"
            EditorObjectKind.AUDIO_SOURCE -> "Audio Source"
        }
        val name = uniqueName(nameBase)
        val objectValue = when (kind) {
            EditorObjectKind.CAMERA -> EditorObject(
                name = name,
                kind = kind,
                transform = Transform3D(z = -10f),
                camera = CameraComponent(primary = document.objects.none { it.camera?.primary == true }),
                parentId = parentId,
            )
            EditorObjectKind.SPRITE -> EditorObject(
                name = name,
                kind = kind,
                transform = Transform3D(x = 0.5f, y = 0.5f),
                sprite = SpriteComponent(width = 1f, height = 1f),
                parentId = parentId,
            )
            EditorObjectKind.TILEMAP -> EditorObject(
                name = name,
                kind = kind,
                transform = Transform3D(),
                body = PhysicsBody2D(BodyType2D.STATIC),
                parentId = parentId,
            )
            EditorObjectKind.MESH3D -> EditorObject(
                name = name,
                kind = kind,
                transform = Transform3D(y = 0.5f),
                parentId = parentId,
            )
            EditorObjectKind.AUDIO_SOURCE -> EditorObject(
                name = name,
                kind = kind,
                transform = Transform3D(),
                parentId = parentId,
            )
            EditorObjectKind.EMPTY -> EditorObject(name = name, kind = kind, parentId = parentId)
        }
        mutateDocument("Create $name") { it.copy(objects = it.objects + objectValue) }
        selectedObjectId = objectValue.id
        log("Created $name.")
        return objectValue.id
    }

    fun duplicateSelected(): String? {
        val selected = selectedObject ?: return null
        val copy = selected.copy(id = UUID.randomUUID().toString(), name = uniqueName("${selected.name} Copy"))
            .let { duplicate ->
                duplicate.copy(transform = duplicate.transform.copy(
                    x = duplicate.transform.x + 0.5f,
                    y = duplicate.transform.y + 0.5f,
                ))
            }
        val duplicatedTiles = if (selected.kind == EditorObjectKind.TILEMAP) {
            document.tiles.filter { it.ownerId == selected.id }.map { it.copy(ownerId = copy.id) }
        } else {
            emptyList()
        }
        mutateDocument("Duplicate ${selected.name}") { current ->
            current.copy(objects = current.objects + copy, tiles = current.tiles + duplicatedTiles)
        }
        selectedObjectId = copy.id
        log("Duplicated ${selected.name}.")
        return copy.id
    }

    fun deleteObject(id: String) {
        val target = document.objects.firstOrNull { it.id == id } ?: return
        val descendants = descendantsOf(id)
        val removedIds = descendants + id
        mutateDocument("Delete ${target.name}") { current ->
            current.copy(
                objects = current.objects.filterNot { it.id in removedIds },
                tiles = current.tiles.filterNot { it.ownerId != null && it.ownerId in removedIds },
                keyframes = current.keyframes.filterNot { it.objectId in removedIds },
            )
        }
        if (selectedObjectId?.let { it in removedIds } == true) selectedObjectId = null
        log("Deleted ${target.name} and ${descendants.size} child object(s).")
    }

    fun renameObject(id: String, newName: String) {
        val cleaned = newName.trim().take(48)
        if (cleaned.isEmpty()) return
        mutateDocument("Rename object") { current ->
            current.copy(objects = current.objects.map { if (it.id == id) it.copy(name = cleaned) else it })
        }
    }

    fun setObjectVisibility(id: String, visible: Boolean) {
        mutateDocument("Change object visibility") { current ->
            current.copy(objects = current.objects.map { if (it.id == id) it.copy(visible = visible) else it })
        }
    }

    fun moveObjectInHierarchy(id: String, delta: Int) {
        val current = document.objects.toMutableList()
        val index = current.indexOfFirst { it.id == id }
        if (index < 0) return
        val newIndex = (index + delta).coerceIn(0, current.lastIndex)
        if (index == newIndex) return
        val item = current.removeAt(index)
        current.add(newIndex, item)
        mutateDocument("Reorder hierarchy") { it.copy(objects = current) }
    }

    fun makeChildOf(objectId: String, parentId: String?) {
        if (objectId == parentId) return
        if (parentId != null && document.objects.none { it.id == parentId }) return
        if (parentId != null && objectId in descendantsOf(parentId)) return
        mutateDocument("Reparent object") { current ->
            current.copy(objects = current.objects.map {
                if (it.id == objectId) it.copy(parentId = parentId) else it
            })
        }
    }

    fun updateObject(id: String, recordUndo: Boolean = true, update: (EditorObject) -> EditorObject) {
        val before = document.objects.firstOrNull { it.id == id } ?: return
        val after = update(before)
        if (before == after) return
        mutateDocument("Edit ${before.name}", recordUndo) { current ->
            current.copy(objects = current.objects.map { if (it.id == id) after else it })
        }
    }

    fun updateTransform(id: String, transform: Transform3D, recordUndo: Boolean = true) {
        updateObject(id, recordUndo) { it.copy(transform = transform) }
    }

    fun addPhysicsBody(id: String) = updateObject(id) {
        it.copy(body = it.body ?: PhysicsBody2D())
    }

    fun removePhysicsBody(id: String) = updateObject(id) { it.copy(body = null) }

    fun addCollider(id: String) = updateObject(id) {
        it.copy(collider = it.collider ?: Collider2D())
    }

    fun removeCollider(id: String) = updateObject(id) { it.copy(collider = null) }

    fun assignSpriteAsset(objectId: String, assetId: String?) {
        val asset = document.assets.firstOrNull { it.id == assetId && it.kind == AssetKind.SPRITE }
        if (assetId != null && asset == null) return
        updateObject(objectId) { target ->
            val old = target.sprite ?: SpriteComponent()
            val width = if (asset != null && asset.width > 0) asset.width / 96f else old.width
            val height = if (asset != null && asset.height > 0) asset.height / 96f else old.height
            target.copy(
                kind = EditorObjectKind.SPRITE,
                sprite = old.copy(
                    assetId = asset?.id,
                    width = width.coerceIn(0.15f, 12f),
                    height = height.coerceIn(0.15f, 12f),
                    sourceX = 0,
                    sourceY = 0,
                    sourceWidth = asset?.width ?: 0,
                    sourceHeight = asset?.height ?: 0,
                ),
            )
        }
        if (asset != null) log("Assigned ${asset.name} to ${document.objects.firstOrNull { it.id == objectId }?.name ?: "object"}.")
    }

    fun updateSprite(id: String, update: (SpriteComponent) -> SpriteComponent) {
        updateObject(id) { target -> target.copy(sprite = update(target.sprite ?: SpriteComponent())) }
    }

    fun setGridSize(size: Float) {
        val safe = size.coerceIn(0.125f, 4f)
        if (abs(document.gridSize - safe) < 0.0001f) return
        mutateDocument("Change grid size") { it.copy(gridSize = safe) }
    }

    fun toggleSnapping() {
        mutateDocument("Toggle snapping") { it.copy(snappingEnabled = !it.snappingEnabled) }
    }

    fun snap(value: Float): Float = if (document.snappingEnabled) {
        (value / document.gridSize).let { kotlin.math.round(it) } * document.gridSize
    } else {
        value
    }

    fun worldTransform(id: String): Transform3D? {
        val objectValue = document.objects.firstOrNull { it.id == id } ?: return null
        return worldTransform(objectValue, mutableSetOf())
    }

    private fun worldTransform(objectValue: EditorObject, seen: MutableSet<String>): Transform3D {
        if (!seen.add(objectValue.id)) return objectValue.transform
        val parent = objectValue.parentId?.let { parentId -> document.objects.firstOrNull { it.id == parentId } }
            ?: return objectValue.transform
        val parentWorld = worldTransform(parent, seen)
        val radians = Math.toRadians(parentWorld.rotationZ.toDouble())
        val scaledX = objectValue.transform.x * parentWorld.scaleX
        val scaledY = objectValue.transform.y * parentWorld.scaleY
        return objectValue.transform.copy(
            x = parentWorld.x + (scaledX * cos(radians) - scaledY * sin(radians)).toFloat(),
            y = parentWorld.y + (scaledX * sin(radians) + scaledY * cos(radians)).toFloat(),
            z = parentWorld.z + objectValue.transform.z * parentWorld.scaleZ,
            rotationX = parentWorld.rotationX + objectValue.transform.rotationX,
            rotationY = parentWorld.rotationY + objectValue.transform.rotationY,
            rotationZ = parentWorld.rotationZ + objectValue.transform.rotationZ,
            scaleX = parentWorld.scaleX * objectValue.transform.scaleX,
            scaleY = parentWorld.scaleY * objectValue.transform.scaleY,
            scaleZ = parentWorld.scaleZ * objectValue.transform.scaleZ,
        )
    }

    fun beginGesture(): SceneDocument = document

    fun finishGesture(before: SceneDocument) {
        if (before == document) return
        pushUndo(before)
        isDirty = true
        statusMessage = "Unsaved changes"
    }

    fun paintTile(worldX: Float, worldY: Float) {
        val owner = activeTilemap()
        val (cellX, cellY) = tileCellCoordinates(worldX, worldY, owner)
        val cell = TileCell(cellX, cellY, selectedTileColor, selectedTileAssetId, owner?.id)
        if (document.tiles.firstOrNull { it.x == cellX && it.y == cellY && it.ownerId == owner?.id } == cell) return
        val tiles = document.tiles.filterNot { it.x == cellX && it.y == cellY && it.ownerId == owner?.id } + cell
        mutateDocument("Paint tile", recordUndo = false) { it.copy(tiles = tiles) }
    }

    fun eraseTile(worldX: Float, worldY: Float) {
        val owner = activeTilemap()
        val (cellX, cellY) = tileCellCoordinates(worldX, worldY, owner)
        if (document.tiles.none { it.x == cellX && it.y == cellY && it.ownerId == owner?.id }) return
        mutateDocument("Erase tile", recordUndo = false) {
            it.copy(tiles = it.tiles.filterNot { tile ->
                tile.x == cellX && tile.y == cellY && tile.ownerId == owner?.id
            })
        }
    }

    private fun activeTilemap(): EditorObject? =
        selectedObject?.takeIf { it.kind == EditorObjectKind.TILEMAP }
            ?: document.objects.firstOrNull { it.kind == EditorObjectKind.TILEMAP }

    private fun tileCellCoordinates(worldX: Float, worldY: Float, owner: EditorObject?): Pair<Int, Int> {
        val transform = owner?.let { worldTransform(it.id) ?: it.transform }
            ?: return floor(worldX).toInt() to floor(worldY).toInt()
        val dx = worldX - transform.x
        val dy = worldY - transform.y
        val radians = Math.toRadians(transform.rotationZ.toDouble())
        val localX = (dx * cos(radians) + dy * sin(radians)).toFloat()
        val localY = (-dx * sin(radians) + dy * cos(radians)).toFloat()
        val scaleX = if (abs(transform.scaleX) < 0.05f) 0.05f else transform.scaleX
        val scaleY = if (abs(transform.scaleY) < 0.05f) 0.05f else transform.scaleY
        return floor(localX / scaleX).toInt() to floor(localY / scaleY).toInt()
    }

    fun setTileColor(colorArgb: Long) {
        selectedTileColor = colorArgb
        selectedTileAssetId = null
    }

    fun setTileAsset(assetId: String?) {
        val asset = document.assets.firstOrNull { it.id == assetId && it.kind == AssetKind.SPRITE }
        selectedTileAssetId = asset?.id
    }

    fun addAnimationKeyframe(timeSeconds: Float = timelineTime) {
        val target = selectedObject ?: return
        val safeTime = timeSeconds.coerceIn(0f, 120f)
        val key = AnimationKeyframe(objectId = target.id, timeSeconds = safeTime, transform = target.transform)
        mutateDocument("Add animation key") { current ->
            val withoutSameTime = current.keyframes.filterNot {
                it.objectId == target.id && abs(it.timeSeconds - safeTime) < 0.001f
            }
            current.copy(keyframes = withoutSameTime + key)
        }
        timelineTime = safeTime
        log("Added ${target.name} keyframe at ${"%.2f".format(Locale.US, safeTime)} s.")
    }

    fun removeAnimationKeyframe(keyframeId: String) {
        mutateDocument("Remove animation key") { current ->
            current.copy(keyframes = current.keyframes.filterNot { it.id == keyframeId })
        }
    }

    fun previewTransform(objectValue: EditorObject): Transform3D {
        val previewActive = animationPreviewPlaying || bottomPanel == BottomPanel.ANIMATION || pane == EditorPane.ANIMATION
        if (!previewActive) return worldTransform(objectValue.id) ?: objectValue.transform
        return sampleAnimation(objectValue.id, timelineTime) ?: worldTransform(objectValue.id) ?: objectValue.transform
    }

    private fun sampleAnimation(objectId: String, time: Float): Transform3D? {
        val keys = document.keyframes.filter { it.objectId == objectId }.sortedBy { it.timeSeconds }
        if (keys.isEmpty()) return null
        val before = keys.lastOrNull { it.timeSeconds <= time } ?: keys.first()
        val after = keys.firstOrNull { it.timeSeconds >= time } ?: keys.last()
        if (before.id == after.id || after.timeSeconds <= before.timeSeconds) return before.transform
        val t = ((time - before.timeSeconds) / (after.timeSeconds - before.timeSeconds)).coerceIn(0f, 1f)
        return interpolate(before.transform, after.transform, t)
    }

    private fun interpolate(a: Transform3D, b: Transform3D, t: Float) = Transform3D(
        x = lerp(a.x, b.x, t), y = lerp(a.y, b.y, t), z = lerp(a.z, b.z, t),
        rotationX = lerpAngle(a.rotationX, b.rotationX, t),
        rotationY = lerpAngle(a.rotationY, b.rotationY, t),
        rotationZ = lerpAngle(a.rotationZ, b.rotationZ, t),
        scaleX = lerp(a.scaleX, b.scaleX, t), scaleY = lerp(a.scaleY, b.scaleY, t),
        scaleZ = lerp(a.scaleZ, b.scaleZ, t),
    )

    private fun lerp(start: Float, end: Float, t: Float) = start + (end - start) * t

    private fun lerpAngle(start: Float, end: Float, t: Float): Float {
        val delta = ((end - start + 540f) % 360f) - 180f
        return start + delta * t
    }

    fun setAnimationPreviewPlaying(playing: Boolean) {
        animationPreviewPlaying = playing
        if (playing && timelineTime >= animationDuration) timelineTime = 0f
    }

    fun advanceAnimation(deltaSeconds: Float) {
        val duration = animationDuration
        if (!animationPreviewPlaying || duration <= 0f) return
        timelineTime = (timelineTime + deltaSeconds).let { if (it > duration) 0f else it }
    }

    fun startPlay() {
        if (document.dimension != SceneDimension.TWO_D) {
            statusMessage = "3D play mode is not implemented yet."
            log(statusMessage)
            return
        }
        if (playState == PlayState.PAUSED) {
            playState = PlayState.PLAYING
            log("Runtime resumed.")
            return
        }
        runtimeStartPositions.clear()
        runtimeBodies = emptyMap()
        collectedObjectIds = emptySet()
        for (objectValue in document.objects) {
            val world = worldTransform(objectValue.id) ?: objectValue.transform
            runtimeStartPositions[objectValue.id] = Vec2(world.x, world.y)
            if (objectValue.body?.bodyType == BodyType2D.DYNAMIC) {
                runtimeBodies = runtimeBodies + (objectValue.id to RuntimeBody(Vec2(world.x, world.y)))
            }
        }
        gameTime = 0f
        jumpQueued = false
        gameInput = 0
        playState = PlayState.PLAYING
        pane = EditorPane.GAME
        log("Runtime started: ${document.name}.")
    }

    fun pausePlay() {
        if (playState != PlayState.PLAYING) return
        playState = PlayState.PAUSED
        gameInput = 0
        log("Runtime paused.")
    }

    fun stopPlay() {
        if (playState == PlayState.STOPPED) return
        playState = PlayState.STOPPED
        runtimeBodies = emptyMap()
        collectedObjectIds = emptySet()
        gameInput = 0
        jumpQueued = false
        gameTime = 0f
        log("Runtime stopped; edit scene restored.")
    }

    fun setHorizontalInput(direction: Int) {
        gameInput = if (direction < 0) -1 else if (direction > 0) 1 else 0
    }

    fun requestJump() {
        if (playState == PlayState.PLAYING) jumpQueued = true
    }

    fun runtimePosition(objectValue: EditorObject): Vec2 {
        return runtimeBodies[objectValue.id]?.position
            ?: worldTransform(objectValue.id)?.let { Vec2(it.x, it.y) }
            ?: Vec2(objectValue.transform.x, objectValue.transform.y)
    }

    fun stepGame(deltaSeconds: Float) {
        if (playState != PlayState.PLAYING) return
        val dt = deltaSeconds.coerceIn(0f, 0.05f)
        if (dt <= 0f) return
        gameTime += dt
        val player = document.objects.firstOrNull {
            it.name.equals("Player", ignoreCase = true) && it.body?.bodyType == BodyType2D.DYNAMIC
        } ?: document.objects.firstOrNull { it.body?.bodyType == BodyType2D.DYNAMIC }
            ?: return
        val playerBody = runtimeBodies[player.id] ?: return
        val playerCollider = player.collider ?: Collider2D()
        val objectStatics = document.objects.asSequence()
            .filter {
                it.id != player.id && it.collider != null && !it.collider.isTrigger &&
                    it.body?.bodyType != BodyType2D.DYNAMIC
            }
            .map { candidate ->
                val world = worldTransform(candidate.id) ?: candidate.transform
                val collider = candidate.collider!!
                Vec2(world.x + collider.offsetX, world.y + collider.offsetY) to collider
            }
            .toList()
        val tileStatics = document.tiles.mapNotNull { tile ->
            val owner = tile.ownerId?.let { ownerId -> document.objects.firstOrNull { it.id == ownerId } }
            if (owner?.body?.bodyType == BodyType2D.DYNAMIC) return@mapNotNull null
            val transform = owner?.let { worldTransform(it.id) ?: it.transform } ?: Transform3D()
            val localX = (tile.x + 0.5f) * transform.scaleX
            val localY = (tile.y + 0.5f) * transform.scaleY
            val radians = Math.toRadians(transform.rotationZ.toDouble())
            val center = Vec2(
                transform.x + (localX * cos(radians) - localY * sin(radians)).toFloat(),
                transform.y + (localX * sin(radians) + localY * cos(radians)).toFloat(),
            )
            center to Collider2D(abs(transform.scaleX).coerceAtLeast(0.05f), abs(transform.scaleY).coerceAtLeast(0.05f))
        }
        val statics = objectStatics + tileStatics
        val start = playerBody.position
        val body = player.body ?: PhysicsBody2D()
        val playerOffsetX = playerCollider.offsetX
        val playerOffsetY = playerCollider.offsetY
        val horizontalSpeed = 4.6f
        var velocityX = gameInput * horizontalSpeed
        if (gameInput == 0) {
            velocityX = playerBody.velocity.x * exp((-max(body.linearDrag, 3.5f) * dt).toDouble()).toFloat()
        }
        var positionX = start.x + velocityX * dt
        val halfWidth = playerCollider.width * 0.5f
        val halfHeight = playerCollider.height * 0.5f
        for ((staticPosition, collider) in statics) {
            val otherHalfWidth = collider.width * 0.5f
            val otherHalfHeight = collider.height * 0.5f
            val playerCenterX = positionX + playerOffsetX
            val playerCenterY = start.y + playerOffsetY
            val verticalOverlap = playerCenterY - halfHeight < staticPosition.y + otherHalfHeight &&
                playerCenterY + halfHeight > staticPosition.y - otherHalfHeight
            val horizontalOverlap = playerCenterX - halfWidth < staticPosition.x + otherHalfWidth &&
                playerCenterX + halfWidth > staticPosition.x - otherHalfWidth
            if (verticalOverlap && horizontalOverlap) {
                positionX = start.x
                velocityX = 0f
                break
            }
        }

        var velocityY = playerBody.velocity.y - 18f * body.gravityScale * dt
        if (jumpQueued && playerBody.grounded) {
            velocityY = 7.2f
            jumpQueued = false
        } else if (jumpQueued && gameTime > 0.2f) {
            jumpQueued = false
        }
        var positionY = start.y + velocityY * dt
        var grounded = false
        for ((staticPosition, collider) in statics) {
            val otherHalfWidth = collider.width * 0.5f
            val otherHalfHeight = collider.height * 0.5f
            val playerCenterX = positionX + playerOffsetX
            val horizontalOverlap = playerCenterX - halfWidth < staticPosition.x + otherHalfWidth &&
                playerCenterX + halfWidth > staticPosition.x - otherHalfWidth
            if (!horizontalOverlap) continue
            val top = staticPosition.y + otherHalfHeight
            val bottom = staticPosition.y - otherHalfHeight
            val oldBottom = start.y + playerOffsetY - halfHeight
            val newBottom = positionY + playerOffsetY - halfHeight
            val oldTop = start.y + playerOffsetY + halfHeight
            val newTop = positionY + playerOffsetY + halfHeight
            if (velocityY <= 0f && oldBottom >= top - 0.08f && newBottom <= top) {
                positionY = top + halfHeight - playerOffsetY
                velocityY = 0f
                grounded = true
            } else if (velocityY > 0f && oldTop <= bottom + 0.08f && newTop >= bottom) {
                positionY = bottom - halfHeight - playerOffsetY
                velocityY = 0f
            }
        }

        var updatedRuntime = runtimeBodies + (
            player.id to RuntimeBody(Vec2(positionX, positionY), Vec2(velocityX, velocityY), grounded)
        )
        val newCoins = document.objects.filter { it.name.contains("coin", ignoreCase = true) }
            .filterNot { it.id in collectedObjectIds }
            .filter { coin ->
                val coinTransform = worldTransform(coin.id) ?: coin.transform
                val coinCollider = coin.collider ?: Collider2D(0.4f, 0.4f)
                aabbOverlap(
                    Vec2(positionX + playerOffsetX, positionY + playerOffsetY), playerCollider.width, playerCollider.height,
                    Vec2(coinTransform.x + coinCollider.offsetX, coinTransform.y + coinCollider.offsetY), coinCollider.width, coinCollider.height,
                )
            }
            .map { it.id }
            .toSet()
        if (newCoins.isNotEmpty()) {
            collectedObjectIds = collectedObjectIds + newCoins
            log("Collected ${newCoins.size} coin(s).")
        }
        val enemy = document.objects.firstOrNull { it.name.contains("enemy", ignoreCase = true) }
        if (enemy != null) {
            val enemyPosition = worldTransform(enemy.id)?.let { Vec2(it.x, it.y) } ?: Vec2()
            val enemyCollider = enemy.collider ?: Collider2D(0.8f, 1f)
            if (aabbOverlap(
                    Vec2(positionX + playerOffsetX, positionY + playerOffsetY), playerCollider.width, playerCollider.height,
                    Vec2(enemyPosition.x + enemyCollider.offsetX, enemyPosition.y + enemyCollider.offsetY), enemyCollider.width, enemyCollider.height,
                )) {
                val startPosition = runtimeStartPositions[player.id] ?: Vec2(-4f, -1.3f)
                updatedRuntime = updatedRuntime + (player.id to RuntimeBody(startPosition))
                log("Player hit an enemy; reset to checkpoint.")
            }
        }
        runtimeBodies = updatedRuntime
    }

    private fun aabbOverlap(a: Vec2, aw: Float, ah: Float, b: Vec2, bw: Float, bh: Float): Boolean =
        abs(a.x - b.x) < (aw + bw) * 0.5f && abs(a.y - b.y) < (ah + bh) * 0.5f

    suspend fun importAsset(uri: Uri): SpriteAsset = withContext(Dispatchers.IO) {
        val resolver = appContext.contentResolver
        val mime = resolver.getType(uri) ?: "application/octet-stream"
        val displayName = queryDisplayName(uri) ?: "import-${System.currentTimeMillis()}"
        val kind = when {
            mime.startsWith("image/") -> AssetKind.SPRITE
            mime.startsWith("audio/") -> AssetKind.AUDIO
            mime.startsWith("text/") || displayName.endsWith(".lua", ignoreCase = true) -> AssetKind.SCRIPT
            mime == "application/json" || displayName.endsWith(".json", ignoreCase = true) -> AssetKind.SCENE
            mime.contains("gltf") || displayName.endsWith(".glb", ignoreCase = true) -> AssetKind.MODEL
            else -> AssetKind.OTHER
        }
        val folder = when (kind) {
            AssetKind.SPRITE -> "Sprites"
            AssetKind.AUDIO -> "Audio"
            AssetKind.SCRIPT -> "Scripts"
            AssetKind.SCENE -> "Scenes"
            AssetKind.MODEL -> "Models"
            AssetKind.OTHER -> "Other"
        }
        val targetDirectory = File(projectRoot, "Assets/$folder")
        if (!targetDirectory.exists() && !targetDirectory.mkdirs()) throw IOException("Could not create the project asset folder.")
        val safeName = displayName.substringAfterLast('/').replace(Regex("[^A-Za-z0-9._-]"), "_").take(96)
        val destination = File(targetDirectory, "${System.currentTimeMillis()}-$safeName")
        val input = resolver.openInputStream(uri) ?: throw IOException("The selected file could not be opened.")
        var byteCount = 0L
        input.use { source ->
            destination.outputStream().use { output ->
                val buffer = ByteArray(16 * 1024)
                while (true) {
                    val read = source.read(buffer)
                    if (read < 0) break
                    byteCount += read
                    if (byteCount > 128L * 1024L * 1024L) {
                        destination.delete()
                        throw IOException("Imported files are limited to 128 MiB.")
                    }
                    output.write(buffer, 0, read)
                }
            }
        }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        if (kind == AssetKind.SPRITE) BitmapFactory.decodeFile(destination.absolutePath, bounds)
        val imported = SpriteAsset(
            name = safeName,
            path = destination.absolutePath,
            mimeType = mime,
            kind = kind,
            byteCount = byteCount,
            width = bounds.outWidth.coerceAtLeast(0),
            height = bounds.outHeight.coerceAtLeast(0),
        )
        withContext(Dispatchers.Main.immediate) {
            mutateDocument("Import ${imported.name}") { current -> current.copy(assets = current.assets + imported) }
            selectedAssetId = imported.id
            log("Imported ${imported.name} (${formatBytes(byteCount)}).")
        }
        imported
    }

    suspend fun loadSceneFromUri(uri: Uri) = withContext(Dispatchers.IO) {
        val text = appContext.contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
            ?: throw IOException("The selected scene could not be opened.")
        val loaded = JSONObject(text).toSceneDocument()
        withContext(Dispatchers.Main.immediate) {
            replaceDocument(loaded)
            selectedObjectId = loaded.objects.firstOrNull()?.id
            isDirty = false
            statusMessage = "Scene loaded"
            log("Loaded scene ${loaded.name}.")
        }
    }

    fun assignSelectedSprite(assetId: String) {
        val target = selectedObject ?: return
        assignSpriteAsset(target.id, assetId)
    }

    fun sceneJsonText(): String = document.toJson().toString(2)

    suspend fun exportScene(uri: Uri) {
        val snapshot = document
        val json = snapshot.toJson().toString(2)
        withContext(Dispatchers.IO) {
            val output = appContext.contentResolver.openOutputStream(uri)
                ?: throw IOException("The destination file could not be opened.")
            output.bufferedWriter(Charsets.UTF_8).use { it.write(json) }
        }
        withContext(Dispatchers.Main.immediate) {
            log("Exported ${snapshot.name} scene.")
            statusMessage = "Scene exported"
        }
    }

    fun saveProject(): Boolean {
        return try {
            val parent = sceneFile.parentFile
            if (parent != null && !parent.exists() && !parent.mkdirs()) throw IOException("Could not create the scene folder.")
            val temporary = File(sceneFile.parentFile, "${sceneFile.name}.tmp")
            temporary.writeText(document.toJson().toString(2), Charsets.UTF_8)
            if (sceneFile.exists() && !sceneFile.delete()) throw IOException("Could not replace the previous scene file.")
            if (!temporary.renameTo(sceneFile)) {
                temporary.copyTo(sceneFile, overwrite = true)
                temporary.delete()
            }
            isDirty = false
            statusMessage = "Saved ${document.name}"
            log("Saved ${sceneFile.relativeToOrSelf(appContext.filesDir).path}.")
            true
        } catch (error: Exception) {
            statusMessage = "Save failed: ${error.message ?: "unknown error"}"
            log(statusMessage)
            false
        }
    }

    fun loadSavedScene(): SceneDocument? {
        return try {
            if (!sceneFile.isFile) null else JSONObject(sceneFile.readText(Charsets.UTF_8)).toSceneDocument()
        } catch (error: Exception) {
            null
        }
    }

    fun undo() {
        if (undoHistory.isEmpty()) return
        redoHistory.addLast(document)
        document = undoHistory.removeLast()
        selectedObjectId = selectedObjectId?.takeIf { id -> document.objects.any { it.id == id } }
        isDirty = true
        statusMessage = "Undo"
        log("Undo.")
    }

    fun redo() {
        if (redoHistory.isEmpty()) return
        undoHistory.addLast(document)
        document = redoHistory.removeLast()
        selectedObjectId = selectedObjectId?.takeIf { id -> document.objects.any { it.id == id } }
        isDirty = true
        statusMessage = "Redo"
        log("Redo.")
    }

    fun executeConsole(commandText: String): String {
        val command = commandText.trim()
        if (command.isEmpty()) return ""
        val parts = command.split(Regex("\\s+"), limit = 2)
        val verb = parts[0].lowercase(Locale.US)
        val argument = parts.getOrNull(1).orEmpty().trim()
        val result = when (verb) {
            "help" -> "Commands: help, clear, spawn, delete, save, play, pause, stop, undo, redo, select <name>, add-key"
            "clear" -> {
                consoleLines.clear()
                "Console cleared."
            }
            "spawn" -> {
                addObject(EditorObjectKind.SPRITE)
                "Spawned a Sprite object."
            }
            "delete" -> {
                val id = selectedObjectId
                if (id == null) "No object selected." else {
                    deleteObject(id)
                    "Deleted selected object."
                }
            }
            "save" -> if (saveProject()) "Project saved." else statusMessage
            "play" -> { startPlay(); "Runtime started." }
            "pause" -> { pausePlay(); "Runtime paused." }
            "stop" -> { stopPlay(); "Runtime stopped." }
            "undo" -> { undo(); "Undo applied." }
            "redo" -> { redo(); "Redo applied." }
            "select" -> {
                val match = document.objects.firstOrNull { it.name.equals(argument, ignoreCase = true) }
                selectObject(match?.id)
                if (match == null) "No object named '$argument'." else "Selected ${match.name}."
            }
            "add-key" -> { addAnimationKeyframe(); "Animation key added." }
            else -> "Unknown command '$verb'. Type help."
        }
        log("> $command")
        if (verb != "clear") log(result)
        return result
    }

    fun log(message: String) {
        consoleLines.add(message)
        while (consoleLines.size > 300) consoleLines.removeAt(0)
    }

    private fun mutateDocument(
        label: String,
        recordUndo: Boolean = true,
        change: (SceneDocument) -> SceneDocument,
    ) {
        val updated = change(document)
        if (updated == document) return
        if (recordUndo) pushUndo(document)
        document = updated
        isDirty = true
        statusMessage = "$label · unsaved"
    }

    private fun replaceDocument(updated: SceneDocument) {
        undoHistory.clear()
        redoHistory.clear()
        document = updated
        selectedObjectId = null
        isDirty = false
    }

    private fun pushUndo(previous: SceneDocument) {
        undoHistory.addLast(previous)
        while (undoHistory.size > 80) undoHistory.removeFirst()
        redoHistory.clear()
    }

    private fun uniqueName(base: String): String {
        val names = document.objects.map { it.name }.toSet()
        if (base !in names) return base
        var index = 1
        while ("$base $index" in names) index++
        return "$base $index"
    }

    private fun descendantsOf(id: String): Set<String> {
        val result = mutableSetOf<String>()
        var changed: Boolean
        do {
            changed = false
            document.objects.forEach { item ->
                if (item.parentId == id || (item.parentId != null && item.parentId in result)) {
                    if (result.add(item.id)) changed = true
                }
            }
        } while (changed)
        return result
    }

    private fun queryDisplayName(uri: Uri): String? {
        appContext.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (index >= 0) return cursor.getString(index)
                }
            }
        return uri.lastPathSegment?.substringAfterLast('/')
    }

    private fun formatBytes(byteCount: Long): String = when {
        byteCount >= 1024L * 1024L -> "%.1f MiB".format(Locale.US, byteCount / (1024f * 1024f))
        byteCount >= 1024L -> "%.1f KiB".format(Locale.US, byteCount / 1024f)
        else -> "$byteCount B"
    }
}

private fun SceneDocument.toJson(): JSONObject = JSONObject().apply {
    put("version", 1)
    put("id", id)
    put("name", name)
    put("dimension", dimension.name)
    put("gridSize", gridSize.toDouble())
    put("snappingEnabled", snappingEnabled)
    put("objects", JSONArray().also { array -> objects.forEach { array.put(it.toJson()) } })
    put("tiles", JSONArray().also { array -> tiles.forEach { array.put(it.toJson()) } })
    put("assets", JSONArray().also { array -> assets.forEach { array.put(it.toJson()) } })
    put("keyframes", JSONArray().also { array -> keyframes.forEach { array.put(it.toJson()) } })
}

private fun EditorObject.toJson(): JSONObject = JSONObject().apply {
    put("id", id); put("name", name); put("kind", kind.name); put("parentId", parentId)
    put("visible", visible); put("transform", transform.toJson()); put("primitive3D", primitive3D.name)
    put("sprite", sprite?.toJson()); put("body", body?.toJson()); put("collider", collider?.toJson()); put("camera", camera?.toJson())
}

private fun Transform3D.toJson(): JSONObject = JSONObject().apply {
    put("x", x.toDouble()); put("y", y.toDouble()); put("z", z.toDouble())
    put("rotationX", rotationX.toDouble()); put("rotationY", rotationY.toDouble()); put("rotationZ", rotationZ.toDouble())
    put("scaleX", scaleX.toDouble()); put("scaleY", scaleY.toDouble()); put("scaleZ", scaleZ.toDouble())
}

private fun SpriteComponent.toJson(): JSONObject = JSONObject().apply {
    put("assetId", assetId); put("width", width.toDouble()); put("height", height.toDouble())
    put("tintArgb", tintArgb); put("pivotX", pivotX.toDouble()); put("pivotY", pivotY.toDouble())
    put("sourceX", sourceX); put("sourceY", sourceY); put("sourceWidth", sourceWidth); put("sourceHeight", sourceHeight)
    put("sortingOrder", sortingOrder)
}

private fun PhysicsBody2D.toJson(): JSONObject = JSONObject().apply {
    put("bodyType", bodyType.name); put("mass", mass.toDouble()); put("gravityScale", gravityScale.toDouble())
    put("linearDrag", linearDrag.toDouble()); put("fixedRotation", fixedRotation)
}

private fun Collider2D.toJson(): JSONObject = JSONObject().apply {
    put("width", width.toDouble()); put("height", height.toDouble())
    put("offsetX", offsetX.toDouble()); put("offsetY", offsetY.toDouble()); put("isTrigger", isTrigger)
}

private fun CameraComponent.toJson(): JSONObject = JSONObject().apply {
    put("orthographicSize", orthographicSize.toDouble()); put("nearClip", nearClip.toDouble())
    put("farClip", farClip.toDouble()); put("primary", primary)
}

private fun TileCell.toJson(): JSONObject = JSONObject().apply {
    put("x", x); put("y", y); put("colorArgb", colorArgb); put("assetId", assetId); put("ownerId", ownerId)
}

private fun SpriteAsset.toJson(): JSONObject = JSONObject().apply {
    put("id", id); put("name", name); put("path", path); put("mimeType", mimeType)
    put("kind", kind.name); put("byteCount", byteCount); put("width", width); put("height", height)
}

private fun AnimationKeyframe.toJson(): JSONObject = JSONObject().apply {
    put("id", id); put("objectId", objectId); put("timeSeconds", timeSeconds.toDouble()); put("transform", transform.toJson())
}

private fun JSONObject.toSceneDocument(): SceneDocument {
    require(optInt("version", 1) <= 1) { "This scene was saved by a newer Box Engine version." }
    return SceneDocument(
        id = optString("id", UUID.randomUUID().toString()),
        name = optString("name", "Imported Scene"),
        dimension = enumValueOrDefault(optString("dimension"), SceneDimension.TWO_D),
        objects = optJSONArray("objects").toObjectList { it.toEditorObject() },
        tiles = optJSONArray("tiles").toObjectList { it.toTileCell() },
        assets = optJSONArray("assets").toObjectList { it.toSpriteAsset() },
        keyframes = optJSONArray("keyframes").toObjectList { it.toAnimationKeyframe() },
        gridSize = optFloat("gridSize", 1f).coerceIn(0.125f, 4f),
        snappingEnabled = optBoolean("snappingEnabled", true),
    )
}

private fun JSONObject.toEditorObject(): EditorObject = EditorObject(
    id = optString("id", UUID.randomUUID().toString()),
    name = optString("name", "GameObject"),
    kind = enumValueOrDefault(optString("kind"), EditorObjectKind.EMPTY),
    transform = optJSONObject("transform")?.toTransform() ?: Transform3D(),
    parentId = optNullableString("parentId"),
    visible = optBoolean("visible", true),
    sprite = optJSONObject("sprite")?.toSpriteComponent(),
    body = optJSONObject("body")?.toBody(),
    collider = optJSONObject("collider")?.toCollider(),
    camera = optJSONObject("camera")?.toCamera(),
    primitive3D = enumValueOrDefault(optString("primitive3D"), Primitive3D.CUBE),
)

private fun JSONObject.toTransform() = Transform3D(
    x = optFloat("x"), y = optFloat("y"), z = optFloat("z"),
    rotationX = optFloat("rotationX"), rotationY = optFloat("rotationY"), rotationZ = optFloat("rotationZ"),
    scaleX = optFloat("scaleX", 1f), scaleY = optFloat("scaleY", 1f), scaleZ = optFloat("scaleZ", 1f),
)

private fun JSONObject.toSpriteComponent() = SpriteComponent(
    assetId = optNullableString("assetId"), width = optFloat("width", 1f), height = optFloat("height", 1f),
    tintArgb = optLong("tintArgb", 0xFF39C6B0L), pivotX = optFloat("pivotX", 0.5f), pivotY = optFloat("pivotY", 0.5f),
    sourceX = optInt("sourceX"), sourceY = optInt("sourceY"), sourceWidth = optInt("sourceWidth"), sourceHeight = optInt("sourceHeight"),
    sortingOrder = optInt("sortingOrder"),
)

private fun JSONObject.toBody() = PhysicsBody2D(
    bodyType = enumValueOrDefault(optString("bodyType"), BodyType2D.DYNAMIC),
    mass = optFloat("mass", 1f).coerceAtLeast(0.01f), gravityScale = optFloat("gravityScale", 1f),
    linearDrag = optFloat("linearDrag"), fixedRotation = optBoolean("fixedRotation"),
)

private fun JSONObject.toCollider() = Collider2D(
    width = optFloat("width", 1f).coerceAtLeast(0.05f), height = optFloat("height", 1f).coerceAtLeast(0.05f),
    offsetX = optFloat("offsetX"), offsetY = optFloat("offsetY"), isTrigger = optBoolean("isTrigger"),
)

private fun JSONObject.toCamera() = CameraComponent(
    orthographicSize = optFloat("orthographicSize", 5f).coerceAtLeast(0.1f),
    nearClip = optFloat("nearClip", -10f), farClip = optFloat("farClip", 100f), primary = optBoolean("primary"),
)

private fun JSONObject.toTileCell() = TileCell(
    x = optInt("x"),
    y = optInt("y"),
    colorArgb = optLong("colorArgb", 0xFF568A53L),
    assetId = optNullableString("assetId"),
    ownerId = optNullableString("ownerId"),
)

private fun JSONObject.toSpriteAsset() = SpriteAsset(
    id = optString("id", UUID.randomUUID().toString()), name = optString("name", "Asset"),
    path = optString("path"), mimeType = optString("mimeType", "application/octet-stream"),
    kind = enumValueOrDefault(optString("kind"), AssetKind.OTHER), byteCount = optLong("byteCount"),
    width = optInt("width"), height = optInt("height"),
)

private fun JSONObject.toAnimationKeyframe() = AnimationKeyframe(
    id = optString("id", UUID.randomUUID().toString()), objectId = optString("objectId"),
    timeSeconds = optFloat("timeSeconds"), transform = optJSONObject("transform")?.toTransform() ?: Transform3D(),
)

private fun JSONObject.optFloat(key: String, fallback: Float = 0f): Float {
    val value = optDouble(key, fallback.toDouble())
    return if (value.isFinite()) value.toFloat() else fallback
}

private fun JSONObject.optNullableString(key: String): String? =
    if (!has(key) || isNull(key)) null else optString(key).takeIf { it.isNotBlank() }

private inline fun <reified T : Enum<T>> enumValueOrDefault(name: String, default: T): T =
    enumValues<T>().firstOrNull { it.name == name } ?: default

private inline fun <T> JSONArray?.toObjectList(convert: (JSONObject) -> T): List<T> {
    if (this == null) return emptyList()
    return buildList {
        for (index in 0 until length()) {
            val item = optJSONObject(index) ?: continue
            add(convert(item))
        }
    }
}
