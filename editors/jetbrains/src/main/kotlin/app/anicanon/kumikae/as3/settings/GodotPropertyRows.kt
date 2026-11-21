package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import com.intellij.icons.AllIcons
import com.intellij.ui.JBColor
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import org.jetbrains.compose.swing.components.Label
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

@Composable
internal fun PlistHeaderRow(onAdd: () -> Unit) {
    val addState by rememberUpdatedState(onAdd)
    SwingNode(
        factory = {
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
        },
        update = { set(addState) { currentAdd = it } },
    )
}

@Composable
internal fun PlistSectionRow(
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
    val addState by rememberUpdatedState(onAdd)
    val bg = rowBackground(selected, striped)
    val fg = if (selected) UIUtil.getTreeSelectionForeground(true) else UIUtil.getLabelForeground()
    val mutedFg = if (selected) UIUtil.getTreeSelectionForeground(true) else UIUtil.getContextHelpForeground()

    SwingNode(
        factory = { SectionRowPanel() },
        update = {
            set(toggleState) { currentToggle = it }
            set(selectState) { currentSelect = it }
            set(addState) { currentAdd = it }
            set(bg) {
                backgroundColor = it
                repaint()
            }
            set(open) {
                chevron.icon = if (it) AllIcons.General.ArrowDown else AllIcons.General.ArrowRight
            }
            set(title to fg) { (textValue, color) ->
                nameLabel.text = textValue
                nameLabel.foreground = color
            }
            set(count to mutedFg) { (n, color) ->
                typeLabel.foreground = color
                countLabel.text = "($n ${if (n == 1) "item" else "items"})"
                countLabel.foreground = color
            }
        },
    )
}

@Composable
internal fun PlistPropertyRow(
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
                        factory = { InlineEditField(rawValue) },
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
        factory = { PropertyRowPanel() },
        update = {
            set(selectState) { currentSelect = it }
            set(startEditState) { currentStartEdit = it }
            set(bg) {
                backgroundColor = it
                repaint()
            }
            set(Triple(name, typeLabel, rawValue)) { (k, t, v) ->
                keyLabel.text = k
                typeLbl.text = t
                valueLbl.text = v
            }
            set(fg to mutedFg) { (primary, secondary) ->
                keyLabel.foreground = primary
                typeLbl.foreground = secondary
                valueLbl.foreground = primary
            }
        },
    )
}

@Composable
internal fun Hairline(modifier: SwingModifier = SwingModifier) {
    SwingNode(
        factory = {
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
        },
        modifier = modifier,
    )
}

internal fun inferGodotType(raw: String): String = when {
    raw == "true" || raw == "false" -> "Boolean"
    raw.startsWith("\"") && raw.endsWith("\"") -> "String"
    raw.startsWith("Color(") -> "Color"
    raw.startsWith("Packed") || raw.startsWith("[") -> "Array"
    raw.startsWith("{") -> "Dictionary"
    raw.toLongOrNull() != null -> "Integer"
    raw.toDoubleOrNull() != null -> "Number"
    else -> "Value"
}

private open class PlistRowPanel : JPanel(BorderLayout()) {
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

private class SectionRowPanel : PlistRowPanel() {
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
    val typeLabel = JLabel("Section").apply { font = JBFont.label() }
    val countLabel = JLabel().apply { font = JBFont.label() }

    init {
        val left = JPanel(FlowLayout(FlowLayout.LEFT, JBUI.scale(6), 0)).apply {
            isOpaque = false
            add(chevron)
            add(nameLabel)
        }
        val right = JPanel(BorderLayout(JBUI.scale(6), 0)).apply { isOpaque = false }
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
    }
}

private class PropertyRowPanel : PlistRowPanel() {
    val keyLabel = JLabel().apply {
        font = JBFont.label()
        border = JBUI.Borders.emptyLeft(22)
    }
    val typeLbl = JLabel().apply { font = JBFont.label() }
    val valueLbl = JLabel().apply { font = JBFont.label() }

    init {
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
    }
}

private class InlineEditField(initial: String) : JBTextField(initial) {
    var currentCommit: ((String) -> Unit)? = null
    var currentCancel: (() -> Unit)? = null

    init {
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

private fun rowBackground(selected: Boolean, striped: Boolean): Color? = when {
    selected -> UIUtil.getTreeSelectionBackground(true)
    striped -> JBColor.namedColor("Table.stripeColor", JBColor(0xF7F8FA, 0x2F3136))
    else -> null
}
