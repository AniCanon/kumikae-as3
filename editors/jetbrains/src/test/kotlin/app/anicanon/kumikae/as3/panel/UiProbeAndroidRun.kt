package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.project.GameTargetContribution
import app.anicanon.kumikae.as3.project.GameTargetSetup
import app.anicanon.kumikae.as3.run.AndroidDestination
import app.anicanon.kumikae.as3.run.AndroidDevices
import app.anicanon.kumikae.as3.settings.GodotConfig
import com.intellij.execution.ExecutionTargetManager
import com.intellij.execution.RunManager
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.service
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.wm.ToolWindowManager
import java.awt.Color
import java.awt.Component
import java.awt.Container
import java.awt.event.MouseEvent
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO
import javax.swing.JButton

internal object UiProbeAndroidRun {
    fun run(output: Path, directory: Path, serial: String) {
        require(serial.matches(Regex("emulator-[0-9]+")))
        val folder = directory.resolve("android-game")
        val trust = TrustedPathsSettings.getInstance()
        val original = trust.getTrustedPaths().toList()
        trust.addTrustedPath(folder.toString())
        try {
            val project = checkNotNull(ProjectUtil.openOrImport(folder, OpenProjectTask { forceOpenInNewFrame = true }))
            val app = ApplicationManager.getApplication()
            try {
                val root = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(folder))
                if (System.getenv("KUMIKAE_EXPECT_TARGET_PROVIDER") == "1") {
                    val providers = GameTargetContribution.EP.extensionList
                    check(
                        providers.any {
                            it.setupAction != "UiProbe.TargetSettings" &&
                                ActionManager.getInstance().getAction(it.setupAction) != null
                        },
                    )
                }
                UiProbeGameSettings.run(project, root, output)
                val source = Files.readString(folder.resolve("export_presets.cfg"))
                lateinit var bundle: String
                app.invokeAndWait {
                    val setup = GameTargetSetup(project, root)
                    val preset = setup.presets().single { it.platform == "Android" }
                    bundle = GodotConfig(source).string("${preset.section}.options", "package/unique_name")
                    setup.add("Android", preset.name, "")
                }
                val selection = checkNotNull(RunManager.getInstance(project).selectedConfiguration)
                val adb = checkNotNull(project.service<AndroidDevices>().sdk).resolve("platform-tools/adb").toString()
                fun adb(vararg args: String): String {
                    val process = ProcessBuilder(adb, "-s", serial, *args).redirectErrorStream(true).start()
                    val text = process.inputStream.bufferedReader().use { it.readText().trim() }
                    check(process.waitFor(30, TimeUnit.SECONDS)) { "adb timed out: ${args.joinToString(" ")}" }
                    return text
                }
                adb("shell", "am", "force-stop", bundle)
                check(adb("shell", "pidof", bundle).isEmpty())
                await("Android execution target", 30) {
                    var launched = false
                    app.invokeAndWait {
                        val target = ExecutionTargetManager.getTargetsToChooseFor(project, selection.configuration)
                            .filterIsInstance<AndroidDestination>().singleOrNull { it.serial == serial }
                        if (target != null) {
                            ExecutionTargetManager.setActiveTarget(project, target)
                            val env = ExecutionEnvironmentBuilder.create(
                                DefaultRunExecutor.getRunExecutorInstance(),
                                selection,
                            )
                                .activeTarget().build()
                            check(env.executionTarget == target)
                            env.runner.execute(env)
                            launched = true
                        }
                    }
                    launched
                }
                await("game process", 150) { adb("shell", "pidof", bundle).toIntOrNull() != null }
                adb("shell", "settings", "put", "secure", "immersive_mode_confirmations", "confirmed")
                val avd = adb("emu", "avd", "name").lineSequence().first().replace('_', ' ')
                val window = checkNotNull(ToolWindowManager.getInstance(project).getToolWindow("Game Devices"))
                app.invokeAndWait {
                    window.show {
                        window.contentManager.findContent(
                            SimulatorToolWindowFactory.DEVICES_TAB,
                        )?.let(window.contentManager::setSelectedContent)
                    }
                }
                await("running emulator in Devices", 30) {
                    var clicked = false
                    app.invokeAndWait {
                        val content = window.contentManager.findContent(
                            SimulatorToolWindowFactory.DEVICES_TAB,
                        )?.component
                        content?.let(::children)?.filterIsInstance<JButton>()?.firstOrNull {
                            it.accessibleContext.accessibleName == "Open $avd"
                        }?.let {
                            it.doClick()
                            clicked = true
                        }
                    }
                    clicked
                }
                lateinit var screen: AndroidScreen
                await("embedded game frame", 30) {
                    var ready = false
                    app.invokeAndWait {
                        children(window.component).filterIsInstance<AndroidScreen>().firstOrNull {
                            it.picture != null &&
                                it.isShowing
                        }?.let {
                            screen = it
                            ready = true
                        }
                    }
                    ready
                }
                val paints = CopyOnWriteArrayList<Long>()

                // Dismiss the OS full-screen hint if it overlays this disposable device's first launch.
                fun tap(x: Double, y: Double) {
                    app.invokeAndWait {
                        val frame = checkNotNull(screen.picture)
                        val viewport =
                            AndroidViewport(
                                frame.guestWidth,
                                frame.guestHeight,
                                screen.rotation,
                                screen.width,
                                screen.height,
                            )
                        val px = (viewport.x + viewport.logicalWidth * viewport.scale * x).toInt()
                        val py = (viewport.y + viewport.logicalHeight * viewport.scale * y).toInt()
                        for (event in listOf(
                            MouseEvent.MOUSE_PRESSED,
                            MouseEvent.MOUSE_RELEASED,
                        )) {
                            screen.dispatchEvent(
                                MouseEvent(screen, event, System.currentTimeMillis(), 0, px, py, 1, false, 1),
                            )
                        }
                    }
                }
                Thread.sleep(1200)
                tap(.71, .46)
                fun isHighlighted(x: Double, y: Double): Boolean {
                    var highlighted = false
                    app.invokeAndWait {
                        val image = screen.picture?.image ?: return@invokeAndWait
                        val (u, v) = when (screen.rotation) {
                            1 -> (1 - y) to x
                            2 -> (1 - x) to (1 - y)
                            3 -> y to (1 - x)
                            else -> x to y
                        }
                        val color = Color(image.getRGB((u * image.width).toInt(), (v * image.height).toInt()))
                        highlighted = color.red in 225..238 && color.green in 230..244 && color.blue in 104..121
                    }
                    return highlighted
                }
                await("highlighted Start button on the home screen", 20) { isHighlighted(.14, .67) }
                val sequences = CopyOnWriteArrayList<Long>()
                val observed = CopyOnWriteArrayList<Long>()
                val delays = CopyOnWriteArrayList<Double>()
                val reader = UiProbeReaderAllocation("Kumikae Android display")
                var sampleStart = 0L
                app.invokeAndWait {
                    sampleStart = System.nanoTime()
                    screen.presented = { now ->
                        val frame = checkNotNull(screen.picture)
                        paints += now
                        sequences += frame.sequence
                        observed += frame.observedAt
                        delays += (now - frame.decodedAt) / 1e6
                    }
                }
                tap(.22, .68)
                await("selected character and Continue button on the roster", 12) {
                    isHighlighted(.12, .40) && isHighlighted(.86, .89)
                }
                Thread.sleep(5000)
                var sampleEnd = 0L
                app.invokeAndWait {
                    screen.presented = {}
                    sampleEnd = System.nanoTime()
                }
                val allocated = reader.bytes()
                app.invokeAndWait {
                    val image = BufferedImage(screen.width, screen.height, BufferedImage.TYPE_INT_RGB)
                    val g = image.createGraphics()
                    try {
                        screen.paint(g)
                    } finally {
                        g.dispose()
                    }
                    ImageIO.write(image, "png", output.resolve("android-embedded.png").toFile())
                }
                check(paints.size >= 2)
                val fps = paints.size * 1e9 / (sampleEnd - sampleStart).coerceAtLeast(1L)
                val span = observed.last() - observed.first()
                val sourceFps = if (span > 0) (sequences.last() - sequences.first()) * 1e9 / span else 0.0
                val sorted = delays.sorted()
                Files.writeString(
                    output.resolve("android-display.txt"),
                    "Game launched from Rider. ${paints.size} frames presented, ${"%.1f".format(fps)} fps.\n" +
                        "Reader allocated $allocated bytes. Capture sequence rate: $sourceFps/s. " +
                        "Decoded-to-paint p50/p95 ms: " +
                        "${sorted[sorted.size / 2]} / ${sorted[(sorted.lastIndex * .95).toInt()]}\n",
                )
                check(source == Files.readString(folder.resolve("export_presets.cfg")))
            } finally {
                app.invokeAndWait { ProjectManagerEx.getInstanceEx().forceCloseProject(project, false) }
            }
        } finally {
            trust.setTrustedPaths(original)
        }
    }
    private fun children(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(children(it)) }
    }
    private fun await(label: String, seconds: Int, work: () -> Boolean) {
        val deadline = System.nanoTime() + seconds * 1_000_000_000L
        while (System.nanoTime() < deadline) {
            if (work()) return
            Thread.sleep(250)
        }
        error("Timed out waiting for $label")
    }
}
