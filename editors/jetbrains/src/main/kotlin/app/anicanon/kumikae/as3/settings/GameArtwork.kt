package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.scene.resolveProjectResource
import com.intellij.openapi.progress.ProcessCanceledException
import com.intellij.openapi.util.Key
import com.intellij.openapi.vfs.VirtualFile
import java.awt.RenderingHints
import java.awt.image.BufferedImage
import java.lang.ref.SoftReference
import javax.imageio.ImageIO
import kotlin.math.roundToInt

/** Keep the source dimensions for the inspector and a bounded, unpadded raster for Retina previews. */
internal object GameArtwork {
    private data class Preview(val stamp: Long, val artwork: Artwork)
    private val cached = Key.create<SoftReference<Preview>>("kumikae.gameArtwork")
    private val unfiltered = Key.create<SoftReference<Preview>>("kumikae.gameArtwork.unfiltered")
    private const val EDGE = 960

    fun preview(root: VirtualFile, path: String, filter: Boolean = true): Artwork? {
        val file = resolveProjectResource(root, path) ?: return null
        if (!GameResourceKind.Icon.accepts(file.name)) return null
        val stamp = file.modificationStamp
        val cache = if (filter) cached else unfiltered
        file.getUserData(cache)?.get()?.takeIf { it.stamp == stamp }?.let { return it.artwork }
        return decode(file, EDGE, filter)?.also { file.putUserData(cache, SoftReference(Preview(stamp, it))) }
    }

    fun decode(file: VirtualFile, edge: Int, filter: Boolean): Artwork? {
        if (edge <= 0) return null
        return try {
            val source = if (file.extension.equals("svg", true)) {
                VectorArtwork.decode(file, edge)
            } else {
                file.inputStream.use { stream ->
                    val input = ImageIO.createImageInputStream(stream) ?: return null
                    input.use {
                        val reader = ImageIO.getImageReaders(it).asSequence().firstOrNull() ?: return null
                        try {
                            reader.input = it
                            val w = reader.getWidth(0)
                            val h = reader.getHeight(0)
                            if (w <= 0 || h <= 0) return null
                            val step = (maxOf(w, h) / edge).coerceAtLeast(1)
                            val params = reader.defaultReadParam.apply { setSourceSubsampling(step, step, 0, 0) }
                            val image = reader.read(0, params) ?: return null
                            Artwork(image, w, h)
                        } finally {
                            reader.dispose()
                        }
                    }
                }
            }
            bounded(source, filter, edge)
        } catch (canceled: ProcessCanceledException) {
            throw canceled
        } catch (_: Exception) {
            null
        }
    }

    internal fun bounded(source: Artwork, filter: Boolean, edge: Int = EDGE): Artwork {
        val image = source.image
        if (edge <= 0 || image.width <= 0 || image.height <= 0) return source
        val scale = edge.toDouble() / maxOf(image.width, image.height)
        if (scale >= 1) return source
        val w = (image.width * scale).roundToInt().coerceAtLeast(1)
        val h = (image.height * scale).roundToInt().coerceAtLeast(1)
        val thumbnail = BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB)
        val g = thumbnail.createGraphics()
        try {
            g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, interpolationHint(filter))
            g.drawImage(image, 0, 0, w, h, null)
        } finally {
            g.dispose()
        }
        return source.copy(image = thumbnail)
    }
}
