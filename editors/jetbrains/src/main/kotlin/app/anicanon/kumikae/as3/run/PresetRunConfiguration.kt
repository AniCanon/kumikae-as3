package app.anicanon.kumikae.as3.run

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.RunConfigurationBase
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.execution.configurations.RuntimeConfigurationError
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import java.nio.file.Files
import java.nio.file.Path

class PresetOptions : RunConfigurationOptions() {
    private val presetProperty = string("").provideDelegate(this, "preset")
    var preset: String
        get() = presetProperty.getValue(this).orEmpty()
        set(value) = presetProperty.setValue(this, value)
}

abstract class PresetRunConfiguration(
    project: Project,
    factory: ConfigurationFactory,
    name: String,
    internal val platform: String,
) : RunConfigurationBase<PresetOptions>(project, factory, name) {
    public override fun getOptions() = super.getOptions() as PresetOptions
    override fun getOptionsClass() = PresetOptions::class.java
    internal fun presets(): List<String> {
        val folder = project.basePath ?: return emptyList()
        val file = Path.of(folder, "export_presets.cfg")
        if (!Files.isRegularFile(file)) return emptyList()
        return GameTargets.presets(GodotConfig(Files.readString(file))).filter {
            it.platform == platform
        }.map { it.name }
    }

    protected fun selectedPreset(): String = presets().singleOrNull { options.preset.isEmpty() || it == options.preset }
        ?: throw RuntimeConfigurationError("Choose a $platform export preset in this configuration.")

    override fun checkConfiguration() {
        try {
            selectedPreset()
        } catch (error: RuntimeConfigurationError) {
            throw error
        } catch (error: Exception) {
            throw RuntimeConfigurationError(error.message ?: "Cannot read export_presets.cfg.")
        }
    }

    override fun getConfigurationEditor(): SettingsEditor<PresetRunConfiguration> = PresetEditor(platform)
}

internal class PresetEditor(private val platform: String) : SettingsEditor<PresetRunConfiguration>() {
    private var presets by mutableStateOf(emptyList<String>())
    private var selected by mutableStateOf("")
    private var problem by mutableStateOf("")

    override fun resetEditorFrom(configuration: PresetRunConfiguration) {
        runCatching { configuration.presets() }
            .onSuccess {
                presets = it
                problem = ""
            }
            .onFailure {
                presets = emptyList()
                problem = it.message ?: "Cannot read Godot export presets."
            }
        selected = configuration.options.preset.ifEmpty { presets.singleOrNull().orEmpty() }
    }

    override fun applyEditorTo(configuration: PresetRunConfiguration) {
        configuration.options.preset = selected
    }

    override fun createEditor() = composePanel(this) {
        SettingsSection("$platform launch") {
            if (problem.isNotEmpty()) StatusLine(problem)
            Label("Godot export preset")
            if (presets.isEmpty()) {
                StatusLine("Use Run > Add Game Target to configure $platform.")
            } else {
                ComboBox(presets, selected.takeIf(presets::contains), onSelectionChange = {
                    if (it != null) {
                        selected = it
                        fireEditorStateChanged()
                    }
                }) { Label(it) }
                if (platform == "Android") {
                    StatusLine("Select a connected device or running ARM64 emulator in the Run toolbar.")
                }
            }
        }
    }
}
