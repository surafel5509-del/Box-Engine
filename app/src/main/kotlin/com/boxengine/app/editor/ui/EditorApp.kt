// Path: app/src/main/kotlin/com/boxengine/app/editor/ui/EditorApp.kt
package com.boxengine.app.editor.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.runtime.withFrameNanos
import com.boxengine.app.editor.model.BottomPanel
import com.boxengine.app.editor.model.EditorController
import com.boxengine.app.editor.model.EditorObjectKind
import com.boxengine.app.editor.model.EditorPane
import com.boxengine.app.editor.model.EditorTool
import com.boxengine.app.editor.model.PlayState
import com.boxengine.app.editor.model.SceneDimension
import kotlinx.coroutines.launch

/** Builds the adaptive Compose shell and connects Android file pickers to the project controller. */
@Composable
fun BoxEngineEditorApp(
    controller: EditorController,
    onNativeTextInputRequested: (String) -> Unit = {},
) {
    val snackbarHostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    val importAssets = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) scope.launch {
            try {
                val asset = controller.importAsset(uri)
                snackbarHostState.showSnackbar("Imported ${asset.name}")
            } catch (error: Exception) {
                val message = error.message ?: "The file could not be imported."
                controller.log(message)
                snackbarHostState.showSnackbar(message)
            }
        }
    }
    val openScene = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) scope.launch {
            try {
                controller.loadSceneFromUri(uri)
                snackbarHostState.showSnackbar("Scene opened")
            } catch (error: Exception) {
                val message = error.message ?: "The scene could not be opened."
                controller.log(message)
                snackbarHostState.showSnackbar(message)
            }
        }
    }
    val exportScene = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json"),
    ) { uri ->
        if (uri != null) scope.launch {
            try {
                controller.exportScene(uri)
                snackbarHostState.showSnackbar("Scene exported as JSON")
            } catch (error: Exception) {
                val message = error.message ?: "The scene could not be exported."
                controller.log(message)
                snackbarHostState.showSnackbar(message)
            }
        }
    }

    LaunchedEffect(controller) {
        var previousFrame = 0L
        while (true) {
            val frame = withFrameNanos { it }
            if (previousFrame != 0L) {
                val delta = ((frame - previousFrame) / 1_000_000_000f).coerceIn(0f, 0.05f)
                controller.stepGame(delta)
                controller.advanceAnimation(delta)
            }
            previousFrame = frame
        }
    }

    BoxEngineEditorTheme {
        Scaffold(
            modifier = Modifier.fillMaxSize(),
            containerColor = EditorColors.background,
            snackbarHost = { SnackbarHost(snackbarHostState) },
            contentWindowInsets = WindowInsets.safeDrawing,
        ) { contentPadding ->
            BoxWithConstraints(
                modifier = Modifier.fillMaxSize().padding(contentPadding),
            ) {
                val wideLayout = maxWidth >= 900.dp && maxHeight >= 520.dp
                if (wideLayout) {
                    WideEditorLayout(
                        controller = controller,
                        onNativeTextInputRequested = onNativeTextInputRequested,
                        onImport = { importAssets.launch(arrayOf("*/*")) },
                        onOpenScene = { openScene.launch(arrayOf("application/json", "text/plain", "*/*")) },
                        onExportScene = {
                            val fileName = controller.document.name
                                .replace(Regex("[^A-Za-z0-9_-]"), "_")
                                .ifBlank { "BoxEngineScene" } + ".json"
                            exportScene.launch(fileName)
                        },
                    )
                } else {
                    CompactEditorLayout(
                        controller = controller,
                        onNativeTextInputRequested = onNativeTextInputRequested,
                        onImport = { importAssets.launch(arrayOf("*/*")) },
                        onOpenScene = { openScene.launch(arrayOf("application/json", "text/plain", "*/*")) },
                        onExportScene = {
                            val fileName = controller.document.name
                                .replace(Regex("[^A-Za-z0-9_-]"), "_")
                                .ifBlank { "BoxEngineScene" } + ".json"
                            exportScene.launch(fileName)
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun WideEditorLayout(
    controller: EditorController,
    onNativeTextInputRequested: (String) -> Unit,
    onImport: () -> Unit,
    onOpenScene: () -> Unit,
    onExportScene: () -> Unit,
) {
    Column(Modifier.fillMaxSize().background(EditorColors.background)) {
        EditorHeader(controller, onOpenScene, onImport, onExportScene)
        EditorToolbar(controller)
        Row(Modifier.weight(1f).fillMaxWidth()) {
            Surface(
                modifier = Modifier.width(225.dp).fillMaxSize(),
                color = EditorColors.panel,
                shape = RoundedCornerShape(0.dp),
            ) {
                HierarchyPanel(controller)
            }
            VerticalRule()
            Column(Modifier.weight(1f).fillMaxSize()) {
                WorkspaceHeader(controller)
                Box(Modifier.weight(1f).fillMaxWidth()) {
                    if (controller.pane == EditorPane.NATIVE_ENGINE) {
                        NativeEnginePanel(onNativeTextInputRequested)
                    } else {
                        WorkspaceViewport(controller)
                    }
                }
                BottomDock(controller, onImport, Modifier.fillMaxWidth().height(230.dp))
            }
            VerticalRule()
            Surface(
                modifier = Modifier.width(300.dp).fillMaxSize(),
                color = EditorColors.panel,
                shape = RoundedCornerShape(0.dp),
            ) {
                InspectorPanel(controller)
            }
        }
        EditorStatusBar(controller)
    }
}

@Composable
private fun CompactEditorLayout(
    controller: EditorController,
    onNativeTextInputRequested: (String) -> Unit,
    onImport: () -> Unit,
    onOpenScene: () -> Unit,
    onExportScene: () -> Unit,
) {
    Column(Modifier.fillMaxSize().background(EditorColors.background)) {
        EditorHeader(controller, onOpenScene, onImport, onExportScene)
        EditorToolbar(controller)
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (controller.pane) {
                EditorPane.SCENE, EditorPane.GAME -> {
                    Column(Modifier.fillMaxSize()) {
                        WorkspaceHeader(controller)
                        Box(Modifier.weight(1f).fillMaxWidth()) {
                            WorkspaceViewport(controller)
                        }
                        BottomDock(controller, onImport, Modifier.fillMaxWidth().height(168.dp))
                    }
                }
                EditorPane.HIERARCHY -> Surface(color = EditorColors.panel) { HierarchyPanel(controller) }
                EditorPane.INSPECTOR -> Surface(color = EditorColors.panel) { InspectorPanel(controller) }
                EditorPane.ASSETS -> AssetsPanel(controller, onImport, Modifier.fillMaxSize())
                EditorPane.TILE_PALETTE -> TilePalettePanel(controller, Modifier.fillMaxSize())
                EditorPane.SPRITE_EDITOR -> SpriteEditorPanel(controller, onImport, Modifier.fillMaxSize())
                EditorPane.ANIMATION -> AnimationPanel(controller, Modifier.fillMaxSize())
                EditorPane.CONSOLE -> ConsolePanel(controller, Modifier.fillMaxSize())
                EditorPane.NATIVE_ENGINE -> NativeEnginePanel(onNativeTextInputRequested)
            }
        }
        CompactNavigation(controller)
        EditorStatusBar(controller)
    }
}

@Composable
private fun EditorHeader(
    controller: EditorController,
    onOpenScene: () -> Unit,
    onImport: () -> Unit,
    onExportScene: () -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }
    Surface(color = EditorColors.panel, tonalElevation = 1.dp) {
        BoxWithConstraints {
            val compact = maxWidth < 640.dp
            Row(
                modifier = Modifier.fillMaxWidth().height(54.dp).padding(horizontal = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(if (compact) 5.dp else 10.dp),
            ) {
                Box(
                    modifier = Modifier.size(32.dp).clip(RoundedCornerShape(9.dp)).background(EditorColors.accent),
                    contentAlignment = Alignment.Center,
                ) {
                    Text("B", color = Color(0xFF071512), fontWeight = FontWeight.Black, fontSize = 19.sp)
                }
                if (!compact) {
                    Column(Modifier.weight(0.35f, fill = false)) {
                        Text("BOX ENGINE", color = EditorColors.text, fontWeight = FontWeight.Bold, fontSize = 13.sp, letterSpacing = 1.3.sp)
                        Text("GAME EDITOR", color = EditorColors.muted, fontSize = 9.sp, letterSpacing = 1.4.sp)
                    }
                }
                Box(Modifier.weight(1f, fill = true)) {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        color = EditorColors.panelRaised,
                        shape = RoundedCornerShape(9.dp),
                        onClick = { menuExpanded = true },
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(7.dp),
                        ) {
                            Text("◈", color = EditorColors.accent, fontSize = 14.sp)
                            Text(controller.document.name, color = EditorColors.text, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            if (controller.isDirty) Text("●", color = EditorColors.amber, fontSize = 9.sp)
                            Text("⌄", color = EditorColors.muted, fontSize = 14.sp)
                        }
                    }
                    DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                DropdownMenuItem(text = { Text("New scene") }, onClick = { menuExpanded = false; controller.newScene() })
                DropdownMenuItem(text = { Text("Save project") }, onClick = { menuExpanded = false; controller.saveProject() })
                DropdownMenuItem(text = { Text("Open scene JSON…") }, onClick = { menuExpanded = false; onOpenScene() })
                DropdownMenuItem(text = { Text("Export scene JSON…") }, onClick = { menuExpanded = false; onExportScene() })
                DropdownMenuItem(text = { Text("Import asset…") }, onClick = { menuExpanded = false; onImport() })
                        DropdownMenuItem(text = { Text("Native GLES engine preview") }, onClick = { menuExpanded = false; controller.pane = EditorPane.NATIVE_ENGINE })
                    }
                }
                if (controller.canUndo) {
                SmallIconButton("↶", "Undo", enabled = controller.canUndo, onClick = controller::undo)
            } else {
                SmallIconButton("↶", "Undo", enabled = false, onClick = {})
            }
            SmallIconButton("↷", "Redo", enabled = controller.canRedo, onClick = controller::redo)
            if (compact) {
                SmallIconButton("▣", "Save project", onClick = { controller.saveProject() })
            } else {
                Button(
                    onClick = { controller.saveProject() },
                    modifier = Modifier.height(34.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 11.dp, vertical = 0.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.panelSelected, contentColor = EditorColors.text),
                ) {
                    Text("Save", fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                }
            }
            RunControls(controller)
            }
        }
    }
}

@Composable
private fun EditorToolbar(controller: EditorController) {
    var addExpanded by remember { mutableStateOf(false) }
    Surface(color = EditorColors.panel, tonalElevation = 0.dp) {
        Row(
            modifier = Modifier.fillMaxWidth().height(45.dp).horizontalScroll(rememberScrollState()).padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            Box {
                ToolButton("+ Add", selected = false, accent = true, onClick = { addExpanded = true })
                DropdownMenu(expanded = addExpanded, onDismissRequest = { addExpanded = false }) {
                    listOf(
                        "Empty object" to EditorObjectKind.EMPTY,
                        "Sprite" to EditorObjectKind.SPRITE,
                        "Tilemap" to EditorObjectKind.TILEMAP,
                        "Camera" to EditorObjectKind.CAMERA,
                        "3D cube" to EditorObjectKind.MESH3D,
                        "Audio source" to EditorObjectKind.AUDIO_SOURCE,
                    ).forEach { (label, kind) ->
                        DropdownMenuItem(
                            text = { Text(label) },
                            onClick = {
                                addExpanded = false
                                if (kind == EditorObjectKind.MESH3D && controller.document.dimension != SceneDimension.THREE_D) {
                                    controller.setDimension(SceneDimension.THREE_D)
                                }
                                controller.addObject(kind)
                            },
                        )
                    }
                }
            }
            VerticalRule(modifier = Modifier.height(22.dp).padding(horizontal = 3.dp))
            ToolButton("↖", controller.tool == EditorTool.SELECT, "Select", onClick = { controller.tool = EditorTool.SELECT })
            ToolButton("✣", controller.tool == EditorTool.MOVE, "Move", onClick = { controller.tool = EditorTool.MOVE })
            ToolButton("⟳", controller.tool == EditorTool.ROTATE, "Rotate", onClick = { controller.tool = EditorTool.ROTATE })
            ToolButton("⤢", controller.tool == EditorTool.SCALE, "Scale", onClick = { controller.tool = EditorTool.SCALE })
            ToolButton("✋", controller.tool == EditorTool.PAN, "Pan", onClick = { controller.tool = EditorTool.PAN })
            if (controller.document.dimension == SceneDimension.TWO_D) {
                ToolButton("▦", controller.tool == EditorTool.PAINT, "Paint tiles", onClick = {
                    controller.bottomPanel = BottomPanel.TILE_PALETTE
                    controller.tool = EditorTool.PAINT
                    controller.pane = EditorPane.SCENE
                })
                ToolButton("⌫", controller.tool == EditorTool.ERASE, "Erase tiles", onClick = {
                    controller.bottomPanel = BottomPanel.TILE_PALETTE
                    controller.tool = EditorTool.ERASE
                    controller.pane = EditorPane.SCENE
                })
            }
            Spacer(Modifier.width(5.dp))
            if (controller.document.dimension == SceneDimension.THREE_D) {
                Text("3D · wireframe", color = EditorColors.muted, fontSize = 10.sp, maxLines = 1)
            } else {
                Text("2D · scene", color = EditorColors.muted, fontSize = 10.sp, maxLines = 1)
            }
        }
    }
}

@Composable
private fun WorkspaceHeader(controller: EditorController) {
    Surface(color = EditorColors.background, tonalElevation = 0.dp) {
        Row(
            modifier = Modifier.fillMaxWidth().height(38.dp).padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            WorkspaceTab("Scene", controller.pane != EditorPane.GAME, onClick = { controller.pane = EditorPane.SCENE })
            WorkspaceTab("Game", controller.pane == EditorPane.GAME, onClick = { controller.pane = EditorPane.GAME })
            Spacer(Modifier.width(10.dp))
            WorkspaceTab("2D", controller.document.dimension == SceneDimension.TWO_D, onClick = { controller.setDimension(SceneDimension.TWO_D) })
            WorkspaceTab("3D", controller.document.dimension == SceneDimension.THREE_D, onClick = { controller.setDimension(SceneDimension.THREE_D) })
            Spacer(Modifier.weight(1f))
            if (controller.document.dimension == SceneDimension.TWO_D) {
                Text("Grid", color = EditorColors.muted, fontSize = 10.sp)
                SmallIconButton(
                    if (controller.document.snappingEnabled) "⌖" else "·",
                    "Toggle grid snapping",
                    onClick = controller::toggleSnapping,
                )
            } else {
                Text("Orbit · pinch to zoom", color = EditorColors.muted, fontSize = 10.sp)
            }
            SmallIconButton("−", "Zoom out", onClick = {
                if (controller.document.dimension == SceneDimension.TWO_D) {
                    controller.viewportZoom = (controller.viewportZoom / 1.18f).coerceIn(0.25f, 5f)
                } else {
                    controller.cameraZoom = (controller.cameraZoom / 1.18f).coerceIn(0.35f, 3f)
                }
            })
            Text(
                if (controller.document.dimension == SceneDimension.TWO_D) "${(controller.viewportZoom * 100).toInt()}%" else "${(controller.cameraZoom * 100).toInt()}%",
                color = EditorColors.muted,
                fontSize = 10.sp,
            )
            SmallIconButton("+", "Zoom in", onClick = {
                if (controller.document.dimension == SceneDimension.TWO_D) {
                    controller.viewportZoom = (controller.viewportZoom * 1.18f).coerceIn(0.25f, 5f)
                } else {
                    controller.cameraZoom = (controller.cameraZoom * 1.18f).coerceIn(0.35f, 3f)
                }
            })
        }
    }
}

@Composable
private fun WorkspaceViewport(controller: EditorController) {
    val game = controller.pane == EditorPane.GAME
    Box(Modifier.fillMaxSize().background(Color(0xFF0A111A))) {
        if (game && controller.document.dimension == SceneDimension.THREE_D) {
            Column(
                Modifier.align(Alignment.Center).padding(20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text("3D game preview is not implemented yet", color = EditorColors.text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Text("The 3D scene editor currently supports projected wireframe editing only.", color = EditorColors.muted, fontSize = 10.sp)
                TextButton(onClick = { controller.pane = EditorPane.SCENE }) {
                    Text("Return to 3D scene", color = EditorColors.accent, fontSize = 10.sp)
                }
            }
        } else {
            SceneViewport(controller, isGameView = game, modifier = Modifier.fillMaxSize())
            if (game) GamePreviewOverlay(controller)
        }
    }
}

@Composable
private fun BoxScope.GamePreviewOverlay(controller: EditorController) {
    Row(
        modifier = Modifier.align(Alignment.TopStart).padding(12.dp)
            .clip(RoundedCornerShape(9.dp)).background(Color(0xD9101823)).padding(horizontal = 11.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(Modifier.size(7.dp).clip(CircleShape).background(if (controller.playState == PlayState.PLAYING) EditorColors.green else EditorColors.amber))
        Text("${controller.collectedObjectIds.size} / ${controller.document.objects.count { it.name.contains("coin", true) }} COINS", color = EditorColors.text, fontSize = 10.sp, fontWeight = FontWeight.Bold)
        Text("${controller.gameTime.toInt()}s", color = EditorColors.muted, fontSize = 10.sp)
    }
    if (controller.playState == PlayState.STOPPED) {
        Surface(
            modifier = Modifier.align(Alignment.Center),
            color = Color(0xEF101722),
            shape = RoundedCornerShape(16.dp),
            tonalElevation = 10.dp,
        ) {
            Column(Modifier.padding(18.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("READY TO PLAY", color = EditorColors.accent, fontSize = 10.sp, letterSpacing = 1.3.sp, fontWeight = FontWeight.Bold)
                Text("Use the touch pad to run the sample platformer.", color = EditorColors.muted, fontSize = 11.sp)
                Button(
                    onClick = controller::startPlay,
                    shape = RoundedCornerShape(9.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.accent, contentColor = Color(0xFF071512)),
                ) { Text("Start playtest", fontWeight = FontWeight.Bold) }
            }
        }
    } else {
        Column(Modifier.align(Alignment.BottomCenter).padding(bottom = 12.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            if (controller.playState == PlayState.PAUSED) {
                Text("PAUSED", color = EditorColors.amber, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
                GameHoldControl("‹", direction = -1, controller = controller)
                GameHoldControl("›", direction = 1, controller = controller)
                Button(
                    onClick = controller::requestJump,
                    shape = CircleShape,
                    modifier = Modifier.height(48.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.accent, contentColor = Color(0xFF071512)),
                ) { Text("JUMP", fontSize = 10.sp, fontWeight = FontWeight.Black) }
            }
        }
    }
}

@Composable
private fun GameHoldControl(label: String, direction: Int, controller: EditorController) {
    Surface(
        modifier = Modifier.size(50.dp).androidPressHold(
            onStart = { controller.setHorizontalInput(direction) },
            onEnd = { controller.setHorizontalInput(0) },
        ),
        color = Color(0xE61B2938),
        shape = CircleShape,
        border = androidx.compose.foundation.BorderStroke(1.dp, EditorColors.border),
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(label, color = EditorColors.text, fontSize = 26.sp, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun CompactNavigation(controller: EditorController) {
    val entries = listOf(
        Triple("Scene", "▧", EditorPane.SCENE),
        Triple("Game", "▶", EditorPane.GAME),
        Triple("Objects", "☷", EditorPane.HIERARCHY),
        Triple("Inspect", "⚙", EditorPane.INSPECTOR),
        Triple("Assets", "◫", EditorPane.ASSETS),
        Triple("Console", "•••", EditorPane.CONSOLE),
        Triple("Native", "◈", EditorPane.NATIVE_ENGINE),
    )
    Surface(color = EditorColors.panel, tonalElevation = 3.dp) {
        Row(
            modifier = Modifier.fillMaxWidth().height(50.dp).horizontalScroll(rememberScrollState()).padding(horizontal = 5.dp),
            horizontalArrangement = Arrangement.SpaceEvenly,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            entries.forEach { (title, icon, target) ->
                val selected = when (target) {
                    EditorPane.SCENE -> controller.pane == EditorPane.SCENE
                    EditorPane.GAME -> controller.pane == EditorPane.GAME
                    else -> controller.pane == target
                }
                CompactNavItem(title, icon, selected, onClick = {
                    controller.pane = target
                    if (target == EditorPane.CONSOLE) controller.bottomPanel = BottomPanel.CONSOLE
                })
            }
        }
    }
}

@Composable
private fun RunControls(controller: EditorController) {
    Row(horizontalArrangement = Arrangement.spacedBy(5.dp), verticalAlignment = Alignment.CenterVertically) {
        when (controller.playState) {
            PlayState.STOPPED -> {
                Button(
                    onClick = controller::startPlay,
                    enabled = controller.document.dimension == SceneDimension.TWO_D,
                    modifier = Modifier.height(34.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 12.dp, vertical = 0.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.accent, contentColor = Color(0xFF061411)),
                ) { Text("▶ Play", fontSize = 12.sp, fontWeight = FontWeight.Bold) }
            }
            PlayState.PLAYING -> {
                Button(
                    onClick = controller::pausePlay,
                    modifier = Modifier.height(34.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 9.dp, vertical = 0.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.amber, contentColor = Color(0xFF211503)),
                ) { Text("Ⅱ Pause", fontSize = 11.sp, fontWeight = FontWeight.Bold) }
                Button(
                    onClick = controller::stopPlay,
                    modifier = Modifier.height(34.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 9.dp, vertical = 0.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.panelSelected, contentColor = EditorColors.text),
                ) { Text("■ Stop", fontSize = 11.sp) }
            }
            PlayState.PAUSED -> {
                Button(
                    onClick = controller::startPlay,
                    modifier = Modifier.height(34.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 9.dp, vertical = 0.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.accent, contentColor = Color(0xFF061411)),
                ) { Text("▶ Resume", fontSize = 11.sp, fontWeight = FontWeight.Bold) }
                Button(
                    onClick = controller::stopPlay,
                    modifier = Modifier.height(34.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 9.dp, vertical = 0.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = EditorColors.panelSelected, contentColor = EditorColors.text),
                ) { Text("■ Stop", fontSize = 11.sp) }
            }
        }
    }
}

@Composable
private fun SmallIconButton(
    glyph: String,
    description: String,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    IconButton(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.size(32.dp).semantics { contentDescription = description },
    ) {
        Text(glyph, color = if (enabled) EditorColors.muted else EditorColors.subtle, fontSize = 17.sp, lineHeight = 20.sp)
    }
}

@Composable
private fun ToolButton(
    label: String,
    selected: Boolean,
    tooltip: String = label,
    accent: Boolean = false,
    onClick: () -> Unit,
) {
    val color = when {
        accent -> EditorColors.accent
        selected -> EditorColors.panelSelected
        else -> Color.Transparent
    }
    Surface(
        onClick = onClick,
        modifier = Modifier.height(31.dp).semantics { contentDescription = tooltip },
        color = color,
        shape = RoundedCornerShape(7.dp),
    ) {
        Box(Modifier.padding(horizontal = 9.dp), contentAlignment = Alignment.Center) {
            Text(label, color = if (accent) Color(0xFF071512) else if (selected) EditorColors.accent else EditorColors.muted, fontSize = 12.sp, fontWeight = if (selected || accent) FontWeight.Bold else FontWeight.Medium)
        }
    }
}

@Composable
private fun WorkspaceTab(label: String, selected: Boolean, onClick: () -> Unit) {
    Surface(onClick = onClick, color = if (selected) EditorColors.panelSelected else Color.Transparent, shape = RoundedCornerShape(6.dp)) {
        Text(
            label,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
            color = if (selected) EditorColors.text else EditorColors.muted,
            fontSize = 11.sp,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
        )
    }
}

@Composable
private fun CompactNavItem(label: String, icon: String, selected: Boolean, onClick: () -> Unit) {
    Surface(onClick = onClick, color = if (selected) EditorColors.panelSelected else Color.Transparent, shape = RoundedCornerShape(9.dp)) {
        Column(
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 4.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(0.dp),
        ) {
            Text(icon, color = if (selected) EditorColors.accent else EditorColors.muted, fontSize = 15.sp, lineHeight = 17.sp)
            Text(label, color = if (selected) EditorColors.text else EditorColors.muted, fontSize = 9.sp, lineHeight = 11.sp)
        }
    }
}

@Composable
private fun EditorStatusBar(controller: EditorController) {
    Surface(color = EditorColors.panel, tonalElevation = 0.dp) {
        Row(
            Modifier.fillMaxWidth().height(24.dp).padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            Box(Modifier.size(5.dp).clip(CircleShape).background(if (controller.isDirty) EditorColors.amber else EditorColors.green))
            Text(controller.statusMessage, color = EditorColors.muted, fontSize = 9.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.weight(1f))
            Text(if (controller.document.dimension == SceneDimension.TWO_D) "2D" else "3D", color = EditorColors.subtle, fontSize = 9.sp)
            Text("·", color = EditorColors.subtle, fontSize = 9.sp)
            Text("${controller.document.objects.size} objects", color = EditorColors.subtle, fontSize = 9.sp)
        }
    }
}

@Composable
private fun VerticalRule(modifier: Modifier = Modifier.fillMaxHeight()) {
    Spacer(modifier.width(1.dp).background(EditorColors.borderSoft))
}

@Composable
private fun Modifier.androidPressHold(onStart: () -> Unit, onEnd: () -> Unit): Modifier {
    val latestStart = rememberUpdatedState(onStart)
    val latestEnd = rememberUpdatedState(onEnd)
    return pointerInput(Unit) {
        detectTapGestures(onPress = {
            latestStart.value()
            try {
                tryAwaitRelease()
            } finally {
                latestEnd.value()
            }
        })
    }
}
