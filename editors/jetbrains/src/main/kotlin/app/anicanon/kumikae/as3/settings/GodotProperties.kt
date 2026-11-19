package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.SurfaceGroup
import com.intellij.icons.AllIcons
import com.intellij.ui.JBColor
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import org.jetbrains.compose.swing.modifier.appearance.foreground
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.BorderLayout
import java.awt.Color
import java.awt.Cursor
import java.awt.Dimension
import java.awt.FlowLayout
import java.awt.Graphics
import java.awt.event.KeyAdapter
import java.awt.event.KeyEvent
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import javax.swing.JComponent
import javax.swing.JLabel
import javax.swing.JPanel
import javax.swing.SwingUtilities
import javax.swing.event.DocumentEvent
import javax.swing.event.DocumentListener

private sealed interface SelectedRow {
    data class Section(val name: String) : SelectedRow
    data class Property(val key: ConfigKey) : SelectedRow
}

@Composable
internal fun GodotProperties(value: ProjectSettings, controller: SettingsController) {
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
        if (trimmed.isNotEmpty() && !trimmed.contains('\n') && !trimmed.contains('\r')) {
            controller.edit { it.copy(propertyEdits = it.propertyEdits + (key to trimmed)) }
        }
        editing = null
    }

    SurfaceGroup(SwingModifier.emptyBorder(JBUI.insetsBottom(10)), padding = 0) {
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
                    if (cleanName.isNotEmpty() && cleanRaw.isNotEmpty() && !cleanRaw.contains('\n')) {
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
            if (open) {
                for (keyName in keys) {
                    val key = ConfigKey(section, keyName)
                    val raw = rawValue(section, keyName)
                    val propSelected = selected == SelectedRow.Property(key)
                    val isEditing = editing == key
                    val propStripe = rowIndex++ % 2 == 1
                    Hairline()
                    PlistPropertyRow(
                        name = keyName,
                        typeLabel = inferGodotType(raw),
                        rawValue = raw,
                        selected = propSelected,
                        striped = propStripe,
                        editing = isEditing,
                        onSelect = {
                            if (editing != key) editing = null
                            selected = SelectedRow.Property(key)
                        },
                        onStartEdit = {
                            selected = SelectedRow.Property(key)
                            editing = key
                        },
                        onCommit = { updated -> commit(key, updated) },
                        onCancel = { editing = null },
                    )
                }
            }
        }
    }
}

@Composable
private fun PlistHeaderRow(onAdd: () -> Unit) {
    val addState by rememberUpdatedState(onAdd)
    SwingNode(factory = {
        PlistRowPanel().apply {
            val keyLbl = JLabel("Key").apply {
                font = JBFont.label().asBold()
                foreground = UIUtil.getContextHelpForeground()
            }
            val typeLbl = JLabel("Type").apply {
                font = JBFont.label().asBold()
                foreground = UIUtil.getContextHelpForeground()
            }
            val right = JPanel(BorderLayout(JBUI.scale(6), 0)).apply { isOpaque = false }
            val valLbl = JLabel("Value").apply {
                font = JBFont.label().asBold()
                foreground = UIUtil.getContextHelpForeground()
            }
            val addBtn = JLabel(AllIcons.General.Add).apply {
                toolTipText = "Add property"
                cursor = Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
                addMouseListener(object : MouseAdapter() {
                    override fun mousePressed(e: MouseEvent) {
                        e.consume()
                        currentAdd?.invoke()
                    }
                })
            }
            right.add(valLbl, BorderLayout.CENTER)
            right.add(addBtn, BorderLayout.EAST)
            layoutColumns(keyLbl, typeLbl, right)
        }
    }, update = {
        set(addState) { currentAdd = it }
    })
}

@Composable
private fun PlistSectionRow(
    title: String,
    count: Int,
    open: Boolean,
    selected: Boolean,
    striped: Boolean,
    onSelect: () -> Unit,
    onToggle: () -> Unit,
    onAdd: () -> Unit,
) {
    val toggleState by rememberUpdatedState(onToggle)
    val selectState by rememberUpdatedState(onSelect)
    val bg = rowBackground(selected, striped)
    val fg = if (selected) UIUtil.getTreeSelectionForeground(true) else UIUtil.getLabelForeground()
    val mutedFg = if (selected) UIUtil.getTreeSelectionForeground(true) else UIUtil.getContextHelpForeground()

    SwingNode(
        factory = {
            PlistRowPanel().apply {
                val left = JPanel(FlowLayout(FlowLayout.LEFT, JBUI.scale(6), 0)).apply {
                    isOpaque = false
                }
                val chevron = JLabel().apply {
                    cursor = Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
                    addMouseListener(object : MouseAdapter() {
                        override fun mousePressed(e: MouseEvent) {
                            e.consume()
                            currentToggle?.invoke()
                        }
                    })
                }
                val nameLabel = JLabel().apply { font = JBFont.label().asBold() }
                left.add(chevron)
                left.add(nameLabel)

                val typeLabel = JLabel("Section").apply { font = JBFont.label() }
                val right = JPanel(BorderLayout(JBUI.scale(6), 0)).apply { isOpaque = false }
                val countLabel = JLabel().apply { font = JBFont.label() }
                val addBtn = JLabel(AllIcons.General.Add).apply {
                    toolTipText = "Add property to section"
                    cursor = Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
                    addMouseListener(object : MouseAdapter() {
                        override fun mousePressed(e: MouseEvent) {
                            e.consume()
                            currentAdd?.invoke()
                        }
                    })
                }
                right.add(countLabel, BorderLayout.CENTER)
                right.add(addBtn, BorderLayout.EAST)

                layoutColumns(left, typeLabel, right)
                addMouseListener(object : MouseAdapter() {
                    override fun mousePressed(e: MouseEvent) {
                        if (e.clickCount >= 2) {
                            currentToggle?.invoke()
                        } else {
                            currentSelect?.invoke()
                        }
                    }
                })
                putClientProperty("chevron", chevron)
                putClientProperty("nameLabel", nameLabel)
                putClientProperty("typeLabel", typeLabel)
                putClientProperty("countLabel", countLabel)
            }
        },
        update = {
            set(toggleState) { currentToggle = it }
            set(selectState) { currentSelect = it }
            set(onAdd) { currentAdd = it }
            set(bg) {
                backgroundColor = it
                repaint()
            }
            set(open) {
                (getClientProperty("chevron") as JLabel).icon =
                    if (it) AllIcons.General.ArrowDown else AllIcons.General.ArrowRight
            }
            set(title to fg) { (textValue, color) ->
                (getClientProperty("nameLabel") as JLabel).apply {
                    text = textValue
                    foreground = color
                }
            }
            set(count to mutedFg) { (n, color) ->
                (getClientProperty("typeLabel") as JLabel).foreground = color
                (getClientProperty("countLabel") as JLabel).apply {
                    text = "($n ${if (n == 1) "item" else "items"})"
                    foreground = color
                }
            }
        },
    )
}

@Composable
private fun PlistPropertyRow(
    name: String,
    typeLabel: String,
    rawValue: String,
    selected: Boolean,
    striped: Boolean,
    editing: Boolean,
    onSelect: () -> Unit,
    onStartEdit: () -> Unit,
    onCommit: (String) -> Unit,
    onCancel: () -> Unit,
) {
    val selectState by rememberUpdatedState(onSelect)
    val startEditState by rememberUpdatedState(onStartEdit)
    val commitState by rememberUpdatedState(onCommit)
    val cancelState by rememberUpdatedState(onCancel)
    val bg = rowBackground(selected, striped)
    val fg = if (selected) UIUtil.getTreeSelectionForeground(true) else UIUtil.getLabelForeground()
    val mutedFg = if (selected) UIUtil.getTreeSelectionForeground(true) else UIUtil.getContextHelpForeground()

    if (editing) {
        Panel(
            PanelLayout.Border(8, 0),
            modifier = SwingModifier.emptyBorder(JBUI.insets(3, 12)),
        ) {
            RowColumns(
                keyContent = {
                    Label(name, SwingModifier.font(JBFont.label()).emptyBorder(JBUI.insetsLeft(22)))
                },
                typeContent = {
                    Label(typeLabel, SwingModifier.font(JBFont.label()).foreground(UIUtil.getContextHelpForeground()))
                },
                valueContent = {
                    SwingNode(
                        factory = {
                            JBTextField(rawValue).apply {
                                font = JBFont.label()
                                addActionListener { currentCommit?.invoke(text) }
                                addKeyListener(object : KeyAdapter() {
                                    override fun keyPressed(e: KeyEvent) {
                                        if (e.keyCode == KeyEvent.VK_ESCAPE) currentCancel?.invoke()
                                    }
                                })
                                SwingUtilities.invokeLater {
                                    requestFocusInWindow()
                                    selectAll()
                                }
                            }
                        },
                        update = {
                            set(commitState) { currentCommit = it }
                            set(cancelState) { currentCancel = it }
                        },
                    )
                },
            )
        }
        return
    }

    SwingNode(
        factory = {
            PlistRowPanel().apply {
                val keyLabel = JLabel().apply {
                    font = JBFont.label()
                    border = JBUI.Borders.emptyLeft(22)
                }
                val typeLbl = JLabel().apply { font = JBFont.label() }
                val valueLbl = JLabel().apply { font = JBFont.label() }
                layoutColumns(keyLabel, typeLbl, valueLbl)
                addMouseListener(object : MouseAdapter() {
                    override fun mousePressed(e: MouseEvent) {
                        if (e.clickCount >= 2) {
                            currentStartEdit?.invoke()
                        } else {
                            currentSelect?.invoke()
                        }
                    }
                })
                putClientProperty("keyLabel", keyLabel)
                putClientProperty("typeLbl", typeLbl)
                putClientProperty("valueLbl", valueLbl)
            }
        },
        update = {
            set(selectState) { currentSelect = it }
            set(startEditState) { currentStartEdit = it }
            set(bg) {
                backgroundColor = it
                repaint()
            }
            set(Triple(name, typeLabel, rawValue)) { (k, t, v) ->
                (getClientProperty("keyLabel") as JLabel).text = k
                (getClientProperty("typeLbl") as JLabel).text = t
                (getClientProperty("valueLbl") as JLabel).text = v
            }
            set(fg to mutedFg) { (primary, secondary) ->
                (getClientProperty("keyLabel") as JLabel).foreground = primary
                (getClientProperty("typeLbl") as JLabel).foreground = secondary
                (getClientProperty("valueLbl") as JLabel).foreground = primary
            }
        },
    )
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
    Column(SwingModifier.emptyBorder(JBUI.insets(8, 12))) {
        Panel(PanelLayout.Grid(1, 3, JBUI.scale(8), 0)) {
            SwingNode(factory = {
                JBTextField(secText).apply {
                    emptyText.text = "Section (blank = root)"
                    document.addDocumentListener(SimpleDocListener { secText = text })
                }
            })
            SwingNode(factory = {
                JBTextField(keyText).apply {
                    emptyText.text = "key/name"
                    document.addDocumentListener(SimpleDocListener { keyText = text })
                    SwingUtilities.invokeLater { requestFocusInWindow() }
                }
            })
            SwingNode(factory = {
                JBTextField(valText).apply {
                    emptyText.text = "value"
                    document.addDocumentListener(SimpleDocListener { valText = text })
                    addActionListener { onAdd(secText, keyText, valText) }
                }
            })
        }
        Panel(PanelLayout.Flow(FlowLayout.RIGHT, 6, 4)) {
            Button("Cancel", onCancel)
            Button("Add property", { onAdd(secText, keyText, valText) })
        }
    }
}

private var JBTextField.currentCommit: ((String) -> Unit)?
    get() = getClientProperty("commit") as? ((String) -> Unit)
    set(value) = putClientProperty("commit", value)

private var JBTextField.currentCancel: (() -> Unit)?
    get() = getClientProperty("cancel") as? (() -> Unit)
    set(value) = putClientProperty("cancel", value)

private class SimpleDocListener(private val changed: () -> Unit) : DocumentListener {
    override fun insertUpdate(e: DocumentEvent) = changed()
    override fun removeUpdate(e: DocumentEvent) = changed()
    override fun changedUpdate(e: DocumentEvent) = changed()
}

private class PlistRowPanel : JPanel(BorderLayout()) {
    var currentToggle: (() -> Unit)? = null
    var currentSelect: (() -> Unit)? = null
    var currentStartEdit: (() -> Unit)? = null
    var currentAdd: (() -> Unit)? = null
    var backgroundColor: Color? = null

    init {
        isOpaque = false
        border = JBUI.Borders.empty(5, 12)
    }

    fun layoutColumns(keyComp: JComponent, typeComp: JComponent, valueComp: JComponent) {
        val leftAndType = JPanel(BorderLayout(JBUI.scale(12), 0)).apply {
            isOpaque = false
            preferredSize = Dimension(JBUI.scale(320), 0)
            minimumSize = Dimension(JBUI.scale(240), 0)
            add(keyComp, BorderLayout.CENTER)
            typeComp.preferredSize = Dimension(JBUI.scale(90), 0)
            add(typeComp, BorderLayout.EAST)
        }
        add(leftAndType, BorderLayout.WEST)
        add(valueComp, BorderLayout.CENTER)
    }

    override fun paintComponent(g: Graphics) {
        val bg = backgroundColor
        if (bg != null) {
            g.color = bg
            g.fillRect(0, 0, width, height)
        }
        super.paintComponent(g)
    }
}

@Composable
private fun RowColumns(
    keyContent: @Composable () -> Unit,
    typeContent: @Composable () -> Unit,
    valueContent: @Composable () -> Unit,
) {
    Panel(PanelLayout.Border(12, 0)) {
        Panel(PanelLayout.Border(12, 0), modifier = SwingModifier.west()) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.center()) { keyContent() }
            Panel(PanelLayout.Border(), modifier = SwingModifier.east()) { typeContent() }
        }
        Panel(PanelLayout.Border(), modifier = SwingModifier.center()) { valueContent() }
    }
}

@Composable
private fun Hairline() {
    SwingNode(factory = {
        object : JComponent() {
            init {
                val h = JBUI.scale(1)
                preferredSize = Dimension(0, h)
                minimumSize = Dimension(0, h)
                maximumSize = Dimension(Int.MAX_VALUE, h)
            }

            override fun paintComponent(g: Graphics) {
                g.color = JBColor.border()
                g.fillRect(0, 0, width, height)
            }
        }
    })
}

private fun rowBackground(selected: Boolean, striped: Boolean): Color? = when {
    selected -> UIUtil.getTreeSelectionBackground(true)
    striped -> JBColor.namedColor("Table.stripeColor", JBColor(0xF7F8FA, 0x2F3136))
    else -> null
}

private fun inferGodotType(raw: String): String = when {
    raw == "true" || raw == "false" -> "Boolean"
    raw.startsWith("\"") && raw.endsWith("\"") -> "String"
    raw.startsWith("Color(") -> "Color"
    raw.startsWith("Packed") || raw.startsWith("[") -> "Array"
    raw.startsWith("{") -> "Dictionary"
    raw.toLongOrNull() != null -> "Integer"
    raw.toDoubleOrNull() != null -> "Number"
    else -> "Value"
}
