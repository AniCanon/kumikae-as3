package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import java.awt.Color
import java.awt.Dimension
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import java.awt.geom.AffineTransform
import javax.swing.JPanel

/** Paints the device around the latest picture, and turns mouse presses on its screen into gestures. */
internal class SimulatorScreen(private val gesture: (SimulatorFrame, Pixel, Pixel, Long) -> Unit) : JPanel() {
    var frame: SimulatorFrame? = null
        set(value) {
            field = value
            repaint()
        }

    /** Two lines shown on the dark screen while there is no picture: what is missing, and what to do. */
    var placeholder: Pair<String, String> = "" to ""
        set(value) {
            field = value
            repaint()
        }
    var inputEnabled = false
    private var pressed: Press? = null

    private class Press(val frame: SimulatorFrame, val at: Pixel, val nanos: Long)

    init {
        isOpaque = false
        minimumSize = Dimension(0, 0)
        addMouseListener(object : MouseAdapter() {
            override fun mousePressed(event: MouseEvent) {
                pressed = null
                if (!inputEnabled || event.button != MouseEvent.BUTTON1) return
                val current = frame ?: return
                pressed = Press(current, pixel(current, event) ?: return, System.nanoTime())
            }

            override fun mouseReleased(event: MouseEvent) {
                val start = pressed.also { pressed = null } ?: return
                val current = frame ?: return
                // A picture replaced during the drag may show another screen; its coordinates no longer apply.
                if (current.device != start.frame.device || current.generation != start.frame.generation) return
                gesture(
                    current,
                    start.at,
                    pixel(current, event) ?: return,
                    (System.nanoTime() - start.nanos) / NANOS_PER_MILLI,
                )
            }
        })
    }

    private fun device(screenWidth: Int, screenHeight: Int) =
        DeviceFrame(screenWidth, screenHeight, width, height, JBUI.scale(MARGIN))

    private fun pixel(on: SimulatorFrame, event: MouseEvent): Pixel? =
        device(on.image.width, on.image.height).pixel(event.x.toDouble(), event.y.toDouble())?.let { (x, y) ->
            Pixel(x, y)
        }

    override fun paintComponent(graphics: Graphics) {
        val canvas = graphics.create() as Graphics2D
        try {
            canvas.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
            canvas.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON)
            canvas.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BICUBIC)
            canvas.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY)
            val image = frame?.image
            val device = device(image?.width ?: EMPTY_WIDTH, image?.height ?: EMPTY_HEIGHT)
            device.paintBody(canvas)
            canvas.clip(device.screenShape)
            if (image == null) {
                paintPlaceholder(canvas, device)
            } else {
                val place = AffineTransform.getTranslateInstance(device.screen.x, device.screen.y)
                place.scale(device.scale, device.scale)
                canvas.drawImage(image, place, null)
            }
        } finally {
            canvas.dispose()
        }
    }

    private fun paintPlaceholder(canvas: Graphics2D, device: DeviceFrame) {
        val screen = device.screen
        canvas.color = Color(0x15161A)
        canvas.fill(screen)
        val (title, hint) = placeholder
        val middle = screen.y + screen.height / 2
        canvas.font = JBFont.label().asBold()
        canvas.color = Color(0xE6E7EB)
        drawCentred(canvas, title, screen.centerX, middle - canvas.fontMetrics.descent)
        canvas.font = JBFont.small()
        canvas.color = Color(0x9A9CA5)
        drawCentred(canvas, hint, screen.centerX, middle + canvas.fontMetrics.height)
    }

    private fun drawCentred(canvas: Graphics2D, text: String, centre: Double, baseline: Double) {
        canvas.drawString(text, (centre - canvas.fontMetrics.stringWidth(text) / 2.0).toFloat(), baseline.toFloat())
    }

    private companion object {
        const val NANOS_PER_MILLI = 1_000_000
        const val MARGIN = 18

        // The shape drawn while no picture has arrived: a current phone's screen.
        const val EMPTY_WIDTH = 1206
        const val EMPTY_HEIGHT = 2622
    }
}
