package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import com.intellij.icons.AllIcons
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.BasicStroke
import java.awt.Cursor
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.Rectangle
import java.awt.RenderingHints
import java.awt.geom.RoundRectangle2D
import javax.swing.JButton

@Composable
internal fun ProjectIconPreview(art: Artwork?, title: String, layout: SplashLayout?, choose: () -> Unit) {
    SwingNode(factory = { ArtworkButton(title, choose) }, update = {
        set(title) {
            toolTipText = it
            getAccessibleContext().accessibleName = it
        }
        set(choose) { onChoose = it }
        set(art) {
            artwork = it
            repaint()
        }
        set(layout) {
            splash = it
            preferredSize = if (it == null) JBUI.size(164, 204) else JBUI.size(336, 204)
            revalidate()
            repaint()
        }
    })
}

/** Native action semantics with a dedicated image canvas and a visible keyboard focus outline. */
internal class ArtworkButton(title: String, var onChoose: () -> Unit) : JButton(AllIcons.General.Add) {
    var artwork: Artwork? = null
    var splash: SplashLayout? = null
    init {
        isContentAreaFilled = false
        isBorderPainted = false
        isOpaque = false
        isRolloverEnabled = true
        cursor = Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
        toolTipText = title
        getAccessibleContext().accessibleName = title
        addActionListener { onChoose() }
    }

    override fun paintComponent(graphics: Graphics) {
        val g = graphics.create() as Graphics2D
        try {
            val pad = JBUI.scale(3)
            val areaW = width - pad * 2
            val areaH = height - pad * 2
            if (areaW <= 0 || areaH <= 0) return
            val layout = splash
            val fitted = fitRect(layout?.width?.toDouble() ?: 1.0, layout?.height?.toDouble() ?: 1.0, areaW, areaH)
            if (fitted.width <= 0 || fitted.height <= 0) return
            val w = fitted.width
            val h = fitted.height
            val x = pad
            val y = pad
            val radius = JBUI.scale(if (layout == null) 18 else 6).toDouble()
            val bounds = RoundRectangle2D.Double(x.toDouble(), y.toDouble(), w.toDouble(), h.toDouble(), radius, radius)
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
            if (layout != null && layout.background.alpha < 255) {
                g.color = JBColor.namedColor("Panel.background", JBColor.background())
                g.fill(bounds)
            }
            g.color = layout?.background ?: JBColor.namedColor("TextField.background", JBColor.background())
            g.fill(bounds)
            val originalClip = g.clip
            g.clip(bounds)
            val asset = artwork
            if (asset != null && layout?.visible != false) {
                asset.draw(g, Rectangle(x, y, w, h), layout)
            } else if (asset == null && layout?.visible != false) {
                icon.paintIcon(this, g, x + (w - icon.iconWidth) / 2, y + (h - icon.iconHeight) / 2)
            }
            g.clip = originalClip
            val active = isFocusOwner || model.isRollover
            g.color = if (active) {
                JBColor.namedColor("Component.focusColor", JBColor.BLUE)
            } else {
                JBColor.namedColor("Component.borderColor", JBColor.GRAY)
            }
            g.stroke = BasicStroke(JBUI.scale(if (active) 2 else 1).toFloat())
            g.draw(bounds)
        } finally {
            g.dispose()
        }
    }
}
