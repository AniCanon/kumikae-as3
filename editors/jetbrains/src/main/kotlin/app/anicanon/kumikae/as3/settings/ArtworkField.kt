package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font

@Composable
internal fun ArtworkField(
    kind: GameResourceKind,
    path: String,
    preview: Artwork?,
    choose: (GameResourceKind, (String) -> Unit) -> Unit,
    layout: SplashLayout? = null,
    change: (String) -> Unit,
) {
    Column(SwingModifier.emptyBorder(JBUI.insets(4))) {
        Label(kind.title, SwingModifier.font(JBFont.label().asBold()))
        ProjectIconPreview(preview, "Choose ${kind.title.lowercase()}", layout) { choose(kind, change) }
        Label(path.substringAfterLast('/').ifEmpty { "Godot default" })
        val details = when {
            layout?.visible == false -> "Hidden at launch"
            preview != null -> formatArtworkMeta(path, preview)
            path.isNotEmpty() -> "Preview unavailable"
            kind == GameResourceKind.Splash -> "PNG"
            else -> "No custom artwork"
        }
        StatusLine(details)
        SettingsActions(4) {
            Button(if (path.isEmpty()) "Choose…" else "Replace…", { choose(kind, change) })
            if (path.isNotEmpty()) Button("Reset", { change("") })
        }
    }
}
