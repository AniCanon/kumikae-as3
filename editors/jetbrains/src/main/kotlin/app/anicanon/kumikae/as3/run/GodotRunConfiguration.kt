package app.anicanon.kumikae.as3.run

import com.intellij.execution.ExecutionException
import com.intellij.execution.Executor
import com.intellij.execution.configurations.CommandLineState
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.configurations.RunConfigurationBase
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.execution.configurations.RunProfileState
import com.intellij.execution.configurations.RuntimeConfigurationError
import com.intellij.execution.configurations.SimpleConfigurationType
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessHandler
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.icons.AllIcons
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.NotNullLazyValue
import java.nio.file.Path

/** "Godot (AS3)": runs the project's game on this computer, with what it prints in the Run window. */
class GodotConfigurationType :
    SimpleConfigurationType(
        ID,
        "Godot (AS3)",
        "Run the project's game with Godot",
        NotNullLazyValue.createValue { AllIcons.Actions.Execute },
    ) {
    override fun createTemplateConfiguration(project: Project): RunConfiguration =
        GodotRunConfiguration(project, this, "Game")

    companion object {
        const val ID = "KumikaeAs3Godot"
    }
}

class GodotRunOptions : RunConfigurationOptions() {
    private val godotProperty = string("").provideDelegate(this, "godot")
    private val folderProperty = string("").provideDelegate(this, "projectFolder")
    private val sceneProperty = string("").provideDelegate(this, "scene")

    var godot: String
        get() = godotProperty.getValue(this).orEmpty()
        set(value) = godotProperty.setValue(this, value)
    var projectFolder: String
        get() = folderProperty.getValue(this).orEmpty()
        set(value) = folderProperty.setValue(this, value)
    var scene: String
        get() = sceneProperty.getValue(this).orEmpty()
        set(value) = sceneProperty.setValue(this, value)
}

class GodotRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
    RunConfigurationBase<GodotRunOptions>(project, factory, name) {
    public override fun getOptions(): GodotRunOptions = super.getOptions() as GodotRunOptions

    override fun getOptionsClass(): Class<out RunConfigurationOptions> = GodotRunOptions::class.java

    override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> = GodotRunEditor()

    private fun godot(): Path? = GodotLaunch.findGodot(options.godot, System.getenv("PATH").orEmpty())

    /** The folder that was set, or the project's own. */
    private fun folder(): Path? = options.projectFolder.trim().ifEmpty { project.basePath.orEmpty() }
        .takeIf { it.isNotEmpty() }?.let(Path::of)

    override fun checkConfiguration() {
        if (godot() == null) throw RuntimeConfigurationError("Godot was not found. Set its path in this configuration.")
        if (folder()?.resolve("project.godot")?.toFile()?.isFile != true) {
            throw RuntimeConfigurationError("The folder has no project.godot.")
        }
    }

    override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState =
        object : CommandLineState(environment) {
            override fun startProcess(): ProcessHandler {
                val godot =
                    godot() ?: throw ExecutionException("Godot was not found. Set its path in the run configuration.")
                val folder = folder() ?: throw ExecutionException("The project has no folder.")
                val line = GeneralCommandLine(GodotLaunch.command(godot, folder, options.scene))
                    .withWorkDirectory(folder.toFile())
                return KillableColoredProcessHandler(line).also(ProcessTerminatedListener::attach)
            }
        }
}
