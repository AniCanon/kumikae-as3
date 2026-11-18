package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.run.GodotLaunch
import com.google.gson.Gson
import com.intellij.execution.RunContentExecutor
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.openapi.components.service
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.vfs.VirtualFile

internal fun exportGame(project: Project, root: VirtualFile, settings: ProjectSettings, presets: List<ExportPreset>) {
    val chooser = FileChooserDescriptorFactory.createSingleFolderDescriptor().withTitle("Export Game Into Folder")
    FileChooser.chooseFile(chooser, project, root.parent) { destination ->
        runCatching {
            FileDocumentManager.getInstance().saveAllDocuments()
            val godot =
                requireNotNull(GodotLaunch.findGodot("", System.getenv("PATH").orEmpty())) {
                    "Install Godot to export the game."
                }
            val runtime = requireNotNull(PluginFiles.native()) { "The plugin's runtime is missing." }
            val config = GodotConfig(settings.exportText)
            val jobs = presets.map { preset ->
                val filename = config.string(preset.section, "export_path").substringAfterLast('/').ifBlank {
                    val suffix = when (preset.platform) {
                        "iOS" -> ".ipa"

                        "Android" -> if (config.raw("${preset.section}.options", "gradle_build/export_format") ==
                            "1"
                        ) {
                            ".aab"
                        } else {
                            ".apk"
                        }

                        "Windows Desktop" -> ".exe"

                        "Linux" -> ".x86_64"

                        "macOS" -> ".zip"

                        "Web" -> ".html"

                        else -> error("Set an export filename for ${preset.name} in Godot.")
                    }
                    settings.name.replace(Regex("[^A-Za-z0-9_-]"), "").ifBlank { "Game" } + suffix
                }
                mapOf("name" to preset.name, "platform" to preset.platform, "filename" to filename)
            }
            val command = GeneralCommandLine(
                "python3", "-I", "-u", runtime.resolve("tools/game_export.py").toString(),
                root.path, destination.path, godot.toString(), runtime.toString(), Gson().toJson(jobs),
            )
            val sdk = project.service<app.anicanon.kumikae.as3.run.AndroidDevices>()
            sdk.sdk?.let {
                command.withEnvironment(
                    "ANDROID_HOME",
                    it.toString(),
                ).withEnvironment("ANDROID_SDK_ROOT", it.toString())
            }
            val handler = KillableColoredProcessHandler(command)
            ProcessTerminatedListener.attach(handler)
            RunContentExecutor(project, handler).withTitle("Export Game").withActivateToolWindow(true)
                .withStop({ handler.destroyProcess() }, { !handler.isProcessTerminated }).run()
        }.onFailure { Messages.showErrorDialog(project, it.message ?: "Could not start Godot.", "Export Game") }
    }
}
