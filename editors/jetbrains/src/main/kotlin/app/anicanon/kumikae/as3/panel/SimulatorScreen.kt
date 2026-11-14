package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import java.awt.Color
import java.awt.Dimension
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.Point
import java.awt.Rectangle
import java.awt.RenderingHints
import java.awt.event.FocusAdapter
import java.awt.event.FocusEvent
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import java.awt.event.MouseWheelEvent
import java.awt.geom.AffineTransform
import javax.swing.JPanel
import javax.swing.JViewport
import javax.swing.Scrollable
import javax.swing.SwingConstants
import javax.swing.SwingUtilities

/** Paints the device around the latest picture, and turns mouse presses on its screen into gestures. */
internal class SimulatorScreen(private val gesture: (SimulatorFrame, Pixel, Pixel, Long) -> Unit) :
    JPanel(),
    Scrollable {
    var frame: SimulatorFrame? = null
        set(value) {
            val previous = field
            if (previous === value) return
            if (value == null || previous?.device != value.device ||
                previous?.generation != value.generation
            ) {
                cancelTouch()
            }
            field = value
            if (previous != null && value != null && previous.geometry == value.geometry) {
                repaint(device(value.image.width, value.image.height).screen.bounds)
            } else {
                repaint()
            }
        }

    /** Two lines shown on the dark screen while there is no picture: what is missing, and what to do. */
    var placeholder: Pair<String, String> = "" to ""
        set(value) {
            if (field == value) return
            field = value
            repaint()
        }
    var inputEnabled = false
        set(value) {
            if (!value) cancelTouch()
            field = value
        }
    var continuousTouch = false
    var onTouch: (SimulatorFrame, String, Pixel) -> Unit = { _, _, _ -> }
    var onFramePresented: (SimulatorFrame, Long) -> Unit = { _, _ -> }
    private var presented: SimulatorFrame? = null
    private val bodyCache = DeviceBodyCache()

    /** The device's name: which models have a camera housing over the screen is known only by it. */
    var deviceName = ""
        set(value) {
            if (field == value) return
            field = value
            repaint()
        }

    /** 1 fits the device to the view. More than 1 makes it larger than the view, which then scrolls. */
    var zoom = 1.0
        set(value) {
            if (field == value) return
            // What was in the middle of the view stays there.
            val seen = visibleRect
            val middle = if (width > 0 && height > 0) seen.centerX / width to seen.centerY / height else null
            field = value
            revalidate()
            repaint()
            val view = parent as? JViewport ?: return
            if (middle != null) {
                SwingUtilities.invokeLater {
                    val x = (middle.first * width - view.extentSize.width / 2.0).toInt()
                    val y = (middle.second * height - view.extentSize.height / 2.0).toInt()
                    view.viewPosition = Point(
                        x.coerceIn(0, maxOf(0, width - view.extentSize.width)),
                        y.coerceIn(0, maxOf(0, height - view.extentSize.height)),
                    )
                }
            }
        }

    /** The wheel with Command or Control held asks for another zoom. */
    var onZoom: (Double) -> Unit = {}

    /** A click on the screen while input is off. */
    var onInputWanted: () -> Unit = {}
    private var pressed: Press? = null

    private class Press(val frame: SimulatorFrame, val at: Pixel, val nanos: Long, val live: Boolean) {
        var latest = at
    }

    fun cancelTouch() {
        val start = pressed.also { pressed = null } ?: return
        if (start.live) onTouch(start.frame, "up", start.latest)
    }

    init {
        isOpaque = false
        minimumSize = Dimension(0, 0)
        isFocusable = true
        addFocusListener(object : FocusAdapter() {
            override fun focusLost(event: FocusEvent) = cancelTouch()
        })
        addHierarchyListener { if (!isShowing) cancelTouch() }
        addMouseWheelListener(::wheel)
        addMouseMotionListener(object : MouseAdapter() {
            override fun mouseDragged(event: MouseEvent) {
                val start = pressed ?: return
                if (!start.live) return
                pixel(start.frame, event)?.let { start.latest = it }
                onTouch(start.frame, "move", start.latest)
            }
        })
        addMouseListener(object : MouseAdapter() {
            override fun mouseClicked(event: MouseEvent) {
                val current = frame ?: return
                if (!inputEnabled && event.button == MouseEvent.BUTTON1 &&
                    pixel(current, event) != null
                ) {
                    onInputWanted()
                }
            }

            override fun mousePressed(event: MouseEvent) {
                cancelTouch()
                if (!inputEnabled || event.button != MouseEvent.BUTTON1) return
                val current = frame ?: return
                val at = pixel(current, event) ?: return
                requestFocusInWindow()
                pressed = Press(current, at, System.nanoTime(), continuousTouch)
                if (continuousTouch) onTouch(current, "down", at)
            }

            override fun mouseReleased(event: MouseEvent) {
                val start = pressed.also { pressed = null } ?: return
                if (start.live) {
                    onTouch(start.frame, "up", pixel(start.frame, event) ?: start.latest)
                    return
                }
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

    private fun wheel(event: MouseWheelEvent) {
        if (event.isMetaDown || event.isControlDown) {
            val next = zoom * Math.pow(WHEEL_STEP, -event.preciseWheelRotation)
            onZoom(next.coerceIn(1.0, MAX_ZOOM))
            event.consume()
        } else {
            // Listening to the wheel takes it from the scroll pane; give back what is not a zoom.
            parent?.dispatchEvent(SwingUtilities.convertMouseEvent(this, event, parent))
        }
    }

    private fun device(screenWidth: Int, screenHeight: Int) = DeviceFrame(
        screenWidth,
        screenHeight,
        width,
        height,
        JBUI.scale(MARGIN),
        island = ISLAND.containsMatchIn(deviceName),
    )

    private fun pixel(on: SimulatorFrame, event: MouseEvent): Pixel? =
        device(on.image.width, on.image.height).pixel(event.x.toDouble(), event.y.toDouble())?.let { (x, y) ->
            Pixel(x, y)
        }

    override fun paintComponent(graphics: Graphics) {
        val canvas = graphics.create() as Graphics2D
        try {
            canvas.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
            canvas.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON)
            canvas.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR)
            canvas.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY)
            val image = frame?.image
            val device = device(image?.width ?: EMPTY_WIDTH, image?.height ?: EMPTY_HEIGHT)
            val body = bodyCache.image(
                DeviceBodySize(
                    image?.width ?: EMPTY_WIDTH,
                    image?.height ?: EMPTY_HEIGHT,
                    width,
                    height,
                    JBUI.scale(MARGIN),
                    canvas.transform.scaleX,
                    canvas.transform.scaleY,
                ),
            )
            if (body == null) {
                device.paintBody(canvas)
            } else {
                canvas.drawImage(body, 0, 0, width, height, null)
            }
            canvas.clip(device.screenShape)
            if (image == null) {
                paintPlaceholder(canvas, device)
            } else {
                val place = AffineTransform.getTranslateInstance(device.screen.x, device.screen.y)
                place.scale(device.scale, device.scale)
                canvas.drawImage(image, place, null)
                device.paintIsland(canvas)
                frame?.takeIf { it !== presented }?.let {
                    presented = it
                    onFramePresented(it, System.nanoTime())
                }
            }
        } finally {
            canvas.dispose()
        }
    }

    override fun getPreferredSize(): Dimension {
        val view = (parent as? JViewport)?.extentSize ?: return super.getPreferredSize()
        return Dimension((view.width * zoom).toInt(), (view.height * zoom).toInt())
    }

    override fun getPreferredScrollableViewportSize(): Dimension = super.getPreferredSize()

    override fun getScrollableTracksViewportWidth() = zoom <= 1.0

    override fun getScrollableTracksViewportHeight() = zoom <= 1.0

    override fun getScrollableUnitIncrement(visible: Rectangle, orientation: Int, direction: Int) =
        JBUI.scale(SCROLL_STEP)

    override fun getScrollableBlockIncrement(visible: Rectangle, orientation: Int, direction: Int) =
        if (orientation == SwingConstants.VERTICAL) visible.height else visible.width

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

    companion object {
        private const val NANOS_PER_MILLI = 1_000_000
        const val MAX_ZOOM = 4.0
        private const val WHEEL_STEP = 1.08
        private const val MARGIN = 18
        private const val SCROLL_STEP = 24

        // The models whose screenshots lack the camera housing drawn over them.
        val ISLAND = Regex("iPhone (14 Pro|1[5-9](?!e)|[2-9][0-9]|Air)")

        // The shape drawn while no picture has arrived: a current phone's screen.
        private const val EMPTY_WIDTH = 1206
        private const val EMPTY_HEIGHT = 2622
    }
}
