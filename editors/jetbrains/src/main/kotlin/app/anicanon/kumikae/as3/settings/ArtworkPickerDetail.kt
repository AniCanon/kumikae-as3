package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.Graphics
import java.awt.Graphics2D
import javax.swing.JComponent
import javax.swing.JLabel

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
            StatusLine(formatArtworkMeta(path, art))
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
            if (!bold) foreground = UIUtil.getContextHelpForeground()
            putClientProperty("html.disable", true)
        }
    }, update = {
        set(value) {
            text = it
            toolTipText = it
        }
    })
}

private class Swatch : JComponent() {
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
            val margin = JBUI.scale(8)
            val areaW = width - margin
            val areaH = height - margin
            if (areaW <= 0 || areaH <= 0) return
            val srcW = layout?.width?.toDouble() ?: 1.0
            val srcH = layout?.height?.toDouble() ?: 1.0
            val fitted = fitRect(srcW, srcH, areaW, areaH)
            if (fitted.width <= 0 || fitted.height <= 0) return
            fitted.translate(margin / 2, margin / 2)
            g.clip(fitted)
            if (layout != null) {
                g.color = layout.background
                g.fill(fitted)
            }
            if (layout?.visible != false) art.draw(g, fitted, layout)
        } finally {
            g.dispose()
        }
    }
}
