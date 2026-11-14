package app.anicanon.kumikae.as3.ui

import com.intellij.icons.AllIcons
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import java.awt.Color
import java.awt.Dimension
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import javax.swing.Icon

/** One tab of a strip: an icon, a name and, where it can be closed, a cross. The chosen one is outlined. */
internal class TabControl : FlatControl() {
    var text = ""
        set(value) {
            field = value
            revalidate()
            repaint()
        }
    var icon: Icon? = null
        set(value) {
            field = value
            revalidate()
            repaint()
        }
    var chosen = false
        set(value) {
            field = value
            repaint()
        }

    /** Set where the tab can be closed. A click on the cross closes; anywhere else chooses. */
    var onClose: (() -> Unit)? = null
        set(value) {
            field = value
            revalidate()
            repaint()
        }

    private val pad get() = JBUI.scale(PAD)
    private val cross get() = AllIcons.Actions.Close

    init {
        font = JBFont.label()
        addMouseListener(object : MouseAdapter() {
            override fun mouseClicked(event: MouseEvent) {
                if (onCross(event.x)) onClose?.invoke()
            }
        })
    }

    /** What a click on the cross does, for a caller that has no pointer. */
    fun doClose() {
        onClose?.invoke()
    }

    private fun onCross(x: Int) = onClose != null && x >= width - pad - cross.iconWidth

    override fun clicked(x: Int): Boolean = !onCross(x)

    override fun getPreferredSize(): Dimension {
        val metrics = getFontMetrics(font)
        val lead = icon?.let { it.iconWidth + pad / 2 } ?: 0
        val trail = if (onClose != null) cross.iconWidth + pad / 2 else 0
        return Dimension(pad * 2 + lead + metrics.stringWidth(text) + trail, metrics.height + pad)
    }

    override fun paintComponent(graphics: Graphics) {
        val canvas = graphics as Graphics2D
        canvas.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
        canvas.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON)
        val arc = JBUI.scale(ARC)
        if (chosen) {
            canvas.color = JBColor(Color(0xFFFFFF), Color(0x2B2D30))
            canvas.fillRoundRect(0, 0, width - 1, height - 1, arc, arc)
            canvas.color = JBColor(Color(0xC9CCD6), Color(0x4E5157))
            canvas.drawRoundRect(0, 0, width - 1, height - 1, arc, arc)
        } else {
            paintHighlight(canvas)
        }
        var x = pad
        icon?.let {
            it.paintIcon(this, canvas, x, (height - it.iconHeight) / 2)
            x += it.iconWidth + pad / 2
        }
        val metrics = getFontMetrics(font)
        canvas.font = font
        canvas.color = if (chosen) UIUtil.getLabelForeground() else UIUtil.getContextHelpForeground()
        canvas.drawString(text, x, (height - metrics.height) / 2 + metrics.ascent)
        if (onClose != null) {
            cross.paintIcon(this, canvas, width - pad - cross.iconWidth, (height - cross.iconHeight) / 2)
        }
    }

    private companion object {
        const val PAD = 10
        const val ARC = 10
    }
}
