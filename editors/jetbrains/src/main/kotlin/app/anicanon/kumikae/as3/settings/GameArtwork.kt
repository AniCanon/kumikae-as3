package app.anicanon.kumikae.as3.settings

import com.intellij.openapi.util.Key
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.ImageLoader
import com.intellij.util.ui.ImageUtil
import java.awt.RenderingHints
import java.awt.image.BufferedImage
import javax.imageio.ImageIO
import kotlin.math.roundToInt

/** Keep the source dimensions for the inspector and a bounded, unpadded raster for Retina previews. */
internal object GameArtwork {
    private data class Preview(val stamp: Long, val artwork: Artwork)
    private val cached = Key.create<Preview>("kumikae.gameArtwork")
    private val unfiltered = Key.create<Preview>("kumikae.gameArtwork.unfiltered")
    private const val EDGE = 960

    fun preview(root: VirtualFile, path: String, filter: Boolean = true): Artwork? {
        if (!path.startsWith("res://")) return null
        val file = root.findFileByRelativePath(path.removePrefix("res://")) ?: return null
        if (!GameResourceKind.Icon.accepts(file.name)) return null
        val stamp = file.modificationStamp
        val cache = if (filter) cached else unfiltered
        file.getUserData(cache)?.takeIf { it.stamp == stamp }?.let { return it.artwork }
        return runCatching {
            val source = if (file.extension.equals("svg", true)) {
                val vector = ImageLoader.loadFromUrl(file.toNioPath().toUri().toURL()) ?: return null
                Artwork(
                    ImageUtil.toBufferedImage(vector, false),
                    ImageUtil.getUserWidth(vector),
                    ImageUtil.getUserHeight(vector),
                )
            } else {
                file.inputStream.use { stream ->
                    ImageIO.createImageInputStream(stream).use { input ->
                        val reader = ImageIO.getImageReaders(input).asSequence().firstOrNull() ?: return null
                        try {
                            reader.input = input
                            val w = reader.getWidth(0)
                            val h = reader.getHeight(0)
                            val step = (maxOf(w, h) / EDGE).coerceAtLeast(1)
                            val params = reader.defaultReadParam.apply { setSourceSubsampling(step, step, 0, 0) }
                            Artwork(reader.read(0, params), w, h)
                        } finally {
                            reader.dispose()
                        }
                    }
                }
            }
            val result = bounded(source, filter)
            file.putUserData(cache, Preview(stamp, result))
            result
        }.getOrNull()
    }

    internal fun bounded(source: Artwork, filter: Boolean): Artwork {
        val image = source.image
        val scale = EDGE.toDouble() / maxOf(image.width, image.height)
        if (scale >= 1) return source
        val w = (image.width * scale).roundToInt().coerceAtLeast(1)
        val h = (image.height * scale).roundToInt().coerceAtLeast(1)
        val thumbnail = BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB)
        val g = thumbnail.createGraphics()
        try {
            val interpolation = if (filter) {
                RenderingHints.VALUE_INTERPOLATION_BILINEAR
            } else {
                RenderingHints.VALUE_INTERPOLATION_NEAREST_NEIGHBOR
            }
            g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, interpolation)
            g.drawImage(image, 0, 0, w, h, null)
        } finally {
            g.dispose()
        }
        return source.copy(image = thumbnail)
    }
}
