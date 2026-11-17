package app.anicanon.kumikae.as3.run

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.panel.SimulatorToolWindowFactory
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.execution.ExecutionException
import com.intellij.execution.ExecutionTarget
import com.intellij.execution.Executor
import com.intellij.execution.configurations.CommandLineState
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.configurations.RunConfigurationBase
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.execution.configurations.RuntimeConfigurationError
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.service
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import java.nio.file.Files
import java.nio.file.Path

class AndroidOptions : RunConfigurationOptions() {
    private val presetProperty = string("").provideDelegate(this, "preset")
    var preset: String
        get() = presetProperty.getValue(this).orEmpty()
        set(value) = presetProperty.setValue(this, value)
}

class AndroidRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
    RunConfigurationBase<AndroidOptions>(project, factory, name) {
    public override fun getOptions() = super.getOptions() as AndroidOptions
    override fun getOptionsClass() = AndroidOptions::class.java
    override fun canRunOn(target: ExecutionTarget) = target is AndroidDestination
    internal fun presets(): List<String> {
        val folder = project.basePath ?: return emptyList()
        val file = Path.of(folder, "export_presets.cfg")
        if (!Files.isRegularFile(file)) return emptyList()
        return GameTargets.presets(GodotConfig(Files.readString(file))).filter {
            it.platform == "Android"
        }.map { it.name }
    }
    private fun preset() = presets().singleOrNull { options.preset.isEmpty() || it == options.preset }
        ?: throw RuntimeConfigurationError("Choose an Android export preset in this configuration.")
    override fun checkConfiguration() {
        preset()
        if (project.service<AndroidDevices>().sdk ==
            null
        ) {
            throw RuntimeConfigurationError("Install Android SDK Platform Tools.")
        }
    }
    override fun getConfigurationEditor() = AndroidEditor()
    override fun getState(executor: Executor, environment: ExecutionEnvironment) =
        object : CommandLineState(environment) {
            override fun startProcess(): KillableColoredProcessHandler {
                val target = environment.executionTarget as? AndroidDestination
                    ?: throw ExecutionException(
                        "Start an ARM64 emulator or connect an Android device and select it in the Run toolbar.",
                    )
                val native =
                    PluginFiles.native() ?: throw ExecutionException("Reinstall the plugin with the Android runtime.")
                val godot =
                    GodotLaunch.findGodot("", System.getenv("PATH").orEmpty())
                        ?: throw ExecutionException("Install Godot to export the game.")
                val line = GeneralCommandLine(
                    "python3", "-I", "-u",
                    native.resolve(
                        "tools/android_run.py",
                    ).toString(),
                    project.basePath!!, preset(), target.serial,
                    project.service<AndroidDevices>().sdk.toString(), godot.toString(),
                    native.resolve(
                        "android/arm64-v8a/libkas3_godot.so",
                    ).toString(),
                ).withEnvironment("JAVA_HOME", System.getenv("JAVA_HOME") ?: System.getProperty("java.home"))
                return KillableColoredProcessHandler(line).also { process ->
                    ProcessTerminatedListener.attach(process)
                    if (target.serial.startsWith("emulator-") && SystemInfo.isMac) {
                        ApplicationManager.getApplication().invokeLater {
                            if (!project.isDisposed) {
                                SimulatorToolWindowFactory.showAndroid(
                                    project,
                                    target.serial,
                                )
                            }
                        }
                    }
                }
            }
        }
}

class AndroidEditor : SettingsEditor<AndroidRunConfiguration>() {
    private var presets by mutableStateOf(emptyList<String>())
    private var selected by mutableStateOf("")
    override fun resetEditorFrom(configuration: AndroidRunConfiguration) {
        presets = configuration.presets()
        selected = configuration.options.preset.ifEmpty { presets.singleOrNull().orEmpty() }
    }
    override fun applyEditorTo(configuration: AndroidRunConfiguration) {
        configuration.options.preset = selected
    }
    override fun createEditor() = composePanel(this) {
        Column {
            Label("Godot export preset")
            ComboBox(presets, selected.takeIf(presets::contains), onSelectionChange = {
                if (it != null) {
                    selected = it
                    fireEditorStateChanged()
                }
            }) { Label(it) }
            StatusLine("Select a connected device or running ARM64 emulator in the Run toolbar.")
        }
    }
}
