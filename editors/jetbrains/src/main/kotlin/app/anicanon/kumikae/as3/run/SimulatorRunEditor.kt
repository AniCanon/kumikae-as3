package app.anicanon.kumikae.as3.run

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import javax.swing.JComponent

internal class SimulatorRunEditor : SettingsEditor<SimulatorRunConfiguration>() {
    private var presets by mutableStateOf(emptyList<String>())
    private var preset by mutableStateOf("")
    private var game by mutableStateOf("")
    private var problem by mutableStateOf("")
    private var deviceAppPath by mutableStateOf("")
    private var legacy by mutableStateOf(false)
    private var bundle by mutableStateOf("")
    private var appPath by mutableStateOf("")

    override fun resetEditorFrom(configuration: SimulatorRunConfiguration) {
        val root = configuration.project.basePath?.let { LocalFileSystem.getInstance().findFileByPath(it) }
        fun text(name: String) =
            root?.findChild(name)?.let { FileDocumentManager.getInstance().getDocument(it)?.text }.orEmpty()
        runCatching {
            game = GodotConfig(text("project.godot")).string("application", "config/name", configuration.project.name)
            presets = GameTargets.presets(GodotConfig(text("export_presets.cfg"))).filter {
                it.platform == "iOS"
            }.map { it.name }
        }.onFailure {
            presets = emptyList()
            problem = it.message ?: "Cannot read Godot settings."
        }.onSuccess { problem = "" }
        deviceAppPath = configuration.options.deviceAppPath
        bundle = configuration.options.bundle
        appPath = configuration.options.appPath
        legacy = configuration.options.preset.isEmpty() && bundle.isNotEmpty()
        preset = configuration.options.preset.ifEmpty { presets.singleOrNull().orEmpty() }
    }

    override fun applyEditorTo(configuration: SimulatorRunConfiguration) {
        configuration.options.preset = if (legacy) "" else preset.ifEmpty { configuration.options.preset }
        configuration.options.deviceAppPath = deviceAppPath.trim()
        configuration.options.bundle = if (legacy) bundle.trim() else ""
        configuration.options.appPath = if (legacy) appPath.trim() else ""
    }

    override fun createEditor(): JComponent = composePanel(this) {
        Panel(PanelLayout.Border(), modifier = SwingModifier.emptyBorder(JBUI.insets(12))) {
            SettingsSection("iOS", SwingModifier.north()) {
                Label(game)
                if (problem.isNotEmpty()) StatusLine(problem)
                if (legacy) {
                    StatusLine("This configuration uses a previously exported app.")
                    Button("Build from Godot settings", {
                        legacy = false
                        fireEditorStateChanged()
                    })
                    LabelledField("Custom bundle identifier", bundle, {
                        bundle = it
                        fireEditorStateChanged()
                    })
                    LabelledField("Simulator app", appPath, {
                        appPath = it
                        fireEditorStateChanged()
                    })
                } else {
                    Label("Godot target")
                    if (presets.isEmpty()) {
                        StatusLine("Adding this configuration creates an iOS export preset.")
                    } else {
                        ComboBox(presets, preset.takeIf { it in presets }, onSelectionChange = {
                            if (it != null) {
                                preset = it
                                fireEditorStateChanged()
                            }
                        }) { Label(it) }
                        if (preset !in presets) StatusLine("Choose an existing iOS target.")
                        StatusLine("Select an iPhone, iPad or simulator in the Run toolbar.")
                    }
                }
                LabelledField("Signed device .app (optional)", deviceAppPath, {
                    deviceAppPath = it
                    fireEditorStateChanged()
                })
            }
        }
    }
}
