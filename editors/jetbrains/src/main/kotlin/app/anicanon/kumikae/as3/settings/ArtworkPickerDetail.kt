package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.Rectangle
import javax.swing.JLabel
import kotlin.math.roundToInt

@Composable
internal fun ArtworkPickerDetail(
    path: String?,
    current: String,
    art: Artwork?,
    loading: Boolean,
    layout: SplashLayout?,
    selectable: Boolean,
    modifier: SwingModifier,
) {
    Column(modifier.emptyBorder(JBUI.insetsLeft(16))) {
        val heading = if (layout == null) "Icon preview" else "Launch preview"
        Label(heading, SwingModifier.font(JBFont.label().asBold()).emptyBorder(JBUI.insetsBottom(8)))
        SwingNode(factory = { Swatch() }, update = {
            set(art to layout) {
                content = it
                repaint()
            }
        })
        val name = path?.substringAfterLast('/').orEmpty()
        DetailLabel(name, true)
        val location = if (path != null && !path.startsWith("res://")) {
            "Import to assets/"
        } else {
            path?.removePrefix("res://")?.substringBeforeLast('/', "Project root").orEmpty()
        }
        DetailLabel(location, false)
        if (art != null && path != null) {
            val format = path.substringAfterLast('.').uppercase()
            StatusLine("$format  ·  ${art.width} × ${art.height} px")
            if (layout?.visible == false) StatusLine("Hidden at launch")
            if (path == current) StatusLine("Currently assigned")
        } else if (path != null) {
            StatusLine(
                when {
                    loading -> "Loading preview…"
                    selectable -> "This format has no IDE preview"
                    else -> "Could not read this image"
                },
            )
        }
    }
}

@Composable
private fun DetailLabel(value: String, bold: Boolean) {
    SwingNode(factory = {
        JLabel().apply {
            font = if (bold) JBFont.label().asBold() else JBFont.small()
            preferredSize = JBUI.size(260, if (bold) 26 else 18)
            if (!bold) foreground = com.intellij.util.ui.UIUtil.getContextHelpForeground()
            putClientProperty("html.disable", true)
        }
    }, update = {
        set(value) {
            text = it
            toolTipText = it
        }
    })
}

private class Swatch : JLabel() {
    var content: Pair<Artwork?, SplashLayout?> = null to null
    init {
        preferredSize = JBUI.size(260, 202)
        getAccessibleContext().accessibleName = "Selected artwork preview"
    }
    override fun paintComponent(graphics: Graphics) {
        val g = graphics.create() as Graphics2D
        try {
            g.color = JBColor.namedColor("TextField.background", JBColor.background())
            g.fillRect(0, 0, width, height)
            val (art, layout) = content
            if (art == null) return
            val ratio = layout?.let { it.width.toDouble() / it.height } ?: 1.0
            val margin = JBUI.scale(8)
            val w = minOf((width - margin).toDouble(), (height - margin) * ratio).roundToInt().coerceAtLeast(1)
            val h = (w / ratio).roundToInt().coerceAtLeast(1)
            val box = Rectangle((width - w) / 2, (height - h) / 2, w, h)
            g.clip(box)
            if (layout != null) {
                g.color = layout.background
                g.fill(box)
            }
            if (layout?.visible != false) art.draw(g, box, layout)
        } finally {
            g.dispose()
        }
    }
}
