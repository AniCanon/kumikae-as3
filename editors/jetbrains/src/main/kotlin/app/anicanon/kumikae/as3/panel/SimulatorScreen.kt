package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel
import com.intellij.ui.JBColor
import java.awt.Color
import java.awt.Dimension
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import java.awt.geom.AffineTransform
import javax.swing.JPanel

/** Paints the latest picture, fitted without stretching, and turns mouse presses into gestures on that picture. */
internal class SimulatorScreen(private val gesture: (SimulatorFrame, Pixel, Pixel, Long) -> Unit) : JPanel() {
    var frame: SimulatorFrame? = null
        set(value) {
            field = value
            repaint()
        }
    var inputEnabled = false
    private var pressed: Press? = null

    private class Press(val frame: SimulatorFrame, val at: Pixel, val nanos: Long)

    init {
        background = JBColor(Color(0xE6E6E6), Color(0x1E1E1E))
        minimumSize = Dimension(0, 0)
        toolTipText = "Click to tap. Drag and release to swipe. Input requires idb."
        addMouseListener(object : MouseAdapter() {
            override fun mousePressed(event: MouseEvent) {
                pressed = null
                if (!inputEnabled || event.button != MouseEvent.BUTTON1) return
                val current = frame ?: return
                val at = current.geometry.fromView(event.x.toDouble(), event.y.toDouble(), width, height) ?: return
                pressed = Press(current, at, System.nanoTime())
            }

            override fun mouseReleased(event: MouseEvent) {
                val start = pressed.also { pressed = null } ?: return
                val current = frame ?: return
                // A picture replaced during the drag may show another screen; its coordinates no longer apply.
                if (current.device != start.frame.device || current.generation != start.frame.generation) return
                val end = current.geometry.fromView(event.x.toDouble(), event.y.toDouble(), width, height) ?: return
                gesture(current, start.at, end, (System.nanoTime() - start.nanos) / NANOS_PER_MILLI)
            }
        })
    }

    override fun paintComponent(graphics: Graphics) {
        super.paintComponent(graphics)
        val image = frame?.image ?: return
        val scale = minOf(width.toDouble() / image.width, height.toDouble() / image.height)
        val transform = AffineTransform.getTranslateInstance(
            (width - image.width * scale) / 2,
            (height - image.height * scale) / 2,
        )
        transform.scale(scale, scale)
        val canvas = graphics as Graphics2D
        canvas.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR)
        canvas.drawImage(image, transform, null)
    }

    private companion object {
        const val NANOS_PER_MILLI = 1_000_000
    }
}
