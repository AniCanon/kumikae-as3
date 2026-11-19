package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.project.AddGameTargetDialog
import app.anicanon.kumikae.as3.project.GameTargetSetup
import app.anicanon.kumikae.as3.run.IosExecutionTarget
import app.anicanon.kumikae.as3.run.SimulatorDestination
import app.anicanon.kumikae.as3.settings.GameTargets
import app.anicanon.kumikae.as3.simulator.SimulatorException
import com.intellij.execution.ExecutionManager
import com.intellij.execution.ExecutionTarget
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.RunManager
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.impl.ConsoleViewImpl
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.execution.ui.RunContentManager
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.service
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.vfs.LocalFileSystem
import java.awt.Color
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO
import javax.swing.Timer

/** Launch a disposable copy of a game on an explicitly selected test device. */
internal object UiProbeGameRun {
    fun run(report: Path, directory: Path) {
        val folder = directory.resolve("game")
        val trust = TrustedPathsSettings.getInstance()
        val original = trust.getTrustedPaths().toList()
        trust.addTrustedPath(folder.toString())
        try {
            val project = checkNotNull(ProjectUtil.openOrImport(folder, OpenProjectTask { forceOpenInNewFrame = true }))
            val app = ApplicationManager.getApplication()
            try {
                val root = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder))
                if (System.getenv("KUMIKAE_ARTWORK_UI_ONLY") == "1") {
                    UiProbeGameSettings.run(project, root, report)
                    return
                }
                val device = Files.readString(directory.resolve("owned-device")).trim()
                val source = Files.readString(folder.resolve("export_presets.cfg"))
                app.invokeAndWait {
                    val setup = GameTargetSetup(project, root)
                    val dialog = AddGameTargetDialog(project, setup)
                    Timer(600) {
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
                val physical = System.getenv("KUMIKAE_IOS_PHYSICAL_PROBE") == "1"
                var chosen: ExecutionTarget? = null
                await("owned device in Rider's destination picker", 45) {
                    app.invokeAndWait {
                        chosen = ExecutionTargetManager.getTargetsToChooseFor(project, selection.configuration)
                            .singleOrNull {
                                if (physical) {
                                    it is IosExecutionTarget && it.device.id == device && it.isReady
                                } else {
                                    it is SimulatorDestination && it.device.id == device
                                }
                            }
                    }
                    chosen != null
                }
                val started = System.currentTimeMillis()
                app.invokeAndWait {
                    RunManager.getInstance(project).selectedConfiguration = selection
                    ExecutionTargetManager.setActiveTarget(project, checkNotNull(chosen))
                    val env = ExecutionEnvironmentBuilder.create(DefaultRunExecutor.getRunExecutorInstance(), selection)
                        .activeTarget().build()
                    check(env.executionTarget.id == checkNotNull(chosen).id) {
                        "Rider selected ${env.executionTarget.displayName} for ${RunManager.getInstance(
                            project,
                        ).selectedConfiguration?.name}"
                    }
                    env.runner.execute(env)
                }
                if (physical) {
                    try {
                        checkPhysicalScene(device, bundle, report) {
                            app.invokeAndWait {
                                val descriptor = RunContentManager.getInstance(project).allDescriptors
                                    .firstOrNull { it.displayName == selection.name } ?: return@invokeAndWait
                                val console = descriptor.executionConsole as? ConsoleViewImpl
                                val output = console?.editor?.document?.text.orEmpty()
                                Files.writeString(report.resolve("physical-console.txt"), output)
                                val code = descriptor.processHandler?.exitCode
                                check(code == null || code == 0) {
                                    "iOS Run exited with $code. See physical-console.txt."
                                }
                            }
                        }
                        check(source == Files.readString(folder.resolve("export_presets.cfg")))
                    } finally {
                        app.invokeAndWait {
                            ExecutionManager.getInstance(project).getRunningDescriptors { it == selection }
                                .forEach { it.processHandler?.destroyProcess() }
                        }
                    }
                    return
                }
                val repository = project.service<SimulatorHost>().repository
                await("game process on the selected simulator", 225) {
                    val page = repository.consolePage(0)
                    Files.writeString(report.resolve("launch-console.txt"), page.entries.joinToString("") { it.text })
                    if (page.exitCode != null) error("Game exited: " + page.entries.joinToString("") { it.text })
                    val running = ProcessBuilder("xcrun", "simctl", "spawn", device, "launchctl", "list")
                        .redirectErrorStream(true).start()
                    val processes = running.inputStream.bufferedReader().use { it.readText() }
                    running.waitFor(15, TimeUnit.SECONDS)
                    page.running &&
                        processes.lines().any {
                            it.contains("UIKitApplication:") &&
                                it.contains(bundle)
                        }
                }
                check(repository.status().device?.id == device)
                check(source == Files.readString(folder.resolve("export_presets.cfg")))
                val sceneMode =
                    System.getenv("KUMIKAE_IOS_SCENE_MODE")
                        ?: if (System.getenv("KUMIKAE_IOS_NATIVE_PROBE") == "1") "native" else null
                if (sceneMode != null) {
                    require(sceneMode in setOf("native", "interpreter"))
                    checkScene(device, bundle, started, report, sceneMode)
                    return
                }
                await("game rendered home screen", 45) {
                    val image = try {
                        repository.screenshot(device).image
                    } catch (retry: SimulatorException) {
                        if (retry.code == "STALE_FRAME") return@await false
                        throw retry
                    }
                    var cream = 0
                    var sage = 0
                    for (x in 0 until 48) {
                        for (y in 0 until 24) {
                            val color = Color(image.getRGB(x * image.width / 48, y * image.height / 24))
                            if (color.red in 235..241 && color.green in 234..240 && color.blue in 225..231) cream++
                            if (color.red in 174..180 && color.green in 186..192 && color.blue in 171..177) sage++
                        }
                    }
                    val ready = image.width > image.height && cream > 90 && sage > 12
                    if (ready) ImageIO.write(image, "png", report.resolve("game-run.png").toFile())
                    ready
                }
                Files.writeString(
                    report.resolve("game-run.txt"),
                    "Rider built the named Godot preset and installed the game on the selected simulator. " +
                        "The game reached its home screen. Export presets preserved.\n",
                )
            } finally {
                app.invokeAndWait { ProjectManagerEx.getInstanceEx().forceCloseProject(project, false) }
            }
        } finally {
            trust.setTrustedPaths(original)
        }
    }

    private fun checkPhysicalScene(device: String, bundle: String, report: Path, checkRun: () -> Unit) {
        val result = report.resolve("physical-game.txt")
        await("AS3 test results from the selected iOS device", 225) {
            checkRun()
            Files.deleteIfExists(result)
            val copy = ProcessBuilder(
                "xcrun", "devicectl", "device", "copy", "from", "--timeout", "5",
                "--device", device, "--domain-type", "appDataContainer", "--domain-identifier", bundle,
                "--source", "Documents/as3test.txt", "--destination", result.toString(),
            ).redirectOutput(ProcessBuilder.Redirect.DISCARD).redirectError(ProcessBuilder.Redirect.DISCARD).start()
            if (copy.waitFor() != 0) {
                Thread.sleep(1500)
                return@await false
            }
            val text = Files.readString(result)
            check(Regex("(?m)^[1-9]\\d* passed, 0 failed$").containsMatchIn(text)) { text }
            true
        }
    }

    private fun checkScene(device: String, bundle: String, started: Long, report: Path, mode: String) {
        val command = ProcessBuilder("xcrun", "simctl", "get_app_container", device, bundle, "data").start()
        val data = command.inputStream.bufferedReader().use { it.readText().trim() }
        check(command.waitFor(15, TimeUnit.SECONDS) && command.exitValue() == 0)
        val log = Path.of(data, "Documents/logs/godot.log")
        await("$mode AS3 test scene", 45) {
            if (!Files.isRegularFile(log) || Files.getLastModifiedTime(log).toMillis() < started) return@await false
            val text = Files.readString(log)
            Files.writeString(report.resolve("$mode-game.txt"), text)
            (text.contains("ActionScript: the scripts run as native code.") == (mode == "native")) &&
                Regex("AS3TEST DONE passed=[1-9]\\d* failed=0").containsMatchIn(text)
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
