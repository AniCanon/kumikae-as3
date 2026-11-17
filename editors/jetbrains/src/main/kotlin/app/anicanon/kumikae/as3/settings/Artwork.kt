package app.anicanon.kumikae.as3.settings

import java.awt.Rectangle
import java.awt.RenderingHints
import java.awt.image.BufferedImage
import javax.swing.ImageIcon
import kotlin.math.roundToInt

internal data class Artwork(val image: BufferedImage, val width: Int = image.width, val height: Int = image.height) {
    fun fit(boxWidth: Int, boxHeight: Int): Rectangle {
        val scale = minOf(boxWidth.toDouble() / width, boxHeight.toDouble() / height)
        val w = (width * scale).roundToInt().coerceAtLeast(1)
        val h = (height * scale).roundToInt().coerceAtLeast(1)
        return Rectangle((boxWidth - w) / 2, (boxHeight - h) / 2, w, h)
    }

    val treeIcon by lazy {
        val result = BufferedImage(16, 16, BufferedImage.TYPE_INT_ARGB)
        val dest = fit(result.width, result.height)
        val g = result.createGraphics()
        try {
            g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BICUBIC)
            g.drawImage(image, dest.x, dest.y, dest.width, dest.height, null)
        } finally {
            g.dispose()
        }
        ImageIcon(result)
    }
}
