package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.project.AddGameTargetDialog
import app.anicanon.kumikae.as3.project.GameTargetSetup
import app.anicanon.kumikae.as3.run.SimulatorDestination
import app.anicanon.kumikae.as3.settings.GameTargets
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.RunManager
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.service
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.vfs.LocalFileSystem
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO

/** Launch a disposable copy of a game on the explicitly supplied, probe-owned simulator. */
internal object UiProbeGameRun {
    fun run(report: Path, directory: Path) {
        val folder = directory.resolve("game")
        val device = Files.readString(directory.resolve("owned-device")).trim()
        val trust = TrustedPathsSettings.getInstance()
        val original = trust.getTrustedPaths().toList()
        trust.addTrustedPath(folder.toString())
        try {
            val project = checkNotNull(ProjectUtil.openOrImport(folder, OpenProjectTask { forceOpenInNewFrame = true }))
            val app = ApplicationManager.getApplication()
            try {
                val root = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder))
                val source = Files.readString(folder.resolve("export_presets.cfg"))
                app.invokeAndWait {
                    val setup = GameTargetSetup(project, root)
                    val dialog = AddGameTargetDialog(project, setup)
                    javax.swing.Timer(600) {
                        dialog.rootPane?.defaultButton?.doClick()
                    }.also {
                        it.isRepeats = false
                        it.start()
                    }
                    check(dialog.showAndGet()) { "Adding the game target was cancelled." }
                    check(RunManager.getInstance(project).selectedConfiguration?.type?.displayName == "Kumikae AS3")
                }
                val bundle = GameTargets.ios(source, "").bundle
                val selection = checkNotNull(RunManager.getInstance(project).selectedConfiguration)
                var chosen: SimulatorDestination? = null
                await("owned device in Rider's destination picker", 45) {
                    app.invokeAndWait {
                        chosen = ExecutionTargetManager.getTargetsToChooseFor(project, selection.configuration)
                            .filterIsInstance<SimulatorDestination>().singleOrNull { it.device.id == device }
                    }
                    chosen != null
                }
                app.invokeAndWait {
                    RunManager.getInstance(project).selectedConfiguration = selection
                    ExecutionTargetManager.setActiveTarget(project, checkNotNull(chosen))
                    val env = ExecutionEnvironmentBuilder.create(DefaultRunExecutor.getRunExecutorInstance(), selection)
                        .activeTarget().build()
                    check((env.executionTarget as? SimulatorDestination)?.device?.id == device) {
                        "Rider selected ${env.executionTarget.displayName} for ${RunManager.getInstance(
                            project,
                        ).selectedConfiguration?.name}"
                    }
                    env.runner.execute(env)
                }
                val repository = project.service<SimulatorHost>().repository
                val heldSince = System.nanoTime()
                await("game process on the selected simulator", 225) {
                    val page = repository.consolePage(0)
                    Files.writeString(report.resolve("launch-console.txt"), page.entries.joinToString("") { it.text })
                    if (page.exitCode != null) error("Game exited: " + page.entries.joinToString("") { it.text })
                    val running = ProcessBuilder("xcrun", "simctl", "spawn", device, "launchctl", "list")
                        .redirectErrorStream(true).start()
                    val processes = running.inputStream.bufferedReader().use { it.readText() }
                    running.waitFor()
                    page.running &&
                        processes.lines().any {
                            it.contains("UIKitApplication:") &&
                                it.contains(bundle)
                        } &&
                        System.nanoTime() - heldSince > TimeUnit.SECONDS.toNanos(5)
                }
                Thread.sleep(2500)
                check(repository.status().device?.id == device)
                check(source == Files.readString(folder.resolve("export_presets.cfg")))
                ImageIO.write(repository.screenshot(device).image, "png", report.resolve("game-run.png").toFile())
                Files.writeString(
                    report.resolve("game-run.txt"),
                    "Rider selected the named preset and destination, built, installed and launched the game. Export presets preserved.\n",
                )
            } finally {
                app.invokeAndWait { ProjectManagerEx.getInstanceEx().forceCloseProject(project, false) }
            }
        } finally {
            trust.setTrustedPaths(original)
        }
    }

    private fun await(label: String, seconds: Long, ready: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(seconds)
        while (System.nanoTime() < deadline) {
            if (ready()) return
            Thread.sleep(150)
        }
        error("Timed out waiting for $label")
    }
}
