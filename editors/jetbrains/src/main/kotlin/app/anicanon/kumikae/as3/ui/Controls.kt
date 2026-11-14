package app.anicanon.kumikae.as3.ui

import com.intellij.icons.AllIcons
import com.intellij.openapi.util.IconLoader
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import java.awt.Color
import java.awt.Cursor
import java.awt.Dimension
import java.awt.FlowLayout
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import javax.accessibility.AccessibleContext
import javax.accessibility.AccessibleRole
import javax.swing.Icon
import javax.swing.JComponent
import javax.swing.JPanel

private val hover = JBColor(Color(0, 0, 0, 22), Color(255, 255, 255, 26))
private val pressed = JBColor(Color(0, 0, 0, 40), Color(255, 255, 255, 46))

private fun Graphics.smooth(): Graphics2D = (this as Graphics2D).apply {
    setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
    setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON)
}

/** Paints the rounded highlight a flat control shows under the pointer, and reports clicks. */
internal abstract class FlatControl : JComponent() {
    var onClick: () -> Unit = {}
    private var over = false
    private var down = false

    init {
        cursor = Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
        addMouseListener(object : MouseAdapter() {
            override fun mouseEntered(event: MouseEvent) = mark(isOver = true, isDown = down)

            override fun mouseExited(event: MouseEvent) = mark(isOver = false, isDown = false)

            override fun mousePressed(event: MouseEvent) = mark(
                isOver = over,
                isDown =
                event.button == MouseEvent.BUTTON1,
            )

            override fun mouseReleased(event: MouseEvent) {
                val clicked = down && over && isEnabled && clicked(event.x)
                mark(isOver = over, isDown = false)
                if (clicked) onClick()
            }
        })
    }

    // A bare JComponent has none; a screen reader needs the role and, through the tooltip, the name.
    override fun getAccessibleContext(): AccessibleContext {
        if (accessibleContext == null) {
            accessibleContext = object : AccessibleJComponent() {
                override fun getAccessibleRole() = AccessibleRole.PUSH_BUTTON

                override fun getAccessibleName(): String? = toolTipText ?: name
            }
        }
        return accessibleContext
    }

    /** Whether a release at this x is a click on the control itself, not on a part with its own meaning. */
    protected open fun clicked(x: Int): Boolean = true

    /** What a click does, for a caller that has no pointer. */
    fun doClick() {
        if (isEnabled) onClick()
    }

    private fun mark(isOver: Boolean, isDown: Boolean) {
        over = isOver
        down = isDown
        repaint()
    }

    protected fun paintHighlight(canvas: Graphics2D) {
        if (!isEnabled || !(over || down)) return
        canvas.color = if (down) pressed else hover
        val arc = JBUI.scale(HIGHLIGHT_ARC)
        canvas.fillRoundRect(0, 0, width, height, arc, arc)
    }

    private companion object {
        const val HIGHLIGHT_ARC = 10
    }
}

/** A toolbar button that is only its icon. `name` is its tooltip, so that a test can find it. */
internal class IconButtonControl : FlatControl() {
    var icon: Icon = AllIcons.Actions.More
        set(value) {
            field = value
            repaint()
        }

    /** Lit while what the button shows or hides is showing. */
    var lit = false
        set(value) {
            field = value
            repaint()
        }

    init {
        val side = JBUI.scale(SIDE)
        preferredSize = Dimension(side, side)
    }

    override fun paintComponent(graphics: Graphics) {
        val canvas = graphics.smooth()
        if (lit && isEnabled) {
            canvas.color = hover
            val arc = JBUI.scale(LIT_ARC)
            canvas.fillRoundRect(0, 0, width, height, arc, arc)
        }
        paintHighlight(canvas)
        val shown = if (isEnabled) icon else IconLoader.getDisabledIcon(icon)
        shown.paintIcon(this, canvas, (width - shown.iconWidth) / 2, (height - shown.iconHeight) / 2)
    }

    private companion object {
        const val SIDE = 30
        const val LIT_ARC = 10
    }
}

/** A name over a smaller second line, with a chevron: the heading that opens a list of choices. */
internal class TitleButtonControl : FlatControl() {
    var title = ""
        set(value) {
            field = value
            revalidate()
            repaint()
        }
    var subtitle = ""
        set(value) {
            field = value
            revalidate()
            repaint()
        }

    var chevron = true
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

    private val titleFont get() = JBFont.label().asBold().biggerOn(1f)
    private val subtitleFont get() = JBFont.small()
    private val pad get() = JBUI.scale(PAD)

    private val lead get() = icon?.let { it.iconWidth + pad } ?: 0

    override fun getPreferredSize(): Dimension {
        val names = getFontMetrics(titleFont)
        val details = getFontMetrics(subtitleFont)
        val text = maxOf(names.stringWidth(title), details.stringWidth(subtitle))
        val trail = if (chevron) AllIcons.General.ChevronDown.iconWidth + pad else 0
        return Dimension(lead + text + trail + pad * 2, names.height + details.height + pad)
    }

    override fun paintComponent(graphics: Graphics) {
        val canvas = graphics.smooth()
        paintHighlight(canvas)
        val names = getFontMetrics(titleFont)
        val details = getFontMetrics(subtitleFont)
        val top = (height - names.height - details.height) / 2
        icon?.let { it.paintIcon(this, canvas, pad, (height - it.iconHeight) / 2) }
        val x = pad + lead
        canvas.font = titleFont
        canvas.color = UIUtil.getLabelForeground()
        canvas.drawString(title, x, top + names.ascent)
        canvas.font = subtitleFont
        canvas.color = UIUtil.getContextHelpForeground()
        canvas.drawString(subtitle, x, top + names.height + details.ascent)
        if (chevron) {
            val mark = AllIcons.General.ChevronDown
            mark.paintIcon(this, canvas, x + pad + names.stringWidth(title), top + (names.height - mark.iconHeight) / 2)
        }
    }

    private companion object {
        const val PAD = 8
    }
}

/** A rounded bar that floats a few controls over the panel's background. */
internal class PillPanel : JPanel(FlowLayout(FlowLayout.CENTER, JBUI.scale(2), 0)) {
    init {
        isOpaque = false
        border = JBUI.Borders.empty(4, 10)
    }

    override fun paintComponent(graphics: Graphics) {
        val canvas = graphics.smooth()
        val arc = height
        canvas.color = JBColor(Color(0xFFFFFF), Color(0x2B2D30))
        canvas.fillRoundRect(0, 0, width - 1, height - 1, arc, arc)
        canvas.color = JBColor(Color(0xD3D5DB), Color(0x43454A))
        canvas.drawRoundRect(0, 0, width - 1, height - 1, arc, arc)
    }
}

/** A short upright line between two groups of buttons in a bar. */
internal class BarSeparatorControl : JComponent() {
    init {
        preferredSize = Dimension(JBUI.scale(WIDTH), JBUI.scale(HEIGHT))
    }

    override fun paintComponent(graphics: Graphics) {
        graphics.color = JBColor(Color(0xD3D5DB), Color(0x43454A))
        graphics.fillRect(width / 2, 0, 1, height)
    }

    private companion object {
        const val WIDTH = 11
        const val HEIGHT = 18
    }
}
