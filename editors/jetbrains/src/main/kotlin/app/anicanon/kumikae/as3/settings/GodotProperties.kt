package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.SurfaceGroup
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.FlowLayout
import javax.swing.SwingUtilities
import javax.swing.event.DocumentEvent
import javax.swing.event.DocumentListener

private sealed interface SelectedRow {
    data class Section(val name: String) : SelectedRow
    data class Property(val key: ConfigKey) : SelectedRow
}

internal fun isValidGodotProperty(section: String, name: String, rawValue: String): Boolean {
    val cleanSection = section.trim()
    val cleanName = name.trim()
    val cleanRaw = rawValue.trim()
    if (cleanName.isEmpty() || cleanRaw.isEmpty()) return false
    if (cleanSection.any { it == '\n' || it == '\r' || it == '[' || it == ']' }) return false
    if (cleanName.any { it == '\n' || it == '\r' || it == '=' || it == '[' || it == ']' }) return false
    if (cleanRaw.contains('\n') || cleanRaw.contains('\r')) return false
    return true
}

@Composable
internal fun GodotProperties(
    value: ProjectSettings,
    controller: SettingsController,
    modifier: SwingModifier = SwingModifier,
) {
    val config = remember(value.projectText) { GodotConfig(value.projectText) }
    val allSections = remember(config, value.propertyEdits) {
        (config.sections + value.propertyEdits.keys.map { it.section }).distinct()
    }
    var expanded by remember { mutableStateOf(setOf("application")) }
    var selected by remember { mutableStateOf<SelectedRow?>(null) }
    var editing by remember { mutableStateOf<ConfigKey?>(null) }
    var addingSection by remember { mutableStateOf<String?>(null) }

    fun sectionKeys(section: String): List<String> {
        val base = config.properties(section)
        val added = value.propertyEdits.keys.filter { it.section == section && it.name !in base }.map { it.name }
        return base + added
    }

    fun rawValue(section: String, name: String): String =
        value.propertyEdits[ConfigKey(section, name)] ?: config.raw(section, name).orEmpty()

    fun commit(key: ConfigKey, raw: String) {
        val trimmed = raw.trim()
        if (isValidGodotProperty(key.section, key.name, trimmed)) {
            controller.edit { it.copy(propertyEdits = it.propertyEdits + (key to trimmed)) }
        }
        editing = null
    }

    SurfaceGroup(modifier.emptyBorder(JBUI.insetsBottom(10)), padding = 0) {
        PlistHeaderRow {
            val target = when (val row = selected) {
                is SelectedRow.Section -> row.name
                is SelectedRow.Property -> row.key.section
                null -> expanded.firstOrNull() ?: "application"
            }
            addingSection = target
            expanded = expanded + target
        }
        if (addingSection != null) {
            Hairline()
            AddPropertyBar(
                section = addingSection.orEmpty(),
                onCancel = { addingSection = null },
                onAdd = { sec, name, raw ->
                    val cleanSection = sec.trim()
                    val cleanName = name.trim()
                    val cleanRaw = raw.trim()
                    if (isValidGodotProperty(cleanSection, cleanName, cleanRaw)) {
                        val key = ConfigKey(cleanSection, cleanName)
                        controller.edit { it.copy(propertyEdits = it.propertyEdits + (key to cleanRaw)) }
                        expanded = expanded + cleanSection
                        selected = SelectedRow.Property(key)
                        addingSection = null
                    }
                },
            )
        }
        var rowIndex = 0
        for (section in allSections) {
            val keys = sectionKeys(section)
            if (keys.isEmpty() && addingSection != section) continue
            val open = section in expanded
            val sectionSelected = selected == SelectedRow.Section(section)
            val stripe = rowIndex++ % 2 == 1
            key(section) {
                Hairline()
                PlistSectionRow(
                    title = section.ifBlank { "Project file" },
                    count = keys.size,
                    open = open,
                    selected = sectionSelected,
                    striped = stripe,
                    onSelect = {
                        selected = SelectedRow.Section(section)
                        editing = null
                    },
                    onToggle = {
                        expanded = if (open) expanded - section else expanded + section
                        selected = SelectedRow.Section(section)
                        editing = null
                    },
                    onAdd = {
                        addingSection = section
                        expanded = expanded + section
                    },
                )
            }
            if (open) {
                for (keyName in keys) {
                    val propKey = ConfigKey(section, keyName)
                    val raw = rawValue(section, keyName)
                    val propSelected = selected == SelectedRow.Property(propKey)
                    val isEditing = editing == propKey
                    val propStripe = rowIndex++ % 2 == 1
                    key(propKey) {
                        Hairline()
                        PlistPropertyRow(
                            name = keyName,
                            typeLabel = inferGodotType(raw),
                            rawValue = raw,
                            selected = propSelected,
                            striped = propStripe,
                            editing = isEditing,
                            onSelect = {
                                if (editing != propKey) editing = null
                                selected = SelectedRow.Property(propKey)
                            },
                            onStartEdit = {
                                selected = SelectedRow.Property(propKey)
                                editing = propKey
                            },
                            onCommit = { updated -> commit(propKey, updated) },
                            onCancel = { editing = null },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun AddPropertyBar(
    section: String,
    onCancel: () -> Unit,
    onAdd: (section: String, name: String, value: String) -> Unit,
) {
    var secText by remember(section) { mutableStateOf(section) }
    var keyText by remember { mutableStateOf("") }
    var valText by remember { mutableStateOf("\"\"") }
    val latestOnAdd by rememberUpdatedState(onAdd)
    val submit = { latestOnAdd(secText, keyText, valText) }
    val submitState by rememberUpdatedState(submit)

    Column(SwingModifier.emptyBorder(JBUI.insets(8, 12))) {
        Panel(PanelLayout.Grid(1, 3, JBUI.scale(8), 0)) {
            SyncedTextField(
                value = secText,
                placeholder = "Section (blank = root)",
                onChange = { secText = it },
                onSubmit = { submitState() },
            )
            SyncedTextField(
                value = keyText,
                placeholder = "key/name",
                focusOnAttach = true,
                onChange = { keyText = it },
                onSubmit = { submitState() },
            )
            SyncedTextField(
                value = valText,
                placeholder = "value",
                onChange = { valText = it },
                onSubmit = { submitState() },
            )
        }
        Panel(PanelLayout.Flow(FlowLayout.RIGHT, 6, 4)) {
            Button("Cancel", onCancel)
            Button("Add property", { submitState() })
        }
    }
}

@Composable
private fun SyncedTextField(
    value: String,
    placeholder: String,
    onChange: (String) -> Unit,
    onSubmit: () -> Unit,
    modifier: SwingModifier = SwingModifier,
    focusOnAttach: Boolean = false,
) {
    val latestChange by rememberUpdatedState(onChange)
    val latestSubmit by rememberUpdatedState(onSubmit)
    SwingNode(
        factory = {
            SyncedPropertyField(value, placeholder).apply {
                if (focusOnAttach) SwingUtilities.invokeLater { requestFocusInWindow() }
            }
        },
        modifier = modifier,
        update = {
            set(latestChange) { onValueChange = it }
            set(latestSubmit) { onEnter = it }
            set(placeholder) { emptyText.text = it }
            set(value) { syncValue(it) }
        },
    )
}

private class SyncedPropertyField(initial: String, placeholder: String) : JBTextField(initial) {
    var onValueChange: (String) -> Unit = {}
    var onEnter: () -> Unit = {}
    private var syncing = false

    init {
        emptyText.text = placeholder
        addActionListener { onEnter() }
        document.addDocumentListener(
            object : DocumentListener {
                override fun insertUpdate(e: DocumentEvent) = notifyChanged()
                override fun removeUpdate(e: DocumentEvent) = notifyChanged()
                override fun changedUpdate(e: DocumentEvent) = notifyChanged()
            },
        )
    }

    private fun notifyChanged() {
        if (!syncing) onValueChange(text)
    }

    fun syncValue(next: String) {
        if (text == next) return
        syncing = true
        try {
            text = next
        } finally {
            syncing = false
        }
    }
}
