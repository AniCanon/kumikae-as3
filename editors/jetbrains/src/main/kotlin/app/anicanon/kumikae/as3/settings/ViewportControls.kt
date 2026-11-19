package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import app.anicanon.kumikae.as3.ui.Tab
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

@Composable
internal fun ViewportControls(value: ProjectSettings, controller: SettingsController) {
    val width = value.width.toIntOrNull() ?: 0
    val height = value.height.toIntOrNull() ?: 0
    val sizes = listOf(960 to 540, 960 to 720, 720 to 720, 720 to 960, 540 to 960)
    Panel(PanelLayout.Flow(FlowLayout.LEFT, 4, 2)) {
        for ((w, h) in sizes) {
            val ratio = viewportRatio(w, h)
            Tab(
                ratio,
                width > 0 && height > 0 && viewportRatio(width, height) == ratio,
                { controller.edit { it.copy(width = w.toString(), height = h.toString()) } },
            )
        }
    }
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
    val onChange = rememberUpdatedState(change)
    SwingNode(
        factory = { DimensionSpinnerControl(size.coerceIn(1, 16384)) { onChange.value(it) } },
        update = {
            set(title) { accessibleContext.accessibleName = it }
            set(size) { syncValue(it) }
        },
    )
}

private class DimensionSpinnerControl(initial: Int, private val onChange: (Int) -> Unit) :
    JSpinner(SpinnerNumberModel(initial, 1, 16384, 1)) {
    private var syncing = false

    init {
        addChangeListener {
            if (!syncing) onChange(value as Int)
        }
    }

    fun syncValue(next: Int) {
        if (next !in 1..16384 || value == next) return
        syncing = true
        try {
            value = next
        } finally {
            syncing = false
        }
    }
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
            val fitted = fitRect(w.toDouble(), h.toDouble(), width - 2 * pad, height - 2 * pad)
            if (fitted.width <= 0 || fitted.height <= 0) return
            val x = pad + fitted.x
            val y = pad + fitted.y
            g.color = JBColor.namedColor("TextField.background", JBColor.background())
            g.fillRoundRect(x, y, fitted.width, fitted.height, pad, pad)
            g.color = JBColor.namedColor("Component.borderColor", JBColor.GRAY)
            g.drawRoundRect(x, y, fitted.width, fitted.height, pad, pad)
            val label = viewportRatio(w, h)
            val measured = g.fontMetrics.stringWidth(label)
            if (measured + pad < fitted.width && g.fontMetrics.height < fitted.height) {
                g.color = JBColor.foreground()
                g.drawString(label, (width - measured) / 2, (height + g.fontMetrics.ascent) / 2)
            }
        } finally {
            g.dispose()
        }
    }
}
