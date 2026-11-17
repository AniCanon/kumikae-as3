package app.anicanon.kumikae.as3.settings

import com.intellij.openapi.util.Key
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.ImageLoader
import com.intellij.util.ui.ImageUtil
import java.awt.RenderingHints
import java.awt.image.BufferedImage
import javax.imageio.ImageIO

/** Share previews across editor tabs and the project tree until Godot's image changes. */
internal object GameArtwork {
    private data class Preview(val stamp: Long, val image: BufferedImage)
    private val cached = Key.create<Preview>("kumikae.gameArtwork")

    fun preview(root: VirtualFile, path: String): BufferedImage? {
        if (!path.startsWith("res://")) return null
        val file = root.findFileByRelativePath(path.removePrefix("res://")) ?: return null
        if (!GameResourceKind.Icon.accepts(file.name)) return null
        val stamp = file.modificationStamp
        file.getUserData(cached)?.takeIf { it.stamp == stamp }?.let { return it.image }
        return runCatching {
            val edge = 160
            val image = if (file.extension.equals("svg", true)) {
                val vector = ImageLoader.loadFromUrl(file.toNioPath().toUri().toURL()) ?: return null
                ImageUtil.toBufferedImage(vector, false)
            } else {
                file.inputStream.use { stream ->
                    ImageIO.createImageInputStream(stream).use { source ->
                        val reader = ImageIO.getImageReaders(source).asSequence().firstOrNull() ?: return null
                        try {
                            reader.input = source
                            val step = (maxOf(reader.getWidth(0), reader.getHeight(0)) / edge).coerceAtLeast(1)
                            val params = reader.defaultReadParam.apply { setSourceSubsampling(step, step, 0, 0) }
                            reader.read(0, params)
                        } finally {
                            reader.dispose()
                        }
                    }
                }
            }
            val preview = BufferedImage(edge, edge, BufferedImage.TYPE_INT_ARGB)
            val graphics = preview.createGraphics()
            try {
                val scale = edge.toDouble() / maxOf(image.getWidth(null), image.getHeight(null)).coerceAtLeast(1)
                val width = (image.getWidth(null) * scale).toInt().coerceAtLeast(1)
                val height = (image.getHeight(null) * scale).toInt().coerceAtLeast(1)
                graphics.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR)
                graphics.drawImage(image, (edge - width) / 2, (edge - height) / 2, width, height, null)
            } finally {
                graphics.dispose()
            }
            file.putUserData(cached, Preview(stamp, preview))
            preview
        }.getOrNull()
    }
}
