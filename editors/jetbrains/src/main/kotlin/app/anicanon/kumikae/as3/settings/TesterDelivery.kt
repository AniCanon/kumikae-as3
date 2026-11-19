package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.mutableStateMapOf
import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.run.AndroidDevices
import app.anicanon.kumikae.as3.run.IdeGodot
import com.google.gson.Gson
import com.intellij.execution.RunContentExecutor
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessEvent
import com.intellij.execution.process.ProcessListener
import com.intellij.execution.process.ProcessOutputTypes
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.ide.util.PropertiesComponent
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.service
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.progress.ProgressIndicator
import com.intellij.openapi.progress.Task
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.util.Key
import com.intellij.openapi.vfs.VirtualFile
import java.nio.file.Path

@Service(Service.Level.PROJECT)
internal class TesterDelivery(private val project: Project) : Disposable {
    private val progressMessages = setOf(
        "Archiving for an iOS device…",
        "Uploading to TestFlight for internal testing…",
        "Uploading signed bundle to Google Play…",
        "TestFlight: Awaiting processing",
        "TestFlight: PROCESSING",
    )
    private val active = mutableMapOf<String, KillableColoredProcessHandler?>()
    private val messages = mutableStateMapOf<String, String>()
    private fun key(root: VirtualFile, preset: ExportPreset) = "kumikae.testers.${root.path}.${preset.name}"
    fun status(root: VirtualFile, preset: ExportPreset): String {
        val id = key(root, preset)
        return messages[id] ?: PropertiesComponent.getInstance(project).getValue(id).orEmpty()
    }

    fun review(root: VirtualFile, settings: ProjectSettings, preset: ExportPreset) {
        val id = key(root, preset)
        if (active.containsKey(id)) {
            Messages.showInfoMessage(project, "A delivery for this target is already open.", "Distribute to Testers")
            return
        }
        active[id] = null
        try {
            val spec = TesterBuild.from(settings, preset)
            val access = TesterAccess(Path.of(root.path), preset, spec)
            val dialog = TesterDeliveryDialog(project, spec, access)
            if (!dialog.showAndGet()) {
                active.remove(id)
                return
            }
            val approved = requireNotNull(dialog.approved)
            FileDocumentManager.getInstance().saveAllDocuments()
            val runtime = requireNotNull(PluginFiles.native()) { "Install the plugin with its mobile runtime." }
            val godot =
                requireNotNull(IdeGodot.find()) {
                    "Install Godot to distribute this game."
                }
            val job = exportJobs(settings, listOf(preset), true).single()
            messages[id] = "Preparing ${spec.version} (${spec.build})…"
            object : Task.Backgroundable(project, "Preparing tester delivery", true) {
                private var payload = ""
                override fun run(indicator: ProgressIndicator) {
                    val upload = if (spec.platform ==
                        "Android"
                    ) {
                        AndroidUploadKeys(project, root.path, preset.name).read()
                    } else {
                        null
                    }
                    upload?.validate()
                    payload =
                        Gson().toJson(
                            mapOf(
                                "spec" to spec, "account" to approved.account, "target" to approved.selected,
                                "label" to approved.label, "root" to root.path, "runtime" to runtime.toString(),
                                "godot" to godot.toString(),
                                "job" to job,
                                "signing" to if (upload == null) emptyMap() else mapOf(preset.section to upload),
                            ),
                        )
                    indicator.checkCanceled()
                }

                override fun onSuccess() {
                    if (project.isDisposed) return
                    try {
                        launch(id, runtime, spec, payload)
                    } catch (failure: Exception) {
                        onThrowable(failure)
                    } finally {
                        payload = ""
                    }
                }

                override fun onThrowable(error: Throwable) {
                    finish(id, error.message ?: "Could not start tester delivery.")
                }
                override fun onCancel() = finish(id, "Delivery cancelled.")
            }.queue()
        } catch (error: Exception) {
            active.remove(id)
            Messages.showErrorDialog(project, error.message ?: "Could not prepare release.", "Distribute to Testers")
        }
    }

    private fun launch(id: String, runtime: Path, spec: TesterBuild, payload: String) {
        val command =
            GeneralCommandLine(
                "python3",
                "-I",
                "-u",
                runtime.resolve("tools/tester_distribution.py").toString(),
                "deliver",
            )
        if (spec.platform == "Android") {
            command.withEnvironment("JAVA_HOME", System.getenv("JAVA_HOME") ?: System.getProperty("java.home"))
            project.service<AndroidDevices>().sdk?.let {
                command.withEnvironment(
                    "ANDROID_HOME",
                    it.toString(),
                ).withEnvironment("ANDROID_SDK_ROOT", it.toString())
            }
        }
        val handler = KillableColoredProcessHandler(command)
        active[id] = handler
        var receipt = ""
        handler.addProcessListener(object : ProcessListener {
            override fun onTextAvailable(event: ProcessEvent, outputType: Key<*>) {
                if (outputType == ProcessOutputTypes.STDOUT || outputType == ProcessOutputTypes.STDERR) {
                    val line = event.text.trim().lineSequence().lastOrNull { it.isNotBlank() } ?: return
                    receipt = line.take(400)
                    if (line in progressMessages) {
                        ApplicationManager.getApplication().invokeLater {
                            if (!project.isDisposed && active[id] === handler) messages[id] = line
                        }
                    }
                }
            }

            override fun processTerminated(event: ProcessEvent) {
                val result = if (event.exitCode == 0) {
                    receipt.ifBlank { "Delivery completed." }
                } else {
                    "Delivery stopped. " + receipt.ifBlank { "See the distribution console for details." }
                }
                ApplicationManager.getApplication().invokeLater { if (!project.isDisposed) finish(id, result) }
            }
        })
        try {
            requireNotNull(handler.processInput).bufferedWriter(Charsets.UTF_8).use { it.write(payload) }
            ProcessTerminatedListener.attach(handler)
            RunContentExecutor(project, handler).withTitle("Distribute · ${spec.identifier}")
                .withActivateToolWindow(true).withStop(handler::destroyProcess, { !handler.isProcessTerminated }).run()
        } catch (failure: Exception) {
            handler.destroyProcess()
            throw failure
        }
    }

    private fun finish(id: String, result: String) {
        active.remove(id)
        if (!project.isDisposed) {
            messages[id] = result
            PropertiesComponent.getInstance(project).setValue(id, result)
        }
    }

    override fun dispose() {
        active.values.filterNotNull().forEach { if (!it.isProcessTerminated) it.destroyProcess() }
        active.clear()
    }
}
