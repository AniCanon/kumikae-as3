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

/** Everything may be left empty: Godot is then looked for, and the game is the project's own. */
internal class GodotRunEditor : SettingsEditor<GodotRunConfiguration>() {
    private var godot by mutableStateOf("")
    private var folder by mutableStateOf("")
    private var scene by mutableStateOf("")

    override fun resetEditorFrom(configuration: GodotRunConfiguration) {
        godot = configuration.options.godot
        folder = configuration.options.projectFolder
        scene = configuration.options.scene
    }

    override fun applyEditorTo(configuration: GodotRunConfiguration) {
        configuration.options.godot = godot.trim()
        configuration.options.projectFolder = folder.trim()
        configuration.options.scene = scene.trim()
    }

    private fun edited(set: () -> Unit) {
        set()
        fireEditorStateChanged()
    }

    override fun createEditor(): JComponent = composePanel(this) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.emptyBorder(JBUI.insets(8))) {
            Column(SwingModifier.north()) {
                LabelledField("Godot (empty: found on the search path or in Applications)", godot, {
                    edited { godot = it }
                })
                LabelledField("Project folder (empty: this project)", folder, { edited { folder = it } })
                LabelledField("Scene (empty: the project's main scene)", scene, { edited { scene = it } })
            }
        }
    }
}
