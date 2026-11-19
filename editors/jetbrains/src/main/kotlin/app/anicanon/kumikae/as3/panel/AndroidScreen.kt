package app.anicanon.kumikae.as3.panel

import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.Point
import java.awt.RenderingHints
import java.awt.event.FocusAdapter
import java.awt.event.FocusEvent
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import javax.swing.JComponent

internal data class AndroidGuestDisplay(val rotation: Int, val width: Int, val height: Int)

internal class AndroidViewport(width: Int, height: Int, val rotation: Int, areaWidth: Int, areaHeight: Int) {
    val logicalWidth = if (rotation % 2 == 0) width else height
    val logicalHeight = if (rotation % 2 == 0) height else width
    val scale = minOf(areaWidth.toDouble() / logicalWidth, areaHeight.toDouble() / logicalHeight)
    val x = (areaWidth - logicalWidth * scale) / 2
    val y = (areaHeight - logicalHeight * scale) / 2
    fun point(atX: Int, atY: Int, guest: AndroidGuestDisplay? = null): Point? {
        if (scale <= 0 || atX < x || atY < y) return null
        val px = (atX - x) / scale
        val py = (atY - y) / scale
        if (px >= logicalWidth || py >= logicalHeight) return null
        return Point(
            (px * (guest?.width ?: logicalWidth) / logicalWidth).toInt(),
            (py * (guest?.height ?: logicalHeight) / logicalHeight).toInt(),
        )
    }
}

internal class AndroidScreen(private val input: (String, Point) -> Unit) : JComponent() {
    var picture: AndroidPicture? = null
        set(value) {
            if (value == null) {
                cancelTouch()
                last = null
            }
            field = value
            repaint()
        }
    var guest: AndroidGuestDisplay? = null
        set(value) {
            if (field != value) {
                cancelTouch()
                field = value
                repaint()
            }
        }
    val rotation: Int get() = guest?.rotation ?: 0
    var presented: (Long) -> Unit = {}
    private var last: AndroidPicture? = null
    private var pressed: Point? = null
    fun cancelTouch() {
        pressed?.let { input("UP", it) }
        pressed = null
    }
    private fun viewport(frame: AndroidPicture) =
        AndroidViewport(frame.guestWidth, frame.guestHeight, rotation, width, height)
    init {
        val mouse = object : MouseAdapter() {
            private fun point(event: MouseEvent) =
                picture?.takeIf { guest != null }?.let { viewport(it).point(event.x, event.y, guest) }
            override fun mousePressed(event: MouseEvent) {
                if (event.button == MouseEvent.BUTTON1) {
                    cancelTouch()
                    point(event)?.let {
                        pressed = it
                        input("DOWN", it)
                    }
                }
            }
            override fun mouseDragged(event: MouseEvent) {
                if (pressed != null) {
                    point(event)?.let {
                        pressed = it
                        input("MOVE", it)
                    }
                }
            }
            override fun mouseReleased(event: MouseEvent) {
                if (event.button != MouseEvent.BUTTON1) return
                pressed?.let { input("UP", point(event) ?: it) }
                pressed = null
            }
        }
        addMouseListener(mouse)
        addMouseMotionListener(mouse)
        addFocusListener(
            object : FocusAdapter() {
                override fun focusLost(event: FocusEvent) = cancelTouch()
            },
        )
        addHierarchyListener { if (!isShowing) cancelTouch() }
    }
    override fun paintComponent(g: Graphics) {
        super.paintComponent(g)
        val frame = picture ?: return
        val v = viewport(frame)
        val canvas = g.create() as Graphics2D
        try {
            canvas.translate(v.x + v.logicalWidth * v.scale / 2, v.y + v.logicalHeight * v.scale / 2)
            canvas.rotate(-rotation * Math.PI / 2)
            canvas.scale(
                v.scale * frame.guestWidth / frame.image.width,
                v.scale * frame.guestHeight / frame.image.height,
            )
            canvas.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR)
            canvas.drawImage(frame.image, -frame.image.width / 2, -frame.image.height / 2, null)
            if (last !== frame) {
                last = frame
                presented(System.nanoTime())
            }
        } finally {
            canvas.dispose()
        }
    }
}
