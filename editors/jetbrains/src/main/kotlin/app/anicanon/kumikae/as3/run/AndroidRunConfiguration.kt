package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.panel.SimulatorToolWindowFactory
import com.intellij.execution.ExecutionException
import com.intellij.execution.ExecutionTarget
import com.intellij.execution.Executor
import com.intellij.execution.configurations.CommandLineState
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.configurations.RuntimeConfigurationError
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.service
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import java.nio.file.Files
import java.nio.file.Path

class AndroidRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
    PresetRunConfiguration(project, factory, name, "Android") {
    override fun canRunOn(target: ExecutionTarget) = target is AndroidDestination

    override fun checkConfiguration() {
        super.checkConfiguration()
        if (project.service<AndroidDevices>().sdk == null) {
            throw RuntimeConfigurationError("Install Android SDK Platform Tools.")
        }
    }

    override fun getState(executor: Executor, environment: ExecutionEnvironment) =
        object : CommandLineState(environment) {
            override fun startProcess(): KillableColoredProcessHandler {
                val target = environment.executionTarget as? AndroidDestination
                    ?: throw ExecutionException(
                        "Start an ARM64 emulator or connect an Android device and select it in the Run toolbar.",
                    )
                val basePath = project.basePath ?: throw ExecutionException("Open the Godot project directory.")
                val sdk = project.service<AndroidDevices>().sdk
                    ?: throw ExecutionException("Install Android SDK Platform Tools.")
                val presetName = try {
                    selectedPreset()
                } catch (error: Exception) {
                    throw ExecutionException(error.message ?: "Choose an Android export preset in this configuration.")
                }
                val native =
                    PluginFiles.native() ?: throw ExecutionException("Reinstall the plugin with the Android runtime.")
                val runnerScript = native.resolve("tools/android_run.py")
                if (!Files.isRegularFile(runnerScript)) {
                    throw ExecutionException("The Android run script is missing from this plugin.")
                }
                val runtime = native.resolve("android/arm64-v8a/libkas3_godot.so")
                if (!Files.isRegularFile(runtime)) {
                    throw ExecutionException("The Android AS3 runtime is missing from this plugin.")
                }
                val godot = IdeGodot.find()
                    ?: throw ExecutionException("Install Godot to export the game.")
                val line = GeneralCommandLine(
                    "python3",
                    "-I",
                    "-u",
                    runnerScript.toString(),
                    basePath,
                    presetName,
                    target.serial,
                    sdk.toString(),
                    godot.toString(),
                    runtime.toString(),
                ).withEnvironment("JAVA_HOME", System.getenv("JAVA_HOME") ?: System.getProperty("java.home"))
                return KillableColoredProcessHandler(line).also { process ->
                    ProcessTerminatedListener.attach(process)
                    if (target.serial.startsWith("emulator-") && SystemInfo.isMac) {
                        ApplicationManager.getApplication().invokeLater {
                            if (!project.isDisposed) {
                                SimulatorToolWindowFactory.showAndroid(project, target.serial)
                            }
                        }
                    }
                }
            }
        }
}
