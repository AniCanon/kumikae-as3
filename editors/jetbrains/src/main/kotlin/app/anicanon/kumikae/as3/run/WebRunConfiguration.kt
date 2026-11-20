package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.PluginFiles
import com.intellij.execution.ExecutionException
import com.intellij.execution.Executor
import com.intellij.execution.configurations.CommandLineState
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.project.Project
import java.nio.file.Files
import java.nio.file.Path

class WebRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
    PresetRunConfiguration(project, factory, name, "Web") {
    override fun getState(executor: Executor, environment: ExecutionEnvironment) =
        object : CommandLineState(environment) {
            override fun startProcess(): KillableColoredProcessHandler {
                val root = project.basePath ?: throw ExecutionException("Open the Godot project directory.")
                val native = PluginFiles.native() ?: throw ExecutionException("Install the Kumikae Web runtime.")
                val godot = IdeGodot.find() ?: throw ExecutionException("Install Godot to export the game.")
                val line = GeneralCommandLine(
                    "python3",
                    "-I",
                    "-u",
                    native.resolve("tools/web_game.py").toString(),
                    root,
                    native.toString(),
                    godot.toString(),
                    selectedPreset(),
                )
                return KillableColoredProcessHandler(line).also { ProcessTerminatedListener.attach(it) }
            }
        }
}
