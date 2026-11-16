package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.Dimension
import java.awt.Graphics
import java.awt.image.BufferedImage
import javax.swing.JComponent

/** Native image painting keeps the icon crisp while the IDE supplies the surrounding form. */
@Composable
internal fun ProjectIconPreview(image: BufferedImage?, modifier: SwingModifier = SwingModifier) {
    if (image == null) return
    SwingNode(factory = { ProjectIconControl() }, modifier = modifier, update = {
        set(image) {
            this.image = it
            repaint()
        }
    })
}

private class ProjectIconControl : JComponent() {
    var image: BufferedImage? = null
    init {
        preferredSize = Dimension(JBUI.scale(96), JBUI.scale(96))
    }
    override fun paintComponent(graphics: Graphics) {
        super.paintComponent(graphics)
        image?.let { graphics.drawImage(it, JBUI.scale(8), JBUI.scale(8), JBUI.scale(80), JBUI.scale(80), null) }
    }
}
