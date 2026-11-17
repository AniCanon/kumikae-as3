package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.Dimension
import java.awt.FlowLayout
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import javax.swing.JComponent
import javax.swing.JSpinner
import javax.swing.SpinnerNumberModel
import kotlin.math.roundToInt

@Composable
internal fun ViewportControls(value: ProjectSettings, controller: SettingsController) {
    val width = value.width.toIntOrNull() ?: 0
    val height = value.height.toIntOrNull() ?: 0
    SwingNode(factory = { ViewportDiagram() }, update = {
        set(width to height) {
            extent = it
            repaint()
        }
    })
    Panel(PanelLayout.Flow(FlowLayout.LEFT, 8, 4)) {
        Label("Width")
        DimensionSpinner(width, "Viewport width") { size -> controller.edit { it.copy(width = size.toString()) } }
        Label("×")
        Label("Height")
        DimensionSpinner(height, "Viewport height") { size -> controller.edit { it.copy(height = size.toString()) } }
        Label("px")
    }
}

@Composable
private fun DimensionSpinner(size: Int, title: String, change: (Int) -> Unit) {
    SwingNode(factory = {
        JSpinner(SpinnerNumberModel(size.coerceIn(1, 16384), 1, 16384, 1)).apply {
            accessibleContext.accessibleName = title
            addChangeListener { change(value as Int) }
        }
    }, update = { set(size) { if (it in 1..16384 && value != it) value = it } })
}

private class ViewportDiagram : JComponent() {
    var extent = 0 to 0
    init {
        preferredSize = Dimension(JBUI.scale(300), JBUI.scale(118))
    }
    override fun paintComponent(graphics: Graphics) {
        val (w, h) = extent
        if (w < 1 || h < 1) return
        val g = graphics.create() as Graphics2D
        try {
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
            val pad = JBUI.scale(12)
            val scale = minOf((width - 2.0 * pad) / w, (height - 2.0 * pad) / h)
            if (scale <= 0) return
            val rw = (w * scale).roundToInt().coerceAtLeast(1)
            val rh = (h * scale).roundToInt().coerceAtLeast(1)
            val x = (width - rw) / 2
            val y = (height - rh) / 2
            g.color = JBColor.namedColor("TextField.background", JBColor.background())
            g.fillRoundRect(x, y, rw, rh, pad, pad)
            g.color = JBColor.namedColor("Component.borderColor", JBColor.GRAY)
            g.drawRoundRect(x, y, rw, rh, pad, pad)
            val label = viewportRatio(w, h)
            val measured = g.fontMetrics.stringWidth(label)
            if (measured + pad < rw && g.fontMetrics.height < rh) {
                g.color = JBColor.foreground()
                g.drawString(label, (width - measured) / 2, (height + g.fontMetrics.ascent) / 2)
            }
        } finally {
            g.dispose()
        }
    }
}
