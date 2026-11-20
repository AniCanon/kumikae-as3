package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.project.GameTargetSetup
import app.anicanon.kumikae.as3.run.PresetEditor
import app.anicanon.kumikae.as3.run.WebRunConfiguration
import com.intellij.execution.ExecutionManager
import com.intellij.execution.RunManager
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.impl.ConsoleViewImpl
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.execution.ui.RunContentManager
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.VirtualFile
import java.net.URI
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

internal object UiProbeWebRun {
    fun run(project: Project, root: VirtualFile) {
        val app = ApplicationManager.getApplication()
        val setup = GameTargetSetup(project, root)
        check(setup.presets().none { it.name == "Browser preview" && it.platform != "Web" })
        val identifier = setup.defaultIdentifier()
        app.invokeAndWait {
            setup.add("Web", "Browser preview", identifier)
        }
        val selection = checkNotNull(RunManager.getInstance(project).selectedConfiguration)
        check((selection.configuration as WebRunConfiguration).options.preset == "Browser preview")
        val readOnEdt = CompletableFuture<Boolean>()
        val editor = PresetEditor("Web") {
            readOnEdt.complete(app.isDispatchThread)
            listOf("Browser preview")
        }
        try {
            app.invokeAndWait { editor.resetFrom(selection.configuration as WebRunConfiguration) }
            check(!readOnEdt.get(5, TimeUnit.SECONDS)) { "The preset editor reads project files on the UI thread." }
        } finally {
            app.invokeAndWait { Disposer.dispose(editor) }
        }
        try {
            app.invokeAndWait {
                ExecutionEnvironmentBuilder.create(DefaultRunExecutor.getRunExecutorInstance(), selection)
                    .activeTarget().buildAndExecute()
            }
            val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(90)
            while (System.nanoTime() < deadline) {
                var url: String? = null
                app.invokeAndWait {
                    val descriptor = RunContentManager.getInstance(project).allDescriptors
                        .firstOrNull { it.displayName == selection.name } ?: return@invokeAndWait
                    val console = descriptor.executionConsole as? ConsoleViewImpl
                    val output = console?.editor?.document?.text.orEmpty()
                    check(descriptor.processHandler?.exitCode == null) { output }
                    url = Regex("http://127\\.0\\.0\\.1:[0-9]+/index\\.html").find(output)?.value
                }
                if (url != null) {
                    val page = URI(checkNotNull(url))
                    check("libkas3_godot.wasm" in page.toURL().readText())
                    page.resolve("libkas3_godot.wasm").toURL().openStream().use {
                        check(it.readNBytes(4).contentEquals(byteArrayOf(0, 97, 115, 109)))
                    }
                    return
                }
                Thread.sleep(200)
            }
            error("Web Run did not serve its exported game within 90 seconds.")
        } finally {
            app.invokeAndWait {
                ExecutionManager.getInstance(project).getRunningDescriptors { it == selection }
                    .forEach { it.processHandler?.destroyProcess() }
            }
        }
    }
}
