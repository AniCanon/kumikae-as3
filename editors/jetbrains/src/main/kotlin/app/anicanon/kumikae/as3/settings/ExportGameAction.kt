package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.run.AndroidDevices
import app.anicanon.kumikae.as3.run.IdeGodot
import com.google.gson.Gson
import com.intellij.execution.RunContentExecutor
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessEvent
import com.intellij.execution.process.ProcessListener
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.notification.NotificationAction
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.service
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.progress.ProcessCanceledException
import com.intellij.openapi.progress.ProgressIndicator
import com.intellij.openapi.progress.Task
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.vfs.VirtualFile
import java.awt.Desktop
import java.nio.file.Files
import java.nio.file.Path

private val FILENAME_UNSAFE = Regex("[^A-Za-z0-9_-]")

internal fun exportJobs(
    settings: ProjectSettings,
    presets: List<ExportPreset>,
    store: Boolean,
): List<Map<String, Any>> {
    val config = GodotConfig(settings.exportText)
    return presets.map { preset ->
        val rawName = config.string(preset.section, "export_path").replace('\\', '/').substringAfterLast('/')
        val baseName = rawName.takeIf { it.isNotBlank() && it != "." && it != ".." } ?: run {
            val suffix = when (preset.platform) {
                "iOS" -> ".ipa"

                "Android" -> if (config.raw("${preset.section}.options", "gradle_build/export_format") == "1") {
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
            settings.name.replace(FILENAME_UNSAFE, "").ifBlank { "Game" } + suffix
        }
        val options = "${preset.section}.options"
        val filename = if (store) {
            baseName.substringBeforeLast('.').ifBlank { "Game" } + if (preset.platform == "Android") ".aab" else ".ipa"
        } else {
            baseName
        }
        mapOf(
            "name" to preset.name,
            "section" to preset.section,
            "platform" to preset.platform,
            "filename" to filename,
            "store" to store,
            "gradleDir" to config.string(options, "gradle_build/gradle_build_directory"),
            "projectOnly" to (store || config.raw(options, "application/export_project_only") == "true"),
        )
    }
}

internal fun exportGame(
    project: Project,
    root: VirtualFile,
    settings: ProjectSettings,
    presets: List<ExportPreset>,
    store: Boolean = false,
) {
    val chooser = FileChooserDescriptorFactory.createSingleFolderDescriptor().withTitle("Export Game Into Folder")
    FileChooser.chooseFile(chooser, project, root.parent) { destination ->
        try {
            FileDocumentManager.getInstance().saveAllDocuments()
            val godot = requireNotNull(IdeGodot.find()) {
                "Install Godot to export the game."
            }
            val runtime = requireNotNull(PluginFiles.native()) { "The plugin's runtime is missing." }
            val jobs = exportJobs(settings, presets, store)
            object : Task.Backgroundable(project, "Preparing game export", true) {
                private var signing = emptyMap<String, UploadKey>()
                override fun run(indicator: ProgressIndicator) {
                    signing = presets.filter { it.platform == "Android" }.mapNotNull { preset ->
                        indicator.checkCanceled()
                        AndroidUploadKeys(project, root.path, preset.name).read()?.let {
                            it.validate()
                            preset.section to it
                        }
                    }.toMap()
                }
                override fun onSuccess() {
                    if (project.isDisposed) return
                    try {
                        startExport(project, root, destination, godot.toString(), runtime, jobs, signing, store)
                    } catch (error: Exception) {
                        Messages.showErrorDialog(project, error.message ?: "Could not start Godot.", "Export Game")
                    }
                }
                override fun onThrowable(error: Throwable) {
                    Messages.showErrorDialog(project, error.message ?: "Could not load signing details.", "Export Game")
                }
            }.queue()
        } catch (canceled: ProcessCanceledException) {
            throw canceled
        } catch (error: Exception) {
            Messages.showErrorDialog(project, error.message ?: "Could not start Godot.", "Export Game")
        }
    }
}

private fun startExport(
    project: Project,
    root: VirtualFile,
    destination: VirtualFile,
    godot: String,
    runtime: Path,
    jobs: List<Map<String, Any>>,
    signing: Map<String, UploadKey>,
    store: Boolean,
) {
    val result = Files.createTempFile("kumikae-products-", ".json")
    try {
        val command = GeneralCommandLine(
            "python3",
            "-I",
            "-u",
            runtime.resolve("tools/game_export.py").toString(),
            root.path,
            destination.path,
            godot,
            runtime.toString(),
            Gson().toJson(jobs),
            result.toString(),
        )
        if (jobs.any { it["platform"] == "Android" }) {
            command.withEnvironment("JAVA_HOME", System.getenv("JAVA_HOME") ?: System.getProperty("java.home"))
        }
        val sdk = project.service<AndroidDevices>()
        sdk.sdk?.let {
            command.withEnvironment("ANDROID_HOME", it.toString())
                .withEnvironment("ANDROID_SDK_ROOT", it.toString())
        }
        val handler = KillableColoredProcessHandler(command)
        handler.addProcessListener(object : ProcessListener {
            override fun processTerminated(event: ProcessEvent) {
                try {
                    if (store && event.exitCode == 0) {
                        val products = Gson().fromJson(Files.readString(result), Array<String>::class.java)
                        val product = Path.of(products.single()).toRealPath()
                        if (product.startsWith(Path.of(destination.path).toRealPath())) {
                            ApplicationManager.getApplication().invokeLater {
                                if (!project.isDisposed) notifyProduct(project, product)
                            }
                        }
                    }
                } finally {
                    Files.deleteIfExists(result)
                }
            }
        })
        try {
            requireNotNull(handler.processInput).bufferedWriter(Charsets.UTF_8).use {
                it.write(Gson().toJson(signing))
            }
            ProcessTerminatedListener.attach(handler)
            RunContentExecutor(project, handler).withTitle("Export Game").withActivateToolWindow(true)
                .withStop({ handler.destroyProcess() }, { !handler.isProcessTerminated }).run()
        } catch (error: Exception) {
            handler.destroyProcess()
            throw error
        }
    } catch (error: Exception) {
        Files.deleteIfExists(result)
        throw error
    }
}

private fun notifyProduct(project: Project, product: Path) {
    val xcode = product.fileName.toString().endsWith(".xcodeproj") && Files.isDirectory(product)
    val message = if (xcode) {
        "Choose Product → Archive in Xcode to prepare your distribution."
    } else {
        "Your signed bundle is ready for Play Console."
    }
    NotificationGroupManager.getInstance().getNotificationGroup("Kumikae AS3")
        .createNotification(
            if (xcode) "Xcode project ready" else "Play bundle ready",
            message,
            NotificationType.INFORMATION,
        )
        .addAction(
            NotificationAction.createSimple(if (xcode) "Open in Xcode" else "Show output folder") {
                if (xcode) {
                    GeneralCommandLine("/usr/bin/open", "-b", "com.apple.dt.Xcode", product.toString()).createProcess()
                } else {
                    Desktop.getDesktop().open(product.parent.toFile())
                }
            },
        ).notify(project)
}
