package app.anicanon.kumikae.as3.project

import androidx.compose.runtime.Composable
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.FlowLayout
import javax.swing.JProgressBar

@Composable
internal fun GodotSetupSection(controller: GodotSetupController, canInstall: Boolean, choose: () -> Unit) {
    SettingsSection("Game engine") {
        val engine = controller.installation
        if (controller.busy) {
            Label(controller.message)
            SwingNode(factory = { JProgressBar().apply { isIndeterminate = true } })
        } else {
            Label(engine?.let { "Godot ${it.version} · Ready" } ?: "Godot 4.7 needed to run your game")
            val detail = when {
                engine != null -> engine.executable.toString().removeSuffix("/${LocalGodotSetup.BINARY}")
                canInstall -> "Official macOS download · 171 MB · Installs for your user account."
                else -> "Choose your Godot executable."
            }
            StatusLine(detail)
            if (controller.message.isNotEmpty()) StatusLine(controller.message)
            Panel(PanelLayout.Flow(FlowLayout.LEADING, 4, 0)) {
                if (engine == null &&
                    canInstall
                ) {
                    Button("Install Godot ${LocalGodotSetup.VERSION}", controller::install)
                }
                Button("Choose existing…", choose)
            }
        }
    }
}
