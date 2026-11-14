package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.language.CompilerLocator
import app.anicanon.kumikae.as3.panel.SimulatorHost
import app.anicanon.kumikae.as3.project.ProjectTemplate
import com.intellij.execution.DefaultExecutionResult
import com.intellij.execution.ExecutionResult
import com.intellij.execution.Executor
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.configurations.RunConfigurationBase
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.execution.configurations.RunProfileState
import com.intellij.execution.configurations.RuntimeConfigurationError
import com.intellij.execution.configurations.SimpleConfigurationType
import com.intellij.execution.filters.TextConsoleBuilderFactory
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.execution.runners.ProgramRunner
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.components.service
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.NotNullLazyValue
import com.intellij.openapi.wm.ToolWindowManager
import java.nio.file.Path

/** "iOS Simulator (AS3)" in Run > Edit Configurations: an app's bundle, and optionally the app to install. */
class SimulatorConfigurationType :
    SimpleConfigurationType(
        "KumikaeAs3Simulator",
        "iOS Simulator (AS3)",
        "Install and launch an app on a running iOS Simulator",
        NotNullLazyValue.createValue { PluginFiles.mark },
    ) {
    override fun createTemplateConfiguration(project: Project): RunConfiguration =
        SimulatorRunConfiguration(project, this, "iOS Simulator")
}

class SimulatorRunOptions : RunConfigurationOptions() {
    private val bundleProperty = string("org.kumikae.as3test").provideDelegate(this, "bundle")
    private val appProperty = string("").provideDelegate(this, "appPath")

    var bundle: String
        get() = bundleProperty.getValue(this).orEmpty()
        set(value) = bundleProperty.setValue(this, value)
    var appPath: String
        get() = appProperty.getValue(this).orEmpty()
        set(value) = appProperty.setValue(this, value)
}

class SimulatorRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
    RunConfigurationBase<SimulatorRunOptions>(project, factory, name) {
    public override fun getOptions(): SimulatorRunOptions = super.getOptions() as SimulatorRunOptions

    override fun getOptionsClass(): Class<out RunConfigurationOptions> = SimulatorRunOptions::class.java

    override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> = SimulatorRunEditor()

    override fun checkConfiguration() {
        if (options.bundle.isBlank()) throw RuntimeConfigurationError("Set the app's bundle identifier.")
    }

    override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState {
        val bundle = options.bundle
        val appPath = options.appPath
        return object : RunProfileState {
            override fun execute(executor: Executor?, runner: ProgramRunner<*>): ExecutionResult {
                val handler =
                    SimulatorProcessHandler(project.service<SimulatorHost>().repository, bundle, appPath, builder())
                val console = TextConsoleBuilderFactory.getInstance().createBuilder(project).console
                console.attachToProcess(handler)
                // The app appears in the panel, so the panel comes forward with it.
                ToolWindowManager.getInstance(project).getToolWindow(TOOL_WINDOW)?.show()
                return DefaultExecutionResult(console, handler)
            }
        }
    }

    /**
     * How the game is built for the Simulator when no app is named: from the project's own iOS export
     * preset. Null where there is none, and the run then launches what is already installed.
     */
    private fun builder(): (((String, Boolean) -> Unit) -> String)? {
        val folder = project.basePath?.let(Path::of)?.takeIf(IosSimulatorBuild::canBuild) ?: return null
        val home = Path.of(System.getProperty("user.home"))
        val settings = PropertiesComponent.getInstance()
        val native = PluginFiles.native()
        val compiler = CompilerLocator.find(
            folder,
            settings.getValue(COMPILER_PATH).orEmpty(),
            System.getenv("PATH").orEmpty(),
            home,
            native,
        )
        return { say ->
            val godot = GodotLaunch.findGodot("", System.getenv("PATH").orEmpty())
                ?: error("Godot was not found: it writes the Xcode project. Install it or put it on the search path.")
            val template = IosSimulatorBuild.findTemplate(settings.getValue(TEMPLATE_PATH).orEmpty(), home, compiler)
                ?: error(
                    "The iOS Simulator needs one of Godot's export templates with a library for Apple silicon, " +
                        "which Godot does not publish. Build it once with " +
                        "compiler/tests/build_ios_simulator_template.sh and keep it as " +
                        "~/Library/Application Support/Kumikae AS3/ios-with-arm64-simulator.zip.",
                )
            val library = ProjectTemplate.libraryBeside(compiler)?.resolve("ios") ?: native?.resolve("ios")
            IosSimulatorBuild(folder, godot, template, library).build(say)
        }
    }

    private companion object {
        const val COMPILER_PATH = "kumikae.as3.compilerPath"
        const val TEMPLATE_PATH = "kumikae.as3.iosSimulatorTemplate"
        const val TOOL_WINDOW = "iOS Simulator"
    }
}
