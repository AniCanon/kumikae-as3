package app.anicanon.kumikae.as3.settings

import java.awt.Graphics2D
import java.awt.Rectangle
import java.awt.RenderingHints
import java.awt.image.BufferedImage
import javax.swing.ImageIcon
import kotlin.math.roundToInt

internal fun fitRect(srcWidth: Double, srcHeight: Double, boxWidth: Int, boxHeight: Int): Rectangle {
    if (srcWidth <= 0 || srcHeight <= 0 || boxWidth <= 0 || boxHeight <= 0) return Rectangle(0, 0, 0, 0)
    val scale = minOf(boxWidth.toDouble() / srcWidth, boxHeight.toDouble() / srcHeight)
    val w = (srcWidth * scale).roundToInt().coerceIn(1, boxWidth)
    val h = (srcHeight * scale).roundToInt().coerceIn(1, boxHeight)
    return Rectangle((boxWidth - w) / 2, (boxHeight - h) / 2, w, h)
}

internal fun interpolationHint(filter: Boolean): Any = if (filter) {
    RenderingHints.VALUE_INTERPOLATION_BILINEAR
} else {
    RenderingHints.VALUE_INTERPOLATION_NEAREST_NEIGHBOR
}

internal fun formatArtworkMeta(path: String, art: Artwork): String {
    val ext = path.substringAfterLast('.', "").uppercase()
    return when {
        ext == "SVG" -> "SVG  ·  Vector"
        ext.isEmpty() -> "${art.width} × ${art.height} px"
        else -> "$ext  ·  ${art.width} × ${art.height} px"
    }
}

internal data class Artwork(val image: BufferedImage, val width: Int = image.width, val height: Int = image.height) {
    fun fit(boxWidth: Int, boxHeight: Int): Rectangle =
        fitRect(width.toDouble(), height.toDouble(), boxWidth, boxHeight)

    fun draw(g: Graphics2D, box: Rectangle, layout: SplashLayout? = null) {
        if (box.width <= 0 || box.height <= 0 || width <= 0 || height <= 0) return
        val dest = if (layout == null) {
            fit(box.width, box.height)
        } else if (layout.width <= 0 || layout.height <= 0) {
            return
        } else {
            val rect = layout.placement(width, height)
            val sx = box.width.toDouble() / layout.width
            val sy = box.height.toDouble() / layout.height
            Rectangle(
                (rect.x * sx).roundToInt(),
                (rect.y * sy).roundToInt(),
                (rect.width * sx).roundToInt().coerceAtLeast(1),
                (rect.height * sy).roundToInt().coerceAtLeast(1),
            )
        }
        if (dest.width <= 0 || dest.height <= 0) return
        g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, interpolationHint(layout?.filter != false))
        g.drawImage(image, box.x + dest.x, box.y + dest.y, dest.width, dest.height, null)
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
