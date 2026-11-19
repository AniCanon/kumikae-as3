package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.SurfaceGroup
import com.intellij.icons.AllIcons
import com.intellij.ui.JBColor
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
import java.awt.Cursor
import java.awt.Dimension
import java.awt.Graphics
import javax.swing.JButton
import javax.swing.JComponent
import javax.swing.SwingConstants

@Composable
internal fun GodotProperties(source: String) {
    val config = remember(source) { GodotConfig(source) }
    var expanded by remember { mutableStateOf(setOf("application")) }
    for (section in config.sections) {
        val keys = config.properties(section)
        if (keys.isEmpty()) continue
        val open = section in expanded
        val title = section.ifBlank { "Project file" }
        SurfaceGroup(SwingModifier.emptyBorder(JBUI.insetsBottom(10)), padding = 0) {
            Panel(PanelLayout.Border(8, 0), modifier = SwingModifier.emptyBorder(JBUI.insets(6, 10))) {
                SectionDisclosure(title, open, SwingModifier.center()) {
                    expanded = if (open) expanded - section else expanded + section
                }
                Label(
                    "${keys.size}",
                    SwingModifier.east()
                        .font(JBFont.small())
                        .foreground(UIUtil.getContextHelpForeground())
                        .emptyBorder(JBUI.insetsRight(4)),
                )
            }
            if (open) {
                SectionDivider()
                Column(SwingModifier.emptyBorder(JBUI.insets(4, 12, 8, 12))) {
                    for ((index, key) in keys.withIndex()) {
                        val topPad = if (index == 0) 4 else 5
                        Panel(
                            PanelLayout.Grid(1, 2, JBUI.scale(16), 0),
                            modifier = SwingModifier.emptyBorder(JBUI.insets(topPad, 0, 5, 0)),
                        ) {
                            Label(key, SwingModifier.font(JBFont.label()))
                            Label(
                                config.raw(section, key).orEmpty(),
                                SwingModifier.font(JBFont.label()).foreground(UIUtil.getContextHelpForeground()),
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SectionDisclosure(title: String, open: Boolean, modifier: SwingModifier, toggle: () -> Unit) {
    SwingNode(
        modifier = modifier,
        factory = {
            JButton(title).apply {
                horizontalAlignment = SwingConstants.LEFT
                isContentAreaFilled = false
                isBorderPainted = false
                isFocusPainted = false
                isOpaque = false
                border = JBUI.Borders.empty(2, 2)
                font = JBFont.label().asBold()
                iconTextGap = JBUI.scale(6)
                cursor = Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
                addActionListener { toggle() }
            }
        },
        update = {
            set(title) { text = it }
            set(open) { icon = if (it) AllIcons.General.ArrowDown else AllIcons.General.ArrowRight }
        },
    )
}

@Composable
private fun SectionDivider() {
    SwingNode(factory = {
        object : JComponent() {
            init {
                val height = JBUI.scale(1)
                preferredSize = Dimension(0, height)
                minimumSize = Dimension(0, height)
                maximumSize = Dimension(Int.MAX_VALUE, height)
            }

            override fun paintComponent(g: Graphics) {
                g.color = JBColor.border()
                g.fillRect(0, 0, width, height)
            }
        }
    })
}
