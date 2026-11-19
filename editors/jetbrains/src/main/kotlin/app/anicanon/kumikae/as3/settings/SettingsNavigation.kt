package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import com.intellij.openapi.ui.ComboBox
import com.intellij.openapi.util.IconLoader
import com.intellij.ui.JBColor
import com.intellij.ui.SimpleListCellRenderer
import com.intellij.ui.components.JBList
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.BorderLayout
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import javax.swing.Icon
import javax.swing.JList
import javax.swing.JPanel
import javax.swing.ListSelectionModel

internal enum class SettingsPage(val title: String, asset: String) {
    General("General", "general"),
    Display("Display", "display"),
    Export("Build & Release", "release"),
    Properties("Properties", "properties"),
    ;

    val symbol: Icon = IconLoader.getIcon("/icons/settings/$asset.svg", SettingsPage::class.java)
}

@Composable
internal fun SettingsNavigation(page: String, onSelect: (String) -> Unit, modifier: SwingModifier = SwingModifier) {
    val select = rememberUpdatedState(onSelect)
    SwingNode(factory = {
        JPanel(BorderLayout()).apply {
            border =
                JBUI.Borders.compound(JBUI.Borders.customLineRight(JBColor.border()), JBUI.Borders.empty(8, 8, 0, 8))
            preferredSize = JBUI.size(170, 0)
        }
    }, modifier = modifier, content = {
        Panel(PanelLayout.Border()) {
            SwingNode(factory = {
                JBList(*SettingsPage.entries.toTypedArray()).apply {
                    name = "Project settings navigation"
                    accessibleContext.accessibleName = name
                    selectionMode = ListSelectionModel.SINGLE_SELECTION
                    isOpaque = false
                    fixedCellHeight = JBUI.scale(34)
                    cellRenderer = SettingsPageRenderer()
                    addListSelectionListener {
                        if (!it.valueIsAdjusting) selectedValue?.let { item -> select.value(item.name) }
                    }
                }
            }, modifier = SwingModifier.north(), update = {
                set(page) { setSelectedValue(SettingsPage.valueOf(it), true) }
            })
        }
    })
}

@Composable
internal fun CompactNavigation(page: String, onSelect: (String) -> Unit, modifier: SwingModifier) {
    val select = rememberUpdatedState(onSelect)
    SwingNode(factory = {
        ComboBox(SettingsPage.entries.toTypedArray()).apply {
            accessibleContext.accessibleName = "Project settings navigation"
            renderer = SettingsPageRenderer()
            addActionListener { (selectedItem as? SettingsPage)?.let { select.value(it.name) } }
        }
    }, modifier = modifier, update = { set(page) { selectedItem = SettingsPage.valueOf(it) } })
}

private class SettingsPageRenderer : SimpleListCellRenderer<SettingsPage>() {
    private var active = false
    override fun isOpaque() = false

    override fun customize(
        list: JList<out SettingsPage>,
        value: SettingsPage,
        index: Int,
        selected: Boolean,
        hasFocus: Boolean,
    ) {
        active = selected
        text = value.title
        icon = value.symbol
        iconTextGap = JBUI.scale(9)
        border = JBUI.Borders.empty(0, 10)
        font = if (selected) JBFont.label().asBold() else JBFont.label()
        if (selected) {
            background = UIUtil.getListSelectionBackground(list.hasFocus())
            foreground = UIUtil.getListSelectionForeground(list.hasFocus())
        }
    }

    override fun paintComponent(graphics: Graphics) {
        if (active) {
            val g = graphics.create() as Graphics2D
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
                g.color = background
                val gap = JBUI.scale(2)
                val arc = JBUI.scale(10)
                g.fillRoundRect(0, gap, width, height - 2 * gap, arc, arc)
                g.color = JBColor.namedColor("Component.focusColor", JBColor.BLUE)
                val marker = JBUI.scale(14)
                g.fillRoundRect(JBUI.scale(2), (height - marker) / 2, gap, marker, gap, gap)
            } finally {
                g.dispose()
            }
        }
        super.paintComponent(graphics)
    }
}
