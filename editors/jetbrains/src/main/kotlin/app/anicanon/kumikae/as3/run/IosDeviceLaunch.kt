package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.settings.IosTarget
import com.intellij.execution.ExecutionException
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessTerminatedListener
import java.nio.file.Files
import java.nio.file.Path

internal object IosDeviceLaunch {
    fun start(
        folder: Path,
        device: IosDevice,
        target: IosTarget?,
        bundle: String,
        signedApp: String,
    ): KillableColoredProcessHandler {
        val native = PluginFiles.native() ?: throw ExecutionException("The plugin's iOS run tools are missing.")
        val script = native.resolve("tools/ios-device.py")
        val arguments = mutableListOf(
            "/usr/bin/python3", "-I", "-u", script.toString(), "--project", folder.toString(),
            "--device", device.id, "--destination", device.buildId, "--bundle", bundle,
        )
        if (signedApp.isNotBlank()) {
            arguments += listOf("--app", folder.resolve(signedApp).toString())
        } else {
            val preset = target ?: GameTargets.ios(Files.readString(folder.resolve("export_presets.cfg")), "")
            val godot = IdeGodot.find()
                ?: throw ExecutionException("Install Godot to export the game.")
            val template = preset.template.takeIf(String::isNotBlank)?.let { folder.resolve(it.removePrefix("res://")) }
            arguments += listOf(
                "--godot", godot.toString(), "--template", template?.toString().orEmpty(),
                "--preset", preset.name, "--section", preset.section, "--team", preset.signingTeam,
                "--library", native.resolve("ios").toString(),
            )
        }
        return KillableColoredProcessHandler(GeneralCommandLine(arguments).withWorkDirectory(folder.toFile()))
            .also(ProcessTerminatedListener::attach)
    }
}
