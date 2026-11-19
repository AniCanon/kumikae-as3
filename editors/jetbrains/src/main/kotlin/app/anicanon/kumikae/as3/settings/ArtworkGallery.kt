package app.anicanon.kumikae.as3.settings

import com.intellij.icons.AllIcons
import com.intellij.ui.components.JBList
import com.intellij.util.ui.JBUI
import java.awt.Component
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.Rectangle
import java.awt.event.MouseEvent
import javax.swing.DefaultListCellRenderer
import javax.swing.Icon
import javax.swing.JLabel
import javax.swing.JList
import javax.swing.ListSelectionModel
import javax.swing.SwingConstants

/** One renderer for the whole grid, including keyboard focus and theme selection colors. */
internal class ArtworkGallery : JBList<String>() {
    private val thumbnails = object : LinkedHashMap<String, Artwork?>(64, 0.75f, true) {
        override fun removeEldestEntry(eldest: Map.Entry<String, Artwork?>?) = size > 160
    }
    init {
        name = "Project images"
        getAccessibleContext().accessibleName = name
        layoutOrientation = HORIZONTAL_WRAP
        visibleRowCount = -1
        fixedCellWidth = JBUI.scale(138)
        fixedCellHeight = JBUI.scale(108)
        selectionMode = ListSelectionModel.SINGLE_SELECTION
        cellRenderer = object : DefaultListCellRenderer() {
            init {
                putClientProperty("html.disable", true)
            }
            override fun getListCellRendererComponent(
                list: JList<*>,
                value: Any?,
                index: Int,
                isSelected: Boolean,
                cellHasFocus: Boolean,
            ): Component {
                val path = value.toString()
                val label = super.getListCellRendererComponent(
                    list,
                    path.substringAfterLast('/'),
                    index,
                    isSelected,
                    cellHasFocus,
                ) as JLabel
                label.horizontalAlignment = SwingConstants.CENTER
                label.horizontalTextPosition = SwingConstants.CENTER
                label.verticalTextPosition = SwingConstants.BOTTOM
                label.iconTextGap = JBUI.scale(5)
                label.icon = TileIcon(thumbnails[path])
                return label
            }
        }
    }

    fun missingVisible(): List<String> {
        val first = firstVisibleIndex
        val last = minOf(lastVisibleIndex, model.size - 1)
        if (first < 0 || last < first) return emptyList()
        return (first..last).map(model::getElementAt).filterNot(thumbnails::containsKey)
    }

    fun showThumbnails(images: Map<String, Artwork?>) {
        thumbnails.putAll(images)
        repaint()
    }

    override fun getToolTipText(event: MouseEvent): String? {
        val index = locationToIndex(event.point)
        return if (index >= 0 && getCellBounds(index, index)?.contains(event.point) == true) {
            model.getElementAt(index)
        } else {
            null
        }
    }

    private class TileIcon(val art: Artwork?) : Icon {
        override fun getIconWidth() = JBUI.scale(114)
        override fun getIconHeight() = JBUI.scale(74)
        override fun paintIcon(c: Component, graphics: Graphics, x: Int, y: Int) {
            val g = graphics.create() as Graphics2D
            try {
                if (art != null) {
                    art.draw(g, Rectangle(x, y, iconWidth, iconHeight))
                } else {
                    val icon = AllIcons.FileTypes.Image
                    icon.paintIcon(c, g, x + (iconWidth - icon.iconWidth) / 2, y + (iconHeight - icon.iconHeight) / 2)
                }
            } finally {
                g.dispose()
            }
        }
    }
}
