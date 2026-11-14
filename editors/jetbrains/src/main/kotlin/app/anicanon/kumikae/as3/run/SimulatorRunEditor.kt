package app.anicanon.kumikae.as3.run

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.options.SettingsEditor
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import javax.swing.JComponent

/** The two things a run needs to know. The device is whichever the panel shows. */
internal class SimulatorRunEditor : SettingsEditor<SimulatorRunConfiguration>() {
    private var bundle by mutableStateOf("")
    private var appPath by mutableStateOf("")

    override fun resetEditorFrom(configuration: SimulatorRunConfiguration) {
        bundle = configuration.options.bundle
        appPath = configuration.options.appPath
    }

    override fun applyEditorTo(configuration: SimulatorRunConfiguration) {
        configuration.options.bundle = bundle.trim()
        configuration.options.appPath = appPath.trim()
    }

    override fun createEditor(): JComponent = composePanel(this) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.emptyBorder(JBUI.insets(8))) {
            Column(SwingModifier.north()) {
                LabelledField("Bundle identifier", bundle, {
                    bundle = it
                    fireEditorStateChanged()
                })
                LabelledField("App to install first (optional, an .app folder)", appPath, {
                    appPath = it
                    fireEditorStateChanged()
                })
            }
        }
    }
}
