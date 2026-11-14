package org.kumikae.jetbrains

import org.kumikae.simulator.Geometry
import org.kumikae.simulator.Pixel
import java.awt.Color
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import java.awt.image.BufferedImage
import javax.swing.JPanel

data class ScreenFrame(val device: String, val generation: Long, val image: BufferedImage) {
    val geometry = Geometry(image.width, image.height)
}

class SimulatorScreen(private val gesture: (ScreenFrame, Pixel, Pixel, Long) -> Unit) : JPanel() {
    var frame: ScreenFrame? = null
        set(value) { field = value; repaint() }
    var inputEnabled = false
    private var down: Triple<ScreenFrame, Pixel, Long>? = null

    init {
        background = Color(30, 30, 30)
        toolTipText = "Click to tap. Drag and release to swipe. Input requires idb."
        addMouseListener(object : MouseAdapter() {
            override fun mousePressed(e: MouseEvent) {
                down = null
                if (!inputEnabled || e.button != MouseEvent.BUTTON1) return
                val current = frame ?: return
                val point = current.geometry.fromView(e.x.toDouble(), e.y.toDouble(), width, height) ?: return
                down = Triple(current, point, System.nanoTime())
            }

            override fun mouseReleased(e: MouseEvent) {
                val start = down.also { down = null } ?: return
                val current = frame ?: return
                if (current.device != start.first.device || current.generation != start.first.generation) return
                val end = current.geometry.fromView(e.x.toDouble(), e.y.toDouble(), width, height) ?: return
                gesture(current, start.second, end, (System.nanoTime() - start.third) / 1_000_000)
            }
        })
    }

    override fun paintComponent(graphics: Graphics) {
        super.paintComponent(graphics)
        val current = frame ?: return
        val scale = minOf(width.toDouble() / current.image.width, height.toDouble() / current.image.height)
        val w = current.image.width * scale
        val h = current.image.height * scale
        (graphics as Graphics2D).setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR)
        val transform = java.awt.geom.AffineTransform.getTranslateInstance((width - w) / 2, (height - h) / 2)
        transform.scale(scale, scale)
        graphics.drawImage(current.image, transform, null)
    }
}
