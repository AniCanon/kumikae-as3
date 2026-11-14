package app.anicanon.kumikae.as3.panel

import java.awt.Color
import java.awt.GradientPaint
import java.awt.Graphics2D
import java.awt.geom.Rectangle2D
import java.awt.geom.RoundRectangle2D

/**
 * Where a device with a screen of the given pixel size sits inside a view, and how it is drawn around
 * that screen. The screen's own rectangle is what a pointer position is measured against.
 */
internal class DeviceFrame(screenWidth: Int, screenHeight: Int, viewWidth: Int, viewHeight: Int, margin: Int) {
    private val short = minOf(screenWidth, screenHeight).toDouble()
    private val phone = maxOf(screenWidth, screenHeight) / short > PHONE_ASPECT
    private val upright = screenHeight >= screenWidth
    private val bezel = short * if (phone) PHONE_BEZEL else TABLET_BEZEL

    /** View units for one pixel of the screen. Zero when the view has no room. */
    val scale: Double = minOf(
        (viewWidth - 2.0 * margin) / (screenWidth + 2 * bezel),
        (viewHeight - 2.0 * margin) / (screenHeight + 2 * bezel),
    ).coerceAtLeast(0.0)

    val screen: Rectangle2D = Rectangle2D.Double(
        (viewWidth - screenWidth * scale) / 2,
        (viewHeight - screenHeight * scale) / 2,
        screenWidth * scale,
        screenHeight * scale,
    )

    private val edge = bezel * scale
    private val corner = short * scale * if (phone) PHONE_CORNER else TABLET_CORNER

    val screenShape: RoundRectangle2D get() = rounded(screen, 0.0, corner)

    /** The screen pixel under a point of the view, or null outside the screen. */
    fun pixel(x: Double, y: Double): Pair<Int, Int>? {
        if (scale <= 0.0 || !screen.contains(x, y)) return null
        return ((x - screen.x) / scale).toInt() to ((y - screen.y) / scale).toInt()
    }

    /** Everything but the screen's picture: shadow, side buttons, the metal edge and the black body. */
    fun paintBody(canvas: Graphics2D) {
        if (scale <= 0.0) return
        val ring = maxOf(MIN_RING, short * scale * RING)
        for (step in SHADOW_STEPS downTo 1) {
            canvas.color = Color(0, 0, 0, SHADOW_ALPHA)
            val spread = edge + ring + step * SHADOW_SPREAD
            canvas.fill(rounded(screen, spread, corner + spread).apply { y += SHADOW_DROP })
        }
        if (phone) paintButtons(canvas, ring)
        val outer = rounded(screen, edge + ring, corner + edge + ring)
        canvas.paint = GradientPaint(
            0f,
            outer.y.toFloat(),
            Color(0xC4C6CC),
            0f,
            (outer.y + outer.height).toFloat(),
            Color(0x5B5D64),
        )
        canvas.fill(outer)
        canvas.color = Color(0x050506)
        canvas.fill(rounded(screen, edge, corner + edge))
    }

    private fun paintButtons(canvas: Graphics2D, ring: Double) {
        canvas.color = Color(0x6E7077)
        val long = if (upright) screen.height else screen.width
        val out = edge + ring * BUTTON_REACH
        val thick = ring * BUTTON_REACH
        for ((at, size, near) in BUTTONS) {
            val start = long * at
            val length = long * size
            val bar = if (upright) {
                val x = if (near) screen.x - out else screen.x + screen.width + out - thick
                RoundRectangle2D.Double(x, screen.y + start, thick, length, thick, thick)
            } else {
                val y = if (near) screen.y - out else screen.y + screen.height + out - thick
                RoundRectangle2D.Double(screen.x + start, y, length, thick, thick, thick)
            }
            canvas.fill(bar)
        }
    }

    private fun rounded(inner: Rectangle2D, grow: Double, radius: Double) = RoundRectangle2D.Double(
        inner.x - grow,
        inner.y - grow,
        inner.width + 2 * grow,
        inner.height + 2 * grow,
        2 * radius,
        2 * radius,
    )

    private data class SideButton(val at: Double, val size: Double, val near: Boolean)

    private companion object {
        // A phone's screen is about twice as tall as wide; a tablet's is closer to square.
        const val PHONE_ASPECT = 1.7
        const val PHONE_BEZEL = 0.035
        const val TABLET_BEZEL = 0.03
        const val PHONE_CORNER = 0.15
        const val TABLET_CORNER = 0.03
        const val RING = 0.006
        const val MIN_RING = 1.5
        const val SHADOW_STEPS = 6
        const val SHADOW_ALPHA = 9
        const val SHADOW_SPREAD = 1.6
        const val SHADOW_DROP = 4.0
        const val BUTTON_REACH = 1.8

        // Along the long side, as a share of it: the action button and two volume buttons, then power.
        val BUTTONS = listOf(
            SideButton(at = 0.17, size = 0.035, near = true),
            SideButton(at = 0.24, size = 0.07, near = true),
            SideButton(at = 0.33, size = 0.07, near = true),
            SideButton(at = 0.28, size = 0.11, near = false),
        )
    }
}
