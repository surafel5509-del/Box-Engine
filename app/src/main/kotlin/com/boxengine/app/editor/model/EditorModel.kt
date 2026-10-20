// Path: app/src/main/kotlin/com/boxengine/app/editor/model/EditorModel.kt
package com.boxengine.app.editor.model

import java.util.UUID

/** Serializable scene data shared by the editor, save format, and runtime preview. */
enum class SceneDimension { TWO_D, THREE_D }
enum class EditorTool { SELECT, MOVE, ROTATE, SCALE, PAN, PAINT, ERASE }
enum class EditorObjectKind { EMPTY, SPRITE, TILEMAP, CAMERA, MESH3D, AUDIO_SOURCE }
enum class BodyType2D { STATIC, DYNAMIC, KINEMATIC }
enum class AssetKind { SPRITE, AUDIO, SCRIPT, SCENE, MODEL, OTHER }
enum class Primitive3D { CUBE, PLANE }
enum class PlayState { STOPPED, PLAYING, PAUSED }
enum class EditorPane { SCENE, GAME, HIERARCHY, INSPECTOR, ASSETS, TILE_PALETTE, ANIMATION, CONSOLE, SPRITE_EDITOR, NATIVE_ENGINE }
enum class BottomPanel { ASSETS, TILE_PALETTE, SPRITE_EDITOR, ANIMATION, CONSOLE }

data class Vec2(val x: Float = 0f, val y: Float = 0f) {
    operator fun plus(other: Vec2) = Vec2(x + other.x, y + other.y)
    operator fun minus(other: Vec2) = Vec2(x - other.x, y - other.y)
    operator fun times(factor: Float) = Vec2(x * factor, y * factor)
}

data class Transform3D(
    val x: Float = 0f,
    val y: Float = 0f,
    val z: Float = 0f,
    val rotationX: Float = 0f,
    val rotationY: Float = 0f,
    val rotationZ: Float = 0f,
    val scaleX: Float = 1f,
    val scaleY: Float = 1f,
    val scaleZ: Float = 1f,
)

data class SpriteComponent(
    val assetId: String? = null,
    val width: Float = 1f,
    val height: Float = 1f,
    val tintArgb: Long = 0xFF39C6B0L,
    val pivotX: Float = 0.5f,
    val pivotY: Float = 0.5f,
    val sourceX: Int = 0,
    val sourceY: Int = 0,
    val sourceWidth: Int = 0,
    val sourceHeight: Int = 0,
    val sortingOrder: Int = 0,
)

data class PhysicsBody2D(
    val bodyType: BodyType2D = BodyType2D.DYNAMIC,
    val mass: Float = 1f,
    val gravityScale: Float = 1f,
    val linearDrag: Float = 0f,
    val fixedRotation: Boolean = false,
)

data class Collider2D(
    val width: Float = 1f,
    val height: Float = 1f,
    val offsetX: Float = 0f,
    val offsetY: Float = 0f,
    val isTrigger: Boolean = false,
)

data class CameraComponent(
    val orthographicSize: Float = 5f,
    val nearClip: Float = -10f,
    val farClip: Float = 100f,
    val primary: Boolean = false,
)

data class EditorObject(
    val id: String = UUID.randomUUID().toString(),
    val name: String,
    val kind: EditorObjectKind,
    val transform: Transform3D = Transform3D(),
    val parentId: String? = null,
    val visible: Boolean = true,
    val sprite: SpriteComponent? = null,
    val body: PhysicsBody2D? = null,
    val collider: Collider2D? = null,
    val camera: CameraComponent? = null,
    val primitive3D: Primitive3D = Primitive3D.CUBE,
)

data class TileCell(
    val x: Int,
    val y: Int,
    val colorArgb: Long,
    val assetId: String? = null,
    val ownerId: String? = null,
)

data class SpriteAsset(
    val id: String = UUID.randomUUID().toString(),
    val name: String,
    val path: String,
    val mimeType: String,
    val kind: AssetKind,
    val byteCount: Long,
    val width: Int = 0,
    val height: Int = 0,
)

data class AnimationKeyframe(
    val id: String = UUID.randomUUID().toString(),
    val objectId: String,
    val timeSeconds: Float,
    val transform: Transform3D,
)

data class SceneDocument(
    val id: String = UUID.randomUUID().toString(),
    val name: String,
    val dimension: SceneDimension = SceneDimension.TWO_D,
    val objects: List<EditorObject>,
    val tiles: List<TileCell> = emptyList(),
    val assets: List<SpriteAsset> = emptyList(),
    val keyframes: List<AnimationKeyframe> = emptyList(),
    val gridSize: Float = 1f,
    val snappingEnabled: Boolean = true,
)

data class RuntimeBody(
    val position: Vec2,
    val velocity: Vec2 = Vec2(),
    val grounded: Boolean = false,
    val visible: Boolean = true,
)

fun createStarterScene(): SceneDocument {
    val camera = EditorObject(
        name = "Main Camera",
        kind = EditorObjectKind.CAMERA,
        transform = Transform3D(x = 0f, y = 0.5f, z = -10f),
        camera = CameraComponent(orthographicSize = 5f, primary = true),
    )
    val ground = EditorObject(
        name = "Ground",
        kind = EditorObjectKind.TILEMAP,
        transform = Transform3D(y = -3f),
        body = PhysicsBody2D(BodyType2D.STATIC),
    )
    val player = EditorObject(
        name = "Player",
        kind = EditorObjectKind.SPRITE,
        transform = Transform3D(x = -4f, y = -1.3f),
        sprite = SpriteComponent(width = 0.72f, height = 1.35f, tintArgb = 0xFF36CBB4L),
        body = PhysicsBody2D(BodyType2D.DYNAMIC, mass = 1f, gravityScale = 1f),
        collider = Collider2D(width = 0.72f, height = 1.3f),
    )
    val platformA = EditorObject(
        name = "Platform A",
        kind = EditorObjectKind.SPRITE,
        transform = Transform3D(x = -0.8f, y = -0.3f),
        sprite = SpriteComponent(width = 3.2f, height = 0.45f, tintArgb = 0xFF596C80L),
        body = PhysicsBody2D(BodyType2D.STATIC),
        collider = Collider2D(width = 3.2f, height = 0.45f),
    )
    val platformB = EditorObject(
        name = "Platform B",
        kind = EditorObjectKind.SPRITE,
        transform = Transform3D(x = 2.5f, y = 1.1f),
        sprite = SpriteComponent(width = 2.5f, height = 0.45f, tintArgb = 0xFF596C80L),
        body = PhysicsBody2D(BodyType2D.STATIC),
        collider = Collider2D(width = 2.5f, height = 0.45f),
    )
    val coin = EditorObject(
        name = "Coin",
        kind = EditorObjectKind.SPRITE,
        transform = Transform3D(x = 1.4f, y = 0.15f),
        sprite = SpriteComponent(width = 0.48f, height = 0.48f, tintArgb = 0xFFFFC85AL),
        collider = Collider2D(width = 0.42f, height = 0.42f, isTrigger = true),
    )
    val enemy = EditorObject(
        name = "Enemy",
        kind = EditorObjectKind.SPRITE,
        transform = Transform3D(x = 5.1f, y = -1.35f),
        sprite = SpriteComponent(width = 0.86f, height = 1.12f, tintArgb = 0xFFE36C73L),
        body = PhysicsBody2D(BodyType2D.STATIC),
        collider = Collider2D(width = 0.86f, height = 1.12f),
    )

    val tiles = buildList {
        for (x in -9..8) {
            add(TileCell(x, -1, 0xFF6A4E3DL, ownerId = ground.id))
            add(TileCell(x, 0, if (x % 3 == 0) 0xFF6F9B59L else 0xFF568A53L, ownerId = ground.id))
        }
    }
    return SceneDocument(
        name = "Sample Platformer",
        objects = listOf(camera, ground, player, platformA, platformB, coin, enemy),
        tiles = tiles,
    )
}

fun createStarter3DObjects(): List<EditorObject> = listOf(
    EditorObject(
        name = "Cube",
        kind = EditorObjectKind.MESH3D,
        transform = Transform3D(y = 0.6f),
        primitive3D = Primitive3D.CUBE,
    ),
    EditorObject(
        name = "Platform",
        kind = EditorObjectKind.MESH3D,
        transform = Transform3D(y = -0.12f, scaleX = 4f, scaleY = 0.2f, scaleZ = 3f),
        primitive3D = Primitive3D.PLANE,
    ),
    EditorObject(
        name = "Main Camera 3D",
        kind = EditorObjectKind.CAMERA,
        transform = Transform3D(x = 0f, y = 2f, z = -8f, rotationX = 12f),
        camera = CameraComponent(orthographicSize = 6f, primary = true),
    ),
)
