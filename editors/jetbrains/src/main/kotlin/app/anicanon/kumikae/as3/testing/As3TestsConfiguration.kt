package app.anicanon.kumikae.as3.testing

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.language.CompilerLocator
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
    override fun createTemplateConfiguration(project: Project): RunConfiguration =
        As3TestsConfiguration(project, this, "AS3 Tests")
    companion object {
        const val ID = "KumikaeAs3Tests"
    }
}

class As3TestsOptions : RunConfigurationOptions() {
    private val manifestProperty = string("kas3-tests.json").provideDelegate(this, "manifest")
    private val casesProperty = string("").provideDelegate(this, "cases")
    var manifest: String
        get() = manifestProperty.getValue(this).orEmpty()
        set(value) = manifestProperty.setValue(this, value)
    var cases: String
        get() = casesProperty.getValue(this).orEmpty()
        set(value) = casesProperty.setValue(this, value)
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
                val compiler =
                    CompilerLocator.find(
                        root(),
                        "",
                        System.getenv("PATH").orEmpty(),
                        Path.of(System.getProperty("user.home")),
                        PluginFiles.native(),
                    )
                        ?: throw RuntimeConfigurationError("Build kas3c or place it on the search path.")
                val command =
                    mutableListOf(
                        "python3",
                        "-u",
                        runner().toString(),
                        root().resolve(options.manifest).toString(),
                        "--compiler",
                        compiler.toString(),
                    )
                for (name in options.cases.lines().filter { it.isNotBlank() }) command += listOf("--case", name.trim())
                return KillableColoredProcessHandler(
                    GeneralCommandLine(command).withWorkDirectory(root().toFile()),
                ).also(ProcessTerminatedListener::attach)
            }
        }
    }
}
