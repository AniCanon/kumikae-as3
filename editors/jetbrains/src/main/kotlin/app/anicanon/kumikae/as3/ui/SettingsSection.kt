package app.anicanon.kumikae.as3.ui

import androidx.compose.runtime.Composable
import com.intellij.ui.JBColor
import com.intellij.ui.RoundedLineBorder
import com.intellij.ui.components.panels.BackgroundRoundedPanel
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import org.jetbrains.compose.swing.modifier.appearance.opaque
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.BorderLayout

@Composable
internal fun SurfaceGroup(modifier: SwingModifier = SwingModifier, padding: Int = 0, content: @Composable () -> Unit) {
    SwingNode(factory = {
        BackgroundRoundedPanel(JBUI.scale(10), BorderLayout()).apply {
            background = UIUtil.getListBackground()
            border = RoundedLineBorder(JBColor.border(), 10)
        }
    }, modifier = modifier, content = {
        Column(SwingModifier.opaque(false).emptyBorder(JBUI.insets(padding)), content)
    })
}

@Composable
internal fun SettingsSection(title: String, modifier: SwingModifier = SwingModifier, content: @Composable () -> Unit) {
    Column(modifier.emptyBorder(JBUI.insetsBottom(10))) {
        Label(title, SwingModifier.font(JBFont.label().asBold()).emptyBorder(JBUI.insets(4, 3, 6, 0)))
        SurfaceGroup(padding = 10, content = content)
    }
}
