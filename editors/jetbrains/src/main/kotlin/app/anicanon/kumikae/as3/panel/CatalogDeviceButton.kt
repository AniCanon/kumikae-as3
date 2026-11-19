package app.anicanon.kumikae.as3.panel

import com.intellij.icons.AllIcons
import com.intellij.ui.ColorUtil
import com.intellij.ui.JBColor
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import java.awt.BasicStroke
import java.awt.Color
import java.awt.Cursor
import java.awt.Dimension
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import javax.swing.JButton
import javax.swing.plaf.basic.BasicGraphicsUtils

internal val catalogSecondary = JBColor.lazy {
    ColorUtil.mix(UIUtil.getPanelBackground(), UIUtil.getLabelForeground(), 0.73)
}

internal data class CatalogDevice(
    val title: String,
    val detail: String,
    val running: Boolean,
    val android: Boolean,
    val available: Boolean,
) {
    val action get() = if (running) "Open" else "Start"
}

/** One focusable action for the whole row, including its trailing Start or Open affordance. */
internal class CatalogDeviceButton : JButton() {
    var device: CatalogDevice? = null
        set(value) {
            field = value
            if (value == null) return
            isEnabled = value.available
            toolTipText = "${value.title} · ${value.detail}"
            getAccessibleContext().accessibleName = if (isEnabled) "${value.action} ${value.title}" else value.title
            getAccessibleContext().accessibleDescription = value.detail
            repaint()
        }
    var activate: () -> Unit = {}

    init {
        isOpaque = false
        isContentAreaFilled = false
        isBorderPainted = false
        isRolloverEnabled = true
        cursor = Cursor.getPredefinedCursor(Cursor.HAND_CURSOR)
        addActionListener { activate() }
    }

    override fun getPreferredSize(): Dimension {
        val textHeight = getFontMetrics(JBFont.label()).height + getFontMetrics(JBFont.small()).height
        return Dimension(0, maxOf(JBUI.scale(54), textHeight + JBUI.scale(16)))
    }

    override fun paintComponent(graphics: Graphics) {
        val row = device ?: return
        val g = graphics.create() as Graphics2D
        try {
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
            g.font = JBFont.small()
            val pad = JBUI.scale(10)
            if (model.isRollover || model.isPressed || hasFocus()) {
                g.color = JBColor.namedColor("List.hoverBackground", UIUtil.getListSelectionBackground(false))
                g.fillRoundRect(1, 1, width - 2, height - 2, pad, pad)
            }
            if (hasFocus()) {
                g.color = JBColor.namedColor("Component.focusColor", JBColor.BLUE)
                g.drawRoundRect(1, 1, width - 3, height - 3, pad, pad)
            }
            paintDevice(g, pad, row)
            val start = pad + JBUI.scale(42)
            val mark = if (row.running) AllIcons.General.ChevronRight else AllIcons.Actions.Execute
            val end = width - pad - mark.iconWidth
            val actionX = end - JBUI.scale(4) - g.fontMetrics.stringWidth(row.action)
            g.color = if (isEnabled) UIUtil.getLabelForeground() else UIUtil.getContextHelpForeground()
            if (isEnabled) {
                g.drawString(row.action, actionX, (height - g.fontMetrics.height) / 2 + g.fontMetrics.ascent)
                mark.paintIcon(this, g, end, (height - mark.iconHeight) / 2)
            }
            val available = ((if (isEnabled) actionX else width) - start - pad).coerceAtLeast(0)
            val small = g.fontMetrics
            g.font = JBFont.label().asBold()
            val large = g.fontMetrics
            val top = (height - large.height - small.height - JBUI.scale(2)) / 2
            g.drawString(
                BasicGraphicsUtils.getClippedString(this, large, row.title, available),
                start,
                top + large.ascent,
            )
            g.font = JBFont.small()
            g.color = catalogSecondary
            val baseline = top + large.height + JBUI.scale(2) + small.ascent
            g.drawString(BasicGraphicsUtils.getClippedString(this, small, row.detail, available), start, baseline)
        } finally {
            g.dispose()
        }
    }

    private fun paintDevice(g: Graphics2D, left: Int, row: CatalogDevice) {
        val icon = g.create() as Graphics2D
        try {
            val size = JBUI.scale(32)
            icon.translate(left, (height - size) / 2)
            icon.scale(size / 32.0, size / 32.0)
            icon.color = JBColor.namedColor("ActionButton.hoverBackground", JBColor(Color(0xE9EBEF), Color(0x34373C)))
            icon.fillRoundRect(0, 0, 32, 32, 9, 9)
            val x = if (row.title.contains("iPad", ignoreCase = true)) 7 else 10
            val w = 32 - 2 * x
            icon.stroke = BasicStroke(1.5f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND)
            icon.color = UIUtil.getLabelForeground()
            icon.drawRoundRect(x, 5, w, 22, 4, 4)
            if (row.android) icon.fillOval(15, 7, 2, 2) else icon.drawLine(14, 7, 18, 7)
            icon.drawLine(14, 24, 18, 24)
            if (row.running) {
                icon.color = UIUtil.getPanelBackground()
                icon.fillOval(22, 22, 10, 10)
                icon.color =
                    JBColor.namedColor("Component.successFocusColor", JBColor(Color(0x278647), Color(0x65B978)))
                icon.fillOval(24, 24, 6, 6)
            }
        } finally {
            icon.dispose()
        }
    }
}
