package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.language.CompilerLocator
import app.anicanon.kumikae.as3.panel.SimulatorHost
import app.anicanon.kumikae.as3.panel.SimulatorToolWindowFactory
import app.anicanon.kumikae.as3.project.ProjectTemplate
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.GodotConfig
import app.anicanon.kumikae.as3.settings.IosTarget
import com.intellij.execution.DefaultExecutionResult
import com.intellij.execution.ExecutionException
import com.intellij.execution.ExecutionResult
import com.intellij.execution.ExecutionTarget
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
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.service
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.NotNullLazyValue
import org.jdom.Element
import java.nio.file.Files
import java.nio.file.Path

/** "iOS Simulator (AS3)" in Run > Edit Configurations: an app's bundle, and optionally the app to install. */
class SimulatorConfigurationType :
    SimpleConfigurationType(
        "KumikaeAs3Simulator",
        "iOS Simulator (AS3)",
        "Install and launch an app on a running iOS Simulator",
        NotNullLazyValue.createValue { PluginFiles.mark },
    ) {
    override fun isApplicable(project: Project) = false

    override fun createTemplateConfiguration(project: Project): RunConfiguration =
        SimulatorRunConfiguration(project, this, "iOS Simulator")
}

class SimulatorRunOptions : RunConfigurationOptions() {
    private val bundleProperty = string("").provideDelegate(this, "bundle")
    private val appProperty = string("").provideDelegate(this, "appPath")

    private val presetProperty = string("").provideDelegate(this, "preset")
    var preset: String
        get() = presetProperty.getValue(this).orEmpty()
        set(value) = presetProperty.setValue(this, value)

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

    override fun readExternal(element: Element) {
        super.readExternal(element)
        val base = project.basePath?.let(Path::of) ?: return
        if (options.preset.isNotBlank() || options.appPath.isBlank()) return
        val generated = base.resolve(".build/ios/build/Build/Products/Debug-iphonesimulator")
        val matching = runCatching {
            val product = Path.of(options.appPath.replace("\$PROJECT_DIR\$", base.toString())).normalize()
            if (product.parent != generated || !product.fileName.toString().endsWith(".app")) return@runCatching null
            val text = base.resolve("export_presets.cfg").takeIf(Files::isRegularFile)?.let(Files::readString)
                ?: return@runCatching null
            GameTargets.presets(GodotConfig(text)).filter {
                it.platform == "iOS" && GameTargets.ios(text, it.name).bundle == options.bundle
            }.singleOrNull()
        }.getOrNull() ?: return
        options.preset = matching.name
        options.appPath = ""
        options.bundle = ""
    }

    override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> = SimulatorRunEditor()

    override fun canRunOn(target: ExecutionTarget) = target is SimulatorDestination ||
        (options.preset.isBlank() && options.bundle.isNotBlank() && super.canRunOn(target))

    override fun checkConfiguration() {
        try {
            target()
        } catch (error: Exception) {
            throw RuntimeConfigurationError(error.message ?: "Cannot read the iOS target.")
        }
    }

    override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState {
        val target = try {
            target()
        } catch (error: Exception) {
            throw ExecutionException(error.message ?: "Cannot read the iOS target.")
        }
        if (target != null && environment.executionTarget !is SimulatorDestination) {
            throw ExecutionException("Choose an available simulator in the Run toolbar.")
        }
        val bundle = target?.bundle ?: options.bundle
        val appPath = if (target == null) options.appPath else ""
        return object : RunProfileState {
            override fun execute(executor: Executor?, runner: ProgramRunner<*>): ExecutionResult {
                val handler = SimulatorProcessHandler(
                    project.service<SimulatorHost>().repository,
                    bundle,
                    appPath,
                    builder(target),
                    sources(),
                    (environment.executionTarget as? SimulatorDestination)?.device?.id,
                )
                val console = TextConsoleBuilderFactory.getInstance().createBuilder(project).console
                console.attachToProcess(handler)
                // The app appears in the panel, so the panel comes forward with it.
                ApplicationManager.getApplication().invokeLater {
                    if (!project.isDisposed) SimulatorToolWindowFactory.showIos(project)
                }
                return DefaultExecutionResult(console, handler)
            }
        }
    }

    /** The game's own folder, where its scripts are edited: empty where the project is not a Godot game. */
    private fun sources(): String =
        project.basePath?.takeIf { Files.isRegularFile(Path.of(it, "project.godot")) }.orEmpty()

    /**
     * How the game is built for the Simulator when no app is named: from the project's own iOS export
     * preset. Null where there is none, and the run then launches what is already installed.
     */
    private fun target(): IosTarget? {
        if (options.preset.isEmpty() && options.bundle.isNotBlank()) return null
        val base = requireNotNull(project.basePath) { "Open the Godot project directory." }
        val path = Path.of(base, "export_presets.cfg")
        require(Files.isRegularFile(path)) { "Add an iOS target to this game first." }
        return GameTargets.ios(Files.readString(path), options.preset)
    }

    private fun builder(target: IosTarget?): (((String, Boolean) -> Unit) -> String)? {
        val folder =
            project.basePath?.let(Path::of)?.takeIf { target != null || IosSimulatorBuild.canBuild(it) } ?: return null
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
            val template = target?.template?.takeIf { it.isNotBlank() }?.let {
                folder.resolve(it.removePrefix("res://")).also { path ->
                    check(Files.isRegularFile(path)) { "The selected Godot preset's debug template is missing: $it" }
                }
            } ?: IosSimulatorBuild.findTemplate(settings.getValue(TEMPLATE_PATH).orEmpty(), home, compiler)
                ?: native?.resolve("ios-with-arm64-simulator.zip")?.takeIf(Files::isRegularFile)
                ?: error(
                    "The iOS Simulator needs one of Godot's export templates with a library for Apple silicon, " +
                        "which Godot does not publish. Build it once with " +
                        "compiler/tests/build_ios_simulator_template.sh and keep it as " +
                        "~/Library/Application Support/Kumikae AS3/ios-with-arm64-simulator.zip.",
                )
            val library = ProjectTemplate.libraryBeside(compiler)?.resolve("ios") ?: native?.resolve("ios")
            IosSimulatorBuild(folder, godot, template, library, target).build(say)
        }
    }

    private companion object {
        const val COMPILER_PATH = "kumikae.as3.compilerPath"
        const val TEMPLATE_PATH = "kumikae.as3.iosSimulatorTemplate"
    }
}
