package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import java.awt.FlowLayout
import java.awt.image.BufferedImage

@Composable
internal fun ArtworkField(
    kind: GameResourceKind,
    path: String,
    preview: BufferedImage?,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
    change: (String) -> Unit,
) {
    Column(SwingModifier.emptyBorder(JBUI.insets(8))) {
        Label(kind.title)
        ProjectIconPreview(preview, "Choose ${kind.title.lowercase()}") { choose(kind, change) }
        Label(if (path.isEmpty()) "Godot default" else path.substringAfterLast('/'))
        if (path.isNotEmpty() && preview == null) Label("Preview unavailable")
        Panel(PanelLayout.Flow(FlowLayout.LEFT, 4, 4)) {
            Button("Choose…", { choose(kind, change) })
            if (path.isNotEmpty()) Button("Reset", { change("") })
        }
    }
}
