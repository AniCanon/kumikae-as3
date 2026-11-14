package app.anicanon.kumikae.as3.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import com.intellij.ui.components.JBLabel
import com.intellij.ui.components.JBScrollPane
import com.intellij.ui.components.JBTextArea
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.Dimension
import java.awt.GridBagConstraints
import java.awt.GridBagLayout
import javax.swing.JComponent
import javax.swing.JPanel

/** Interop for controls that paint themselves or keep IDE behaviour; the caller owns the component. */
@Composable
internal fun Native(component: JComponent, modifier: SwingModifier = SwingModifier) {
    key(component) {
        SwingNode(factory = { component }, modifier = modifier)
    }
}

/** Full-width rows stacked from the top. */
@Composable
internal fun Column(modifier: SwingModifier = SwingModifier, content: @Composable () -> Unit) {
    Panel(PanelLayout.Border(), modifier = modifier) {
        SwingNode(factory = {
            JPanel(object : GridBagLayout() {
                init {
                    defaultConstraints = GridBagConstraints().apply {
                        gridx = 0
                        gridwidth = GridBagConstraints.REMAINDER
                        weightx = 1.0
                        fill = GridBagConstraints.HORIZONTAL
                        anchor = GridBagConstraints.NORTHWEST
                        insets = JBUI.insetsBottom(4)
                    }
                }
            })
        }, modifier = SwingModifier.north(), content = content)
    }
}

/** Wrapping read-only text. A label would be cut off in a narrow tool window. */
@Composable
internal fun Note(text: String, modifier: SwingModifier = SwingModifier, secondary: Boolean = true) {
    SwingNode(factory = {
        JBTextArea(text).apply {
            isEditable = false
            isOpaque = false
            lineWrap = true
            wrapStyleWord = true
            columns = NOTE_COLUMNS
            font = if (secondary) JBFont.small() else JBFont.label()
            if (secondary) foreground = JBUI.CurrentTheme.ContextHelp.FOREGROUND
            border = JBUI.Borders.empty(2, 0)
        }
    }, modifier = modifier, update = { set(text) { this.text = it } })
}

/** A label, a control that takes the spare width, and an optional action at the end of the row. */
@Composable
internal fun FormRow(
    label: String,
    modifier: SwingModifier = SwingModifier,
    action: (@Composable () -> Unit)? = null,
    control: @Composable (SwingModifier) -> Unit,
) {
    Panel(PanelLayout.Border(JBUI.scale(6), 0), modifier = modifier) {
        SwingNode(factory = {
            JBLabel(label).apply { preferredSize = Dimension(JBUI.scale(LABEL_WIDTH), preferredSize.height) }
        }, modifier = SwingModifier.west(), update = { set(label) { text = it } })
        control(SwingModifier.center())
        if (action != null) {
            Panel(PanelLayout.Border(), modifier = SwingModifier.east()) { action() }
        }
    }
}

/** Text the user can select and copy but not change. */
@Composable
internal fun ReadOnlyField(text: String, modifier: SwingModifier = SwingModifier) {
    SwingNode(factory = {
        JBTextField(text).apply { isEditable = false }
    }, modifier = modifier, update = {
        set(text) {
            this.text = it
            caretPosition = 0
        }
    })
}

/** Attach caller-owned controls only while applying the composition, never inside remember. */
@Composable
internal fun NativeScrollPane(
    component: JComponent,
    modifier: SwingModifier = SwingModifier,
    preferredSize: Dimension? = null,
) {
    SwingNode(
        factory = { JBScrollPane() },
        modifier = modifier,
        update = {
            set(component) { setViewportView(it) }
            set(preferredSize) { this.preferredSize = it }
        },
    )
}

private const val NOTE_COLUMNS = 24
private const val LABEL_WIDTH = 52
