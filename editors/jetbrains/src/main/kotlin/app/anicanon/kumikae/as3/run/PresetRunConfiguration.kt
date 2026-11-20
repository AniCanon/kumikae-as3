package app.anicanon.kumikae.as3.run

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.project.NewGameTargetListener
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.execution.ExecutionException
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.RunConfigurationBase
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.util.concurrency.AppExecutorUtil
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import java.io.IOException
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
    override fun onNewConfigurationCreated() {
        options.preset = NewGameTargetListener.prepare(this, platform, options.preset)
    }

    internal fun presets(): List<String> {
        val folder = project.basePath ?: return emptyList()
        val file = Path.of(folder, "export_presets.cfg")
        if (!Files.isRegularFile(file)) return emptyList()
        return GameTargets.presets(GodotConfig(Files.readString(file))).filter {
            it.platform == platform
        }.map { it.name }
    }

    protected fun selectedPreset(): String = try {
        presets().singleOrNull { options.preset.isEmpty() || it == options.preset }
            ?: throw ExecutionException("Choose a $platform export preset in this configuration.")
    } catch (error: IOException) {
        throw ExecutionException("Cannot read export_presets.cfg.", error)
    }

    override fun getConfigurationEditor(): SettingsEditor<PresetRunConfiguration> =
        PresetEditor(platform, PresetRunConfiguration::presets)
}

internal class PresetEditor(
    private val platform: String,
    private val loadPresets: (PresetRunConfiguration) -> List<String>,
) : SettingsEditor<PresetRunConfiguration>() {
    private var presets by mutableStateOf(emptyList<String>())
    private var selected by mutableStateOf("")
    private var problem by mutableStateOf("")

    override fun resetEditorFrom(configuration: PresetRunConfiguration) {
        selected = configuration.options.preset
        presets = emptyList()
        problem = "Loading export presets…"
        ReadAction.nonBlocking<Result<List<String>>> {
            try {
                Result.success(loadPresets(configuration))
            } catch (error: IOException) {
                Result.failure(error)
            }
        }.coalesceBy(this).expireWith(this).finishOnUiThread(ModalityState.any()) { result ->
            presets = result.getOrDefault(emptyList())
            problem = result.exceptionOrNull()?.message.orEmpty()
            if (selected.isEmpty()) selected = presets.singleOrNull().orEmpty()
        }.submit(AppExecutorUtil.getAppExecutorService())
    }

    override fun applyEditorTo(configuration: PresetRunConfiguration) {
        if (selected.isNotEmpty()) configuration.options.preset = selected
    }

    override fun createEditor() = composePanel(this) {
        SettingsSection("$platform launch") {
            Label("Godot export preset")
            if (problem.isNotEmpty()) {
                StatusLine(problem)
            } else if (presets.isEmpty()) {
                StatusLine("Adding this configuration creates a $platform export preset.")
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
