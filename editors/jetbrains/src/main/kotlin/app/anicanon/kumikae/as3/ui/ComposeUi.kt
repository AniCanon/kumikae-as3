package app.anicanon.kumikae.as3.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import com.intellij.ui.components.JBLabel
import com.intellij.ui.components.JBScrollPane
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.GridBagConstraints
import java.awt.GridBagLayout
import javax.swing.Icon
import javax.swing.JComponent
import javax.swing.JPanel
import javax.swing.SwingConstants

/** Interop for controls that paint themselves or keep IDE behaviour; the caller owns the component. */
@Composable
internal fun Native(component: JComponent, modifier: SwingModifier = SwingModifier) {
    key(component) {
        SwingNode(factory = { component }, modifier = modifier)
    }
}

/** A flat button that is only its icon. The tooltip is also its name, by which a test finds it. */
@Composable
internal fun IconButton(
    icon: Icon,
    tooltip: String,
    onClick: (JComponent) -> Unit,
    modifier: SwingModifier = SwingModifier,
    enabled: Boolean = true,
    lit: Boolean = false,
) {
    SwingNode(factory = { IconButtonControl() }, modifier = modifier, update = {
        set(icon) { this.icon = it }
        set(tooltip) {
            toolTipText = it
            name = it
        }
        set(enabled) { isEnabled = it }
        set(lit) { this.lit = it }
        set(onClick) { this.onClick = { it(this) } }
    })
}

/** The heading that names what is shown and opens the list of what else could be. */
@Composable
internal fun TitleButton(
    title: String,
    subtitle: String,
    onClick: (JComponent) -> Unit,
    modifier: SwingModifier = SwingModifier,
    chevron: Boolean = true,
    icon: Icon? = null,
) {
    SwingNode(factory = { TitleButtonControl() }, modifier = modifier, update = {
        set(chevron) { this.chevron = it }
        set(icon) { this.icon = it }
        set(title) {
            this.title = it
        }
        set(subtitle) { this.subtitle = it }
        set(onClick) { this.onClick = { it(this) } }
    })
}

/** One tab of a strip. With `onClose` it carries a cross. */
@Composable
internal fun Tab(
    text: String,
    chosen: Boolean,
    onClick: () -> Unit,
    modifier: SwingModifier = SwingModifier,
    icon: Icon? = null,
    onClose: (() -> Unit)? = null,
) {
    SwingNode(factory = { TabControl() }, modifier = modifier, update = {
        set(text) {
            this.text = it
            name = it
        }
        set(icon) { this.icon = it }
        set(chosen) { this.chosen = it }
        set(onClick) { this.onClick = it }
        set(onClose) { this.onClose = it }
    })
}

/** Full-width rows stacked from the top. */
@Composable
internal fun Column(modifier: SwingModifier = SwingModifier, content: @Composable () -> Unit) {
    SwingNode(factory = {
        JPanel(object : GridBagLayout() {
            init {
                defaultConstraints = GridBagConstraints().apply {
                    gridx = 0
                    gridwidth = GridBagConstraints.REMAINDER
                    weightx = 1.0
                    fill = GridBagConstraints.HORIZONTAL
                    anchor = GridBagConstraints.NORTHWEST
                }
            }
        })
    }, modifier = modifier, content = content)
}

/** A rounded bar of controls that floats over the panel. */
@Composable
internal fun Pill(modifier: SwingModifier = SwingModifier, content: @Composable () -> Unit) {
    SwingNode(factory = { PillPanel() }, modifier = modifier, content = content)
}

@Composable
internal fun BarSeparator(modifier: SwingModifier = SwingModifier) {
    SwingNode(factory = { BarSeparatorControl() }, modifier = modifier)
}

/** One quiet line of text that is cut with an ellipsis, and shown whole in its tooltip. */
@Composable
internal fun StatusLine(text: String, modifier: SwingModifier = SwingModifier, trailing: Boolean = false) {
    SwingNode(factory = {
        JBLabel().apply {
            font = JBFont.small()
            foreground = UIUtil.getContextHelpForeground()
            border = JBUI.Borders.empty(2, 8, 4, 8)
            horizontalAlignment = if (trailing) SwingConstants.RIGHT else SwingConstants.LEFT
        }
    }, modifier = modifier, update = {
        set(text) {
            this.text = it
            toolTipText = it.ifEmpty { null }
        }
    })
}

/** Attach caller-owned controls only while applying the composition, never inside remember. */
@Composable
internal fun NativeScrollPane(component: JComponent, modifier: SwingModifier = SwingModifier, visible: Boolean = true) {
    SwingNode(
        factory = {
            JBScrollPane().apply {
                border =
                    JBUI.Borders.customLineTop(JBUI.CurrentTheme.ToolWindow.borderColor())
            }
        },
        modifier = modifier,
        update = {
            set(component) { setViewportView(it) }
            set(visible) { isVisible = it }
        },
    )
}
