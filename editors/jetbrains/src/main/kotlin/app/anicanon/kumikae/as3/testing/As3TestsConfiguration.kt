package app.anicanon.kumikae.as3.testing

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.language.CompilerLocator
import app.anicanon.kumikae.as3.run.AndroidDevices
import app.anicanon.kumikae.as3.run.IdeGodot
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
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.execution.testframework.sm.SMTestRunnerConnectionUtil
import com.intellij.openapi.components.service
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.NotNullLazyValue
import java.nio.file.Files
import java.nio.file.Path

class As3TestsType :
    SimpleConfigurationType(
        ID,
        "AS3 Tests",
        "Run AS3 suites with native test results",
        NotNullLazyValue.createValue {
            PluginFiles.mark
        },
    ) {
    override fun isApplicable(project: Project) = false

    override fun createTemplateConfiguration(project: Project): RunConfiguration =
        As3TestsConfiguration(project, this, "AS3 Tests")
    companion object {
        const val ID = "KumikaeAs3Tests"
    }
}

class As3TestsOptions : RunConfigurationOptions() {
    private val manifestProperty = string("kas3-tests.json").provideDelegate(this, "manifest")
    private val casesProperty = string("").provideDelegate(this, "cases")
    private val nativeProperty = property(false).provideDelegate(this, "native")
    private val deviceProperty = string("").provideDelegate(this, "device")
    var manifest: String
        get() = manifestProperty.getValue(this).orEmpty()
        set(value) = manifestProperty.setValue(this, value)
    var cases: String
        get() = casesProperty.getValue(this).orEmpty()
        set(value) = casesProperty.setValue(this, value)
    var native: Boolean
        get() = nativeProperty.getValue(this)
        set(value) = nativeProperty.setValue(this, value)
    var device: String
        get() = deviceProperty.getValue(this).orEmpty()
        set(value) = deviceProperty.setValue(this, value)
}

class As3TestsConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
    RunConfigurationBase<As3TestsOptions>(project, factory, name) {
    public override fun getOptions(): As3TestsOptions = super.getOptions() as As3TestsOptions
    override fun getOptionsClass(): Class<out RunConfigurationOptions> = As3TestsOptions::class.java
    override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> = As3TestsEditor()
    private fun root() = Path.of(project.basePath ?: throw RuntimeConfigurationError("Open a project folder first."))
    private fun runner() = PluginFiles.native()?.resolve("testing/runner.py")
        ?.takeIf { Files.isRegularFile(it) }
        ?: throw RuntimeConfigurationError("The AS3 test runner is missing from this plugin installation.")

    override fun checkConfiguration() {
        if (!Files.isRegularFile(
                root().resolve(options.manifest),
            )
        ) {
            throw RuntimeConfigurationError("Test manifest not found: ${options.manifest}")
        }
        runner()
    }

    override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState {
        val configuration = this
        return object : CommandLineState(environment) {
            override fun createConsole(executor: Executor) = SMTestRunnerConnectionUtil.createConsole(
                "AS3",
                As3TestConsole(configuration, executor),
            )

            override fun startProcess(): KillableColoredProcessHandler {
                val projectRoot = try {
                    root()
                } catch (error: RuntimeConfigurationError) {
                    throw ExecutionException(error.message)
                }
                val runnerScript = try {
                    runner()
                } catch (error: RuntimeConfigurationError) {
                    throw ExecutionException(error.message)
                }
                val compiler = CompilerLocator.findForProject(projectRoot)
                    ?: throw ExecutionException("Build kas3c or place it on the search path.")
                val command = mutableListOf(
                    "python3",
                    "-I",
                    "-u",
                    runnerScript.toString(),
                    projectRoot.resolve(options.manifest).toString(),
                    "--compiler",
                    compiler.toString(),
                )
                IdeGodot.find()?.let {
                    command += listOf("--godot", it.toString())
                }
                if (options.native) command += "--native"
                options.device.trim().takeIf { it.isNotEmpty() }?.let { command += listOf("--device", it) }
                project.service<AndroidDevices>().sdk?.let { command += listOf("--android-sdk", it.toString()) }
                for (name in options.cases.lines().filter { it.isNotBlank() }) command += listOf("--case", name.trim())
                return KillableColoredProcessHandler(
                    GeneralCommandLine(command).withWorkDirectory(projectRoot.toFile()),
                ).also(ProcessTerminatedListener::attach)
            }
        }
    }
}
