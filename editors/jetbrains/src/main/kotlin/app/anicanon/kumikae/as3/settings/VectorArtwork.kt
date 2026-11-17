package app.anicanon.kumikae.as3.settings

import com.intellij.openapi.util.IconLoader
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.ui.scale.ScaleContext
import com.intellij.ui.scale.ScaleType
import com.intellij.util.ui.ImageUtil
import java.io.InputStream
import javax.xml.stream.XMLInputFactory
import javax.xml.stream.XMLStreamConstants
import kotlin.math.roundToInt

internal object VectorArtwork {
    private val length = Regex("""([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)(px|pt|pc|mm|cm|in)?""")
    private fun pixels(text: String?): Double? {
        val match = length.matchEntire(text?.trim().orEmpty()) ?: return null
        val unit = when (match.groupValues[2]) {
            "in" -> 96.0
            "cm" -> 96.0 / 2.54
            "mm" -> 96.0 / 25.4
            "pt" -> 96.0 / 72
            "pc" -> 16.0
            else -> 1.0
        }
        return (match.groupValues[1].toDouble() * unit).takeIf { it.isFinite() && it > 0 }
    }

    fun size(input: InputStream): Pair<Int, Int> {
        val xml = XMLInputFactory.newDefaultFactory().apply {
            setProperty(XMLInputFactory.SUPPORT_DTD, false)
            setProperty(XMLInputFactory.IS_SUPPORTING_EXTERNAL_ENTITIES, false)
        }.createXMLStreamReader(input)
        try {
            while (xml.hasNext() && xml.eventType != XMLStreamConstants.START_ELEMENT) xml.next()
            require(xml.eventType == XMLStreamConstants.START_ELEMENT && xml.localName == "svg")
            fun attr(key: String) = xml.getAttributeValue(null, key)
            val box = attr("viewBox")?.trim()?.split(Regex("[,\\s]+"))
            val vw = box?.getOrNull(2)?.let(::pixels)
            val vh = box?.getOrNull(3)?.let(::pixels)
            val ratio = if (vw != null && vh != null) vw / vh else null
            val w = pixels(attr("width"))
            val h = pixels(attr("height"))
            val width = w ?: if (h != null && ratio != null) h * ratio else vw ?: 300.0
            val height = h ?: if (w != null && ratio != null) w / ratio else vh ?: 150.0
            return width.roundToInt().coerceAtLeast(1) to height.roundToInt().coerceAtLeast(1)
        } finally {
            xml.close()
        }
    }

    fun decode(file: VirtualFile, edge: Int): Artwork {
        val (w, h) = file.inputStream.use(::size)
        val scale = minOf(1.0, edge.toDouble() / maxOf(w, h))
        val icon = requireNotNull(IconLoader.findIcon(file.toNioPath().toUri().toURL(), false))
        val context = ScaleContext.createIdentity().copyWithScale(ScaleType.OBJ_SCALE.of(scale))
        val image = requireNotNull(IconLoader.toImage(icon, context))
        return Artwork(ImageUtil.toBufferedImage(image, false), w, h)
    }
}
