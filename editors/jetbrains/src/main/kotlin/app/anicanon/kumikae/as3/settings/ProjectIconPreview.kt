package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import com.intellij.icons.AllIcons
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.Component
import java.awt.FlowLayout
import java.awt.Graphics
import java.awt.image.BufferedImage
import javax.swing.Icon
import javax.swing.JButton

@Composable
internal fun ProjectIconPreview(image: BufferedImage?, title: String, choose: () -> Unit) {
    Panel(PanelLayout.Flow(FlowLayout.LEFT, 0, 0)) {
        SwingNode(factory = { ArtworkButton(title, choose) }, update = {
            set(choose) { onChoose = it }
            set(image) { icon = it?.let(::ArtworkThumbnail) ?: AllIcons.General.Add }
        })
    }
}

internal class ArtworkButton(title: String, var onChoose: () -> Unit) : JButton(AllIcons.General.Add) {
    init {
        preferredSize = JBUI.size(96)
        margin = JBUI.emptyInsets()
        toolTipText = title
        getAccessibleContext().accessibleName = title
        addActionListener { onChoose() }
    }
}

private class ArtworkThumbnail(private val image: BufferedImage) : Icon {
    override fun getIconWidth() = JBUI.scale(80)
    override fun getIconHeight() = iconWidth
    override fun paintIcon(component: Component?, graphics: Graphics, x: Int, y: Int) {
        graphics.drawImage(image, x, y, iconWidth, iconHeight, component)
    }
}
