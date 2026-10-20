// Path: app/src/main/kotlin/com/boxengine/app/editor/ui/HierarchyInspector.kt
package com.boxengine.app.editor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.lazy.items
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
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.boxengine.app.editor.model.AssetKind
import com.boxengine.app.editor.model.BodyType2D
import com.boxengine.app.editor.model.CameraComponent
import com.boxengine.app.editor.model.Collider2D
import com.boxengine.app.editor.model.EditorController
import com.boxengine.app.editor.model.EditorObject
import com.boxengine.app.editor.model.EditorObjectKind
import com.boxengine.app.editor.model.PhysicsBody2D
import com.boxengine.app.editor.model.Primitive3D
import com.boxengine.app.editor.model.SpriteComponent
import com.boxengine.app.editor.model.Transform3D

/** Presents scene organization controls and editable component inspectors. */
@Composable
fun HierarchyPanel(controller: EditorController) {
    var addMenu by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().background(EditorColors.panel)) {
        Row(
            modifier = Modifier.fillMaxWidth().height(42.dp).padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text("HIERARCHY", color = EditorColors.muted, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.2.sp)
            Spacer(Modifier.weight(1f))
            Text("${controller.document.objects.size}", color = EditorColors.subtle, fontSize = 10.sp)
            Box {
                PanelIconButton("+", "Create object", onClick = { addMenu = true })
                DropdownMenu(expanded = addMenu, onDismissRequest = { addMenu = false }) {
                    listOf(
                        "Empty object" to EditorObjectKind.EMPTY,
                        "Sprite" to EditorObjectKind.SPRITE,
                        "Tilemap" to EditorObjectKind.TILEMAP,
                        "Camera" to EditorObjectKind.CAMERA,
                        "3D cube" to EditorObjectKind.MESH3D,
                        "Audio source" to EditorObjectKind.AUDIO_SOURCE,
                    ).forEach { (name, kind) ->
                        DropdownMenuItem(text = { Text(name) }, onClick = {
                            addMenu = false
                            if (kind == EditorObjectKind.MESH3D) controller.setDimension(com.boxengine.app.editor.model.SceneDimension.THREE_D)
                            controller.addObject(kind)
                        })
                    }
                }
            }
        }
        Surface(Modifier.fillMaxWidth().height(1.dp), color = EditorColors.borderSoft) {}
        if (controller.document.objects.isEmpty()) {
            EmptyPanelMessage("No objects in this scene", "Use + to create your first object.")
        } else {
            androidx.compose.foundation.lazy.LazyColumn(
                modifier = Modifier.weight(1f).fillMaxWidth().padding(vertical = 5.dp),
                verticalArrangement = Arrangement.spacedBy(1.dp),
            ) {
                items(controller.document.objects, key = { it.id }) { item ->
                    HierarchyRow(controller, item, hierarchyDepth(controller, item))
                }
            }
        }
        Surface(Modifier.fillMaxWidth().height(1.dp), color = EditorColors.borderSoft) {}
        Row(
            modifier = Modifier.fillMaxWidth().height(42.dp).padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            Text("Objects", color = EditorColors.subtle, fontSize = 10.sp)
            Spacer(Modifier.weight(1f))
            PanelIconButton("⧉", "Duplicate selected", enabled = controller.selectedObject != null, onClick = { controller.duplicateSelected() })
            PanelIconButton("↑", "Move up", enabled = controller.selectedObject != null, onClick = { controller.selectedObjectId?.let { controller.moveObjectInHierarchy(it, -1) } })
            PanelIconButton("↓", "Move down", enabled = controller.selectedObject != null, onClick = { controller.selectedObjectId?.let { controller.moveObjectInHierarchy(it, 1) } })
            PanelIconButton("×", "Delete selected", enabled = controller.selectedObject != null, onClick = { controller.selectedObjectId?.let(controller::deleteObject) }, danger = true)
        }
    }
}

@Composable
private fun HierarchyRow(controller: EditorController, item: EditorObject, depth: Int) {
    var menuExpanded by remember { mutableStateOf(false) }
    var renameDialog by remember { mutableStateOf(false) }
    var nameDraft by remember(item.name) { mutableStateOf(item.name) }
    var reparentMenu by remember { mutableStateOf(false) }
    val selected = controller.selectedObjectId == item.id
    val kindColor = when (item.kind) {
        EditorObjectKind.CAMERA -> EditorColors.blue
        EditorObjectKind.MESH3D -> EditorColors.purple
        EditorObjectKind.TILEMAP -> EditorColors.green
        EditorObjectKind.AUDIO_SOURCE -> EditorColors.amber
        EditorObjectKind.SPRITE -> EditorColors.accent
        EditorObjectKind.EMPTY -> EditorColors.muted
    }
    Row(
        modifier = Modifier.fillMaxWidth()
            .padding(start = (8 + depth * 14).dp, end = 5.dp, top = 1.dp, bottom = 1.dp)
            .clip(RoundedCornerShape(7.dp))
            .background(if (selected) EditorColors.panelSelected else Color.Transparent)
            .clickable { controller.selectObject(item.id) }
            .height(36.dp)
            .padding(start = 7.dp, end = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        Box(Modifier.size(7.dp).clip(CircleShape).background(if (item.visible) kindColor else EditorColors.subtle))
        Text(objectGlyph(item), color = kindColor, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        Text(
            item.name,
            modifier = Modifier.weight(1f),
            color = if (item.visible) EditorColors.text else EditorColors.subtle,
            fontSize = 11.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Box {
            PanelIconButton("⋮", "Object actions", onClick = { menuExpanded = true })
            DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                DropdownMenuItem(text = { Text("Rename") }, onClick = { menuExpanded = false; nameDraft = item.name; renameDialog = true })
                DropdownMenuItem(text = { Text("Duplicate") }, onClick = { menuExpanded = false; controller.selectObject(item.id); controller.duplicateSelected() })
                DropdownMenuItem(text = { Text(if (item.visible) "Hide in scene" else "Show in scene") }, onClick = { menuExpanded = false; controller.setObjectVisibility(item.id, !item.visible) })
                DropdownMenuItem(text = { Text("Move up") }, onClick = { menuExpanded = false; controller.moveObjectInHierarchy(item.id, -1) })
                DropdownMenuItem(text = { Text("Move down") }, onClick = { menuExpanded = false; controller.moveObjectInHierarchy(item.id, 1) })
                DropdownMenuItem(text = { Text(if (item.parentId == null) "Choose parent…" else "Change parent…") }, onClick = { menuExpanded = false; reparentMenu = true })
                if (item.parentId != null) DropdownMenuItem(text = { Text("Move to root") }, onClick = { menuExpanded = false; controller.makeChildOf(item.id, null) })
                DropdownMenuItem(text = { Text("Delete") }, onClick = { menuExpanded = false; controller.deleteObject(item.id) }, leadingIcon = { Text("×", color = EditorColors.red) })
            }
            DropdownMenu(expanded = reparentMenu, onDismissRequest = { reparentMenu = false }) {
                DropdownMenuItem(text = { Text("No parent (root)") }, onClick = { reparentMenu = false; controller.makeChildOf(item.id, null) })
                controller.document.objects.filter { it.id != item.id && it.id !in hierarchyDescendants(controller, item.id) }.forEach { parent ->
                    DropdownMenuItem(text = { Text("Child of ${parent.name}", maxLines = 1, overflow = TextOverflow.Ellipsis) }, onClick = {
                        reparentMenu = false
                        controller.makeChildOf(item.id, parent.id)
                    })
                }
            }
        }
    }
    if (renameDialog) {
        AlertDialog(
            onDismissRequest = { renameDialog = false },
            title = { Text("Rename object") },
            text = {
                OutlinedTextField(
                    value = nameDraft,
                    onValueChange = { nameDraft = it.take(48) },
                    singleLine = true,
                    label = { Text("Object name") },
                )
            },
            confirmButton = {
                TextButton(onClick = { controller.renameObject(item.id, nameDraft); renameDialog = false }) { Text("Rename") }
            },
            dismissButton = { TextButton(onClick = { renameDialog = false }) { Text("Cancel") } },
            containerColor = EditorColors.panelRaised,
        )
    }
}

@Composable
fun InspectorPanel(controller: EditorController) {
    val item = controller.selectedObject
    Column(Modifier.fillMaxSize().background(EditorColors.panel)) {
        Row(
            Modifier.fillMaxWidth().height(42.dp).padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("INSPECTOR", color = EditorColors.muted, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.2.sp)
            Spacer(Modifier.weight(1f))
            if (item != null) Text(objectGlyph(item), color = EditorColors.accent, fontSize = 14.sp)
        }
        Surface(Modifier.fillMaxWidth().height(1.dp), color = EditorColors.borderSoft) {}
        if (item == null) {
            EmptyPanelMessage("Nothing selected", "Choose a scene object to edit its components.")
        } else {
            Column(
                Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 11.dp, vertical = 10.dp),
                verticalArrangement = Arrangement.spacedBy(9.dp),
            ) {
                InspectorIdentity(controller, item)
                InspectorSection("Transform", "XYZ") {
                    TransformFields(controller, item)
                }
                if (item.kind == EditorObjectKind.SPRITE || item.sprite != null) {
                    InspectorSection("Sprite Renderer", "2D") {
                        SpriteFields(controller, item)
                    }
                }
                if (item.camera != null) {
                    InspectorSection("Camera", if (controller.document.dimension == com.boxengine.app.editor.model.SceneDimension.TWO_D) "2D" else "3D") {
                        CameraFields(controller, item)
                    }
                }
                if (item.body != null) {
                    InspectorSection("Rigidbody 2D", "PHYSICS") { BodyFields(controller, item) }
                } else if (item.kind != EditorObjectKind.MESH3D) {
                    ComponentAddButton("+ Add Rigidbody 2D") { controller.addPhysicsBody(item.id) }
                }
                if (item.collider != null) {
                    InspectorSection("Collider 2D", if (item.collider.isTrigger) "TRIGGER" else "SOLID") { ColliderFields(controller, item) }
                } else if (item.kind != EditorObjectKind.MESH3D && item.kind != EditorObjectKind.CAMERA) {
                    ComponentAddButton("+ Add Collider 2D") { controller.addCollider(item.id) }
                }
                if (item.kind == EditorObjectKind.MESH3D) {
                    InspectorSection("Mesh Primitive", "3D") {
                        Row(horizontalArrangement = Arrangement.spacedBy(7.dp)) {
                            ChoiceChip("Cube", item.primitive3D == Primitive3D.CUBE) {
                                controller.updateObject(item.id) { it.copy(primitive3D = Primitive3D.CUBE) }
                            }
                            ChoiceChip("Plane", item.primitive3D == Primitive3D.PLANE) {
                                controller.updateObject(item.id) { it.copy(primitive3D = Primitive3D.PLANE) }
                            }
                        }
                    }
                }
                if (item.kind != EditorObjectKind.MESH3D) {
                    ComponentAddButton("+ Add Audio Source") {
                        controller.addObject(EditorObjectKind.AUDIO_SOURCE, item.id)
                    }
                }
                Button(
                    onClick = { controller.deleteObject(item.id) },
                    modifier = Modifier.fillMaxWidth().height(37.dp),
                    shape = RoundedCornerShape(8.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF321D25), contentColor = EditorColors.red),
                ) { Text("Delete object", fontSize = 11.sp) }
            }
        }
    }
}

@Composable
private fun InspectorIdentity(controller: EditorController, item: EditorObject) {
    var name by remember(item.id, item.name) { mutableStateOf(item.name) }
    var focused by remember { mutableStateOf(false) }
    OutlinedTextField(
        value = name,
        onValueChange = { name = it.take(48) },
        singleLine = true,
        modifier = Modifier.fillMaxWidth().height(57.dp).onFocusChanged { focusState ->
            if (focused && !focusState.isFocused && name.trim().isNotEmpty() && name != item.name) {
                controller.renameObject(item.id, name)
            }
            focused = focusState.isFocused
        },
        label = { Text("Name", fontSize = 10.sp) },
        textStyle = androidx.compose.ui.text.TextStyle(color = EditorColors.text, fontSize = 12.sp),
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(onDone = { if (name.trim().isNotEmpty()) controller.renameObject(item.id, name) }),
        colors = inspectorTextFieldColors(),
    )
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
        MetaPill(item.kind.name.replace('_', ' '))
        if (item.body != null) MetaPill(item.body.bodyType.name)
        if (item.collider != null) MetaPill(if (item.collider.isTrigger) "TRIGGER" else "COLLIDER")
    }
}

@Composable
private fun TransformFields(controller: EditorController, item: EditorObject) {
    val t = item.transform
    TripleFieldRow("Position", listOf("X", "Y", "Z"), listOf(t.x, t.y, t.z)) { index, value ->
        controller.updateTransform(item.id, when (index) {
            0 -> t.copy(x = value); 1 -> t.copy(y = value); else -> t.copy(z = value)
        })
    }
    TripleFieldRow("Rotation", listOf("X°", "Y°", "Z°"), listOf(t.rotationX, t.rotationY, t.rotationZ)) { index, value ->
        controller.updateTransform(item.id, when (index) {
            0 -> t.copy(rotationX = value); 1 -> t.copy(rotationY = value); else -> t.copy(rotationZ = value)
        })
    }
    TripleFieldRow("Scale", listOf("X", "Y", "Z"), listOf(t.scaleX, t.scaleY, t.scaleZ)) { index, value ->
        val safe = value.coerceIn(0.05f, 50f)
        controller.updateTransform(item.id, when (index) {
            0 -> t.copy(scaleX = safe); 1 -> t.copy(scaleY = safe); else -> t.copy(scaleZ = safe)
        })
    }
}

@Composable
private fun SpriteFields(controller: EditorController, item: EditorObject) {
    val sprite = item.sprite ?: SpriteComponent()
    Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
        SpriteAssetDropdown(
            controller = controller,
            selectedAssetId = sprite.assetId,
            onSelect = { controller.assignSpriteAsset(item.id, it) },
        )
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            NumberField("Width", sprite.width, Modifier.weight(1f), min = 0.05f, max = 50f) { value ->
                controller.updateSprite(item.id) { it.copy(width = value) }
            }
            NumberField("Height", sprite.height, Modifier.weight(1f), min = 0.05f, max = 50f) { value ->
                controller.updateSprite(item.id) { it.copy(height = value) }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(5.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Tint", color = EditorColors.muted, fontSize = 10.sp)
            val tintChoices = listOf(0xFF39C6B0, 0xFFFFC85A, 0xFFE36C73, 0xFF74A8EE, 0xFFE8EEF4, 0xFF8ACC75)
            tintChoices.forEach { argb ->
                Box(
                    modifier = Modifier.size(19.dp).clip(CircleShape)
                        .background(Color(argb.toInt()))
                        .clickable { controller.updateSprite(item.id) { it.copy(tintArgb = argb) } },
                )
            }
        }
        NumberField("Sort order", sprite.sortingOrder.toFloat(), Modifier.fillMaxWidth(), min = -1000f, max = 1000f) { value ->
            controller.updateSprite(item.id) { it.copy(sortingOrder = value.toInt()) }
        }
    }
}

@Composable
private fun BodyFields(controller: EditorController, item: EditorObject) {
    val body = item.body ?: return
    Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
        var bodyTypeMenu by remember { mutableStateOf(false) }
        Box {
            Surface(
                onClick = { bodyTypeMenu = true },
                color = EditorColors.panelSelected,
                shape = RoundedCornerShape(8.dp),
            ) {
                Row(Modifier.fillMaxWidth().padding(horizontal = 9.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("Body type", color = EditorColors.muted, fontSize = 10.sp)
                    Spacer(Modifier.weight(1f))
                    Text(body.bodyType.name, color = EditorColors.text, fontSize = 11.sp)
                    Text("  ⌄", color = EditorColors.accent, fontSize = 13.sp)
                }
            }
            DropdownMenu(expanded = bodyTypeMenu, onDismissRequest = { bodyTypeMenu = false }) {
                BodyType2D.entries.forEach { value ->
                    DropdownMenuItem(text = { Text(value.name) }, onClick = {
                        bodyTypeMenu = false
                        controller.updateObject(item.id) { it.copy(body = it.body?.copy(bodyType = value)) }
                    })
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            NumberField("Mass", body.mass, Modifier.weight(1f), min = 0.01f, max = 1000f) { value ->
                controller.updateObject(item.id) { it.copy(body = it.body?.copy(mass = value)) }
            }
            NumberField("Gravity", body.gravityScale, Modifier.weight(1f), min = -10f, max = 10f) { value ->
                controller.updateObject(item.id) { it.copy(body = it.body?.copy(gravityScale = value)) }
            }
        }
        NumberField("Linear drag", body.linearDrag, Modifier.fillMaxWidth(), min = 0f, max = 100f) { value ->
            controller.updateObject(item.id) { it.copy(body = it.body?.copy(linearDrag = value)) }
        }
        SwitchProperty("Fixed rotation", body.fixedRotation) { value ->
            controller.updateObject(item.id) { it.copy(body = it.body?.copy(fixedRotation = value)) }
        }
        ComponentRemoveButton("Remove Rigidbody 2D") { controller.removePhysicsBody(item.id) }
    }
}

@Composable
private fun ColliderFields(controller: EditorController, item: EditorObject) {
    val collider = item.collider ?: return
    Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            NumberField("Width", collider.width, Modifier.weight(1f), min = 0.05f, max = 100f) { value ->
                controller.updateObject(item.id) { it.copy(collider = it.collider?.copy(width = value)) }
            }
            NumberField("Height", collider.height, Modifier.weight(1f), min = 0.05f, max = 100f) { value ->
                controller.updateObject(item.id) { it.copy(collider = it.collider?.copy(height = value)) }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            NumberField("Offset X", collider.offsetX, Modifier.weight(1f), min = -100f, max = 100f) { value ->
                controller.updateObject(item.id) { it.copy(collider = it.collider?.copy(offsetX = value)) }
            }
            NumberField("Offset Y", collider.offsetY, Modifier.weight(1f), min = -100f, max = 100f) { value ->
                controller.updateObject(item.id) { it.copy(collider = it.collider?.copy(offsetY = value)) }
            }
        }
        SwitchProperty("Is trigger", collider.isTrigger) { value ->
            controller.updateObject(item.id) { it.copy(collider = it.collider?.copy(isTrigger = value)) }
        }
        ComponentRemoveButton("Remove Collider 2D") { controller.removeCollider(item.id) }
    }
}

@Composable
private fun CameraFields(controller: EditorController, item: EditorObject) {
    val camera = item.camera ?: CameraComponent()
    Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
        NumberField("Orthographic size", camera.orthographicSize, Modifier.fillMaxWidth(), min = 0.1f, max = 100f) { value ->
            controller.updateObject(item.id) { it.copy(camera = it.camera?.copy(orthographicSize = value)) }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            NumberField("Near clip", camera.nearClip, Modifier.weight(1f), min = -1000f, max = 1000f) { value ->
                controller.updateObject(item.id) { it.copy(camera = it.camera?.copy(nearClip = value)) }
            }
            NumberField("Far clip", camera.farClip, Modifier.weight(1f), min = 0.1f, max = 10000f) { value ->
                controller.updateObject(item.id) { it.copy(camera = it.camera?.copy(farClip = value)) }
            }
        }
        SwitchProperty("Primary camera", camera.primary) { value ->
            controller.setCameraPrimary(item.id, value)
        }
    }
}

@Composable
private fun TripleFieldRow(
    title: String,
    labels: List<String>,
    values: List<Float>,
    onValue: (Int, Float) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(title, color = EditorColors.muted, fontSize = 9.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
            values.forEachIndexed { index, value ->
                NumberField(labels[index], value, Modifier.weight(1f), min = -10000f, max = 10000f) { number -> onValue(index, number) }
            }
        }
    }
}

@Composable
private fun NumberField(
    label: String,
    value: Float,
    modifier: Modifier = Modifier,
    min: Float = -10000f,
    max: Float = 10000f,
    onCommit: (Float) -> Unit,
) {
    val formattedValue = if (value.isFinite()) String.format(java.util.Locale.US, "%.3f", value).trimEnd('0').trimEnd('.') else "0"
    var text by remember(value) { mutableStateOf(formattedValue) }
    var focused by remember { mutableStateOf(false) }
    fun commit() {
        val parsed = text.toFloatOrNull()
        if (parsed == null) {
            text = formattedValue
        } else {
            val safe = parsed.coerceIn(min, max)
            text = String.format(java.util.Locale.US, "%.3f", safe).trimEnd('0').trimEnd('.')
            if (safe != value) onCommit(safe)
        }
    }
    OutlinedTextField(
        value = text,
        onValueChange = { input ->
            if (input.length <= 14 && input.matches(Regex("-?\\d*(\\.\\d*)?"))) text = input
        },
        modifier = modifier.height(55.dp).onFocusChanged { focusState ->
            if (focused && !focusState.isFocused) commit()
            focused = focusState.isFocused
        },
        label = { Text(label, fontSize = 9.sp, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        singleLine = true,
        textStyle = androidx.compose.ui.text.TextStyle(color = EditorColors.text, fontSize = 11.sp),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(onDone = { commit() }),
        colors = inspectorTextFieldColors(),
    )
}

@Composable
private fun SwitchProperty(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(34.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            color = EditorColors.text,
            fontSize = 10.sp,
            modifier = Modifier.weight(1f).clickable { onChange(!checked) },
        )
        Switch(checked = checked, onCheckedChange = onChange, modifier = Modifier.height(28.dp))
    }
}

@Composable
private fun ComponentAddButton(label: String, onClick: () -> Unit) {
    Surface(onClick = onClick, color = EditorColors.panelSelected, shape = RoundedCornerShape(8.dp)) {
        Text(label, Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 9.dp), color = EditorColors.accent, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun ComponentRemoveButton(label: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
        TextButton(onClick = onClick) {
            Text(label, color = EditorColors.red, fontSize = 9.sp)
        }
    }
}

@Composable
private fun InspectorSection(title: String, badge: String, content: @Composable () -> Unit) {
    Surface(color = EditorColors.panelRaised, shape = RoundedCornerShape(10.dp)) {
        Column(Modifier.fillMaxWidth().padding(9.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, color = EditorColors.text, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.weight(1f))
                MetaPill(badge)
            }
            content()
        }
    }
}

@Composable
private fun MetaPill(text: String) {
    Surface(color = Color(0xFF202D3B), shape = RoundedCornerShape(5.dp)) {
        Text(text, modifier = Modifier.padding(horizontal = 6.dp, vertical = 3.dp), color = EditorColors.muted, fontSize = 8.sp, letterSpacing = 0.3.sp)
    }
}

@Composable
private fun ChoiceChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Surface(onClick = onClick, color = if (selected) EditorColors.accentDark else EditorColors.panelSelected, shape = RoundedCornerShape(7.dp)) {
        Text(label, Modifier.padding(horizontal = 12.dp, vertical = 7.dp), color = if (selected) EditorColors.accent else EditorColors.text, fontSize = 10.sp)
    }
}

@Composable
private fun PanelIconButton(label: String, description: String, enabled: Boolean = true, danger: Boolean = false, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.size(28.dp),
        color = Color.Transparent,
        shape = RoundedCornerShape(6.dp),
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(label, color = if (!enabled) EditorColors.subtle else if (danger) EditorColors.red else EditorColors.muted, fontSize = 14.sp)
        }
    }
}

@Composable
private fun EmptyPanelMessage(title: String, message: String) {
    Column(
        Modifier.fillMaxSize().padding(18.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("◇", color = EditorColors.subtle, fontSize = 28.sp)
        Spacer(Modifier.height(8.dp))
        Text(title, color = EditorColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Text(message, color = EditorColors.muted, fontSize = 10.sp)
    }
}

@Composable
fun SpriteAssetDropdown(
    controller: EditorController,
    selectedAssetId: String?,
    onSelect: (String?) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    val selected = controller.document.assets.firstOrNull { it.id == selectedAssetId }
    Box {
        Surface(
            onClick = { expanded = true },
            color = EditorColors.panelSelected,
            shape = RoundedCornerShape(8.dp),
        ) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 9.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Texture", color = EditorColors.muted, fontSize = 10.sp)
                Spacer(Modifier.weight(1f))
                Text(selected?.name ?: "None", color = if (selected == null) EditorColors.subtle else EditorColors.text, fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text("  ⌄", color = EditorColors.accent, fontSize = 13.sp)
            }
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            DropdownMenuItem(text = { Text("No texture") }, onClick = { expanded = false; onSelect(null) })
            controller.document.assets.filter { it.kind == AssetKind.SPRITE }.forEach { asset ->
                DropdownMenuItem(text = { Text(asset.name, maxLines = 1, overflow = TextOverflow.Ellipsis) }, onClick = { expanded = false; onSelect(asset.id) })
            }
        }
    }
}

@Composable
private fun inspectorTextFieldColors() = TextFieldDefaults.colors(
    focusedContainerColor = EditorColors.panelSelected,
    unfocusedContainerColor = EditorColors.panelSelected,
    disabledContainerColor = EditorColors.panelSelected,
    focusedTextColor = EditorColors.text,
    unfocusedTextColor = EditorColors.text,
    focusedLabelColor = EditorColors.accent,
    unfocusedLabelColor = EditorColors.muted,
    focusedIndicatorColor = EditorColors.accent,
    unfocusedIndicatorColor = EditorColors.border,
    cursorColor = EditorColors.accent,
)

private fun objectGlyph(item: EditorObject): String = when (item.kind) {
    EditorObjectKind.CAMERA -> "▣"
    EditorObjectKind.MESH3D -> "⬡"
    EditorObjectKind.TILEMAP -> "▦"
    EditorObjectKind.AUDIO_SOURCE -> "♫"
    EditorObjectKind.SPRITE -> "▰"
    EditorObjectKind.EMPTY -> "◇"
}

private fun hierarchyDepth(controller: EditorController, item: EditorObject): Int {
    var depth = 0
    var parent = item.parentId
    val seen = mutableSetOf<String>()
    while (parent != null && seen.add(parent)) {
        depth++
        parent = controller.document.objects.firstOrNull { it.id == parent }?.parentId
    }
    return depth.coerceAtMost(4)
}

private fun hierarchyDescendants(controller: EditorController, id: String): Set<String> {
    val descendants = mutableSetOf<String>()
    var changed: Boolean
    do {
        changed = false
        controller.document.objects.forEach { item ->
            if (item.parentId == id || (item.parentId != null && item.parentId in descendants)) {
                if (descendants.add(item.id)) changed = true
            }
        }
    } while (changed)
    return descendants
}
