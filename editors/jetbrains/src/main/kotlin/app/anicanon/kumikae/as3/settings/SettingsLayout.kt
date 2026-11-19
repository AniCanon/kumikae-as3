package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.PluginFiles
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import com.intellij.util.ui.WrapLayout
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.BorderLayout
import java.awt.Dimension
import java.awt.FlowLayout
import java.awt.Rectangle
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import javax.swing.JLabel
import javax.swing.JPanel
import javax.swing.JScrollPane
import javax.swing.Scrollable

@Composable
internal fun SettingsLayout(
    projectName: String,
    page: String,
    onSelect: (String) -> Unit,
    modifier: SwingModifier,
    footer: @Composable () -> Unit,
    content: @Composable (Boolean) -> Unit,
) {
    var compact by remember { mutableStateOf(false) }
    SwingNode(factory = {
        JPanel(BorderLayout()).apply {
            addComponentListener(object : ComponentAdapter() {
                override fun componentResized(event: ComponentEvent) {
                    compact = width < JBUI.scale(700)
                }
            })
        }
    }, modifier = modifier, content = {
        Panel(PanelLayout.Border()) {
            SwingNode(factory = {
                JPanel(BorderLayout(JBUI.scale(12), 0)).apply {
                    border =
                        JBUI.Borders.compound(
                            JBUI.Borders.customLineBottom(JBColor.border()),
                            JBUI.Borders.empty(12, 16),
                        )
                    add(
                        JLabel("Project settings", PluginFiles.mark, JLabel.LEADING).apply {
                            font = JBFont.h3().asBold()
                            iconTextGap = JBUI.scale(8)
                        },
                        BorderLayout.WEST,
                    )
                    add(
                        JLabel("", JLabel.LEADING).apply {
                            foreground = UIUtil.getContextHelpForeground()
                        },
                        BorderLayout.CENTER,
                    )
                }
            }, modifier = SwingModifier.north(), update = {
                set(projectName) {
                    (getComponent(1) as JLabel).apply {
                        text = if (it.isBlank()) "" else "›    $it"
                        toolTipText = it
                    }
                }
            })
            if (!compact) SettingsNavigation(page, onSelect, SwingModifier.west())
            Panel(PanelLayout.Border(), modifier = SwingModifier.center().emptyBorder(JBUI.insets(0, 16, 0, 8))) {
                if (compact) {
                    CompactNavigation(page, onSelect, SwingModifier.north().emptyBorder(JBUI.insets(8, 0)))
                } else {
                    Label(
                        SettingsPage.valueOf(page).title,
                        SwingModifier.north().font(JBFont.h2().asBold()).emptyBorder(JBUI.insets(12, 2, 8, 0)),
                    )
                }
                key(page) {
                    ScrollPane(
                        modifier = SwingModifier.center().emptyBorder(JBUI.emptyInsets()),
                        horizontalScrollbar = JScrollPane.HORIZONTAL_SCROLLBAR_NEVER,
                    ) {
                        SwingNode(factory = { SettingsForm() }, modifier = SwingModifier.viewport(), content = {
                            Panel(PanelLayout.Border()) {
                                Panel(
                                    PanelLayout.Border(),
                                    modifier = SwingModifier.north().emptyBorder(JBUI.insetsRight(8)),
                                ) {
                                    content(compact)
                                }
                            }
                        })
                    }
                }
            }
            SwingNode(factory = {
                JPanel(BorderLayout()).apply { border = JBUI.Borders.customLineTop(JBColor.border()) }
            }, modifier = SwingModifier.south(), content = footer)
        }
    })
}

private class SettingsForm :
    JPanel(BorderLayout()),
    Scrollable {
    override fun getScrollableTracksViewportWidth() = true
    override fun getScrollableTracksViewportHeight() = false
    override fun getPreferredScrollableViewportSize(): Dimension = preferredSize
    override fun getScrollableUnitIncrement(rect: Rectangle, orientation: Int, direction: Int) = JBUI.scale(24)
    override fun getScrollableBlockIncrement(rect: Rectangle, orientation: Int, direction: Int) = (
        rect.height -
            JBUI.scale(24)
        ).coerceAtLeast(1)
}

@Composable
internal fun SettingsActions(gap: Int = 4, content: @Composable () -> Unit) {
    SwingNode(factory = {
        JPanel(WrapLayout(FlowLayout.LEADING, JBUI.scale(gap), JBUI.scale(3))).apply {
            isOpaque =
                false
        }
    }, content = content)
}
