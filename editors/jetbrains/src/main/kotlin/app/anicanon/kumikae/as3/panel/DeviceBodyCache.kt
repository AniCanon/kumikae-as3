package app.anicanon.kumikae.as3.panel

import java.awt.RenderingHints
import java.awt.image.BufferedImage
import kotlin.math.ceil

internal data class DeviceBodySize(
    val screenWidth: Int,
    val screenHeight: Int,
    val width: Int,
    val height: Int,
    val margin: Int,
    val scaleX: Double,
    val scaleY: Double,
)

/** Static decoration is rendered only when geometry or display density changes. EDT-owned. */
internal class DeviceBodyCache {
    private var previous: DeviceBodySize? = null
    private var cached: BufferedImage? = null

    fun image(size: DeviceBodySize): BufferedImage? {
        cached?.takeIf { previous == size }?.let { return it }
        val width = ceil(size.width * size.scaleX)
        val height = ceil(size.height * size.scaleY)
        // A large undocked view at maximum zoom must not allocate hundreds of MiB on the EDT.
        if (width <= 0 || height <= 0 || !width.isFinite() || !height.isFinite() || width * height > MAX_PIXELS) {
            previous = null
            cached = null
            return null
        }
        val image = BufferedImage(
            width.toInt(),
            height.toInt(),
            BufferedImage.TYPE_INT_ARGB_PRE,
        )
        val canvas = image.createGraphics()
        try {
            canvas.scale(size.scaleX, size.scaleY)
            canvas.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
            canvas.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY)
            DeviceFrame(size.screenWidth, size.screenHeight, size.width, size.height, size.margin).paintBody(canvas)
        } finally {
            canvas.dispose()
        }
        previous = size
        cached = image
        return image
    }

    private companion object {
        const val MAX_PIXELS = 4 * 1024 * 1024 // At most 16 MiB for the one retained decoration.
    }
}
