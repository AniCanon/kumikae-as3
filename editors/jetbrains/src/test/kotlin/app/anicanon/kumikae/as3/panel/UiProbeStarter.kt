package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.ui.IconButtonControl
import app.anicanon.kumikae.as3.ui.TabControl
import com.intellij.ide.ui.LafManager
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ApplicationStarter
import com.intellij.openapi.application.ex.ApplicationEx
import com.intellij.openapi.util.Disposer
import java.awt.Component
import java.awt.Container
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO
import javax.swing.JButton
import javax.swing.JComponent
import javax.swing.JFrame
import javax.swing.JLabel
import javax.swing.JTextArea

/** Separate sandbox-only plugin. Never included in the distribution. */
class UiProbeStarter : ApplicationStarter {
    override val requiredModality = ApplicationStarter.NOT_IN_EDT
    override val isHeadless = false

    override fun main(args: List<String>) {
        val destination = Path.of(args[1])
        Files.createDirectories(destination)
        val failure = runCatching {
            UiProbeFeedbackGuard.run(destination) {
                val game = System.getenv("KUMIKAE_GAME_RUN_PROBE")
                if (System.getenv("KUMIKAE_GODOT_SETUP_PROBE") == "1") {
                    checkThemes(destination, godotSetupOnly = true)
                } else if (System.getenv("KUMIKAE_DEVICE_PERF_PROBE") == "1") {
                    UiProbeLive.run(destination)
                } else if (game != null && System.getenv("KUMIKAE_ANDROID_PROBE_SERIAL") != null) {
                    UiProbeAndroidRun.run(destination, Path.of(game), System.getenv("KUMIKAE_ANDROID_PROBE_SERIAL"))
                } else if (game != null) {
                    UiProbeGameRun.run(destination, Path.of(game))
                } else if (System.getenv("KUMIKAE_DEVICE_SETTINGS_PROBE") == "1") {
                    checkThemes(destination, deviceSettingsOnly = true)
                } else if (System.getenv("KUMIKAE_MISSING_CLIENT_PROBE") == "1") {
                    UiProbeLanguage.run(destination)
                    UiProbeCompletion.run(destination, missingClient = true)
                } else if (System.getenv("KUMIKAE_COMPLETION_CONTROL_PROBE") == "1") {
                    UiProbeCompletion.run(destination, plainText = true)
                } else {
                    UiProbeGameTree.run()
                    UiProbeLanguage.run(destination)
                    UiProbeCompletion.run(destination)
                    checkThemes(destination)
                    if (System.getenv("KUMIKAE_SIMULATOR_MEMORY_PROBE") == "1") UiProbeMemory.run(destination)
                    if (System.getenv("KUMIKAE_SIMULATOR_LIVE_PROBE") == "1") UiProbeLive.run(destination)
                }
            }
        }.exceptionOrNull()
        Files.writeString(destination.resolve("result.txt"), failure?.stackTraceToString() ?: "passed\n")
        val application = ApplicationManager.getApplication()
        application.invokeLater { (application as ApplicationEx).exit(true, true, if (failure == null) 0 else 1) }
    }

    private fun checkThemes(destination: Path, deviceSettingsOnly: Boolean = false, godotSetupOnly: Boolean = false) {
        val manager = LafManager.getInstance()
        val original = manager.currentUIThemeLookAndFeel
        val available = manager.installedThemes.toList()
        Files.writeString(destination.resolve("themes.txt"), available.joinToString("\n") { it.name })
        try {
            for (requested in listOf("Light", "Dark")) {
                val theme = available.firstOrNull { it.name.equals(requested, ignoreCase = true) }
                    ?: error("Missing theme $requested; available: ${available.map { it.name }}")
                onEdt {
                    manager.setCurrentUIThemeLookAndFeel(theme)
                    manager.updateUI()
                }
                if (godotSetupOnly) {
                    UiProbeProjectExperience.godotSetup(destination, requested.lowercase())
                    continue
                }
                if (!deviceSettingsOnly) checkWindow(destination, requested.lowercase())
                UiProbeProjectExperience.run(destination, requested.lowercase())
            }
        } finally {
            onEdt {
                if (original != null) manager.setCurrentUIThemeLookAndFeel(original)
                manager.updateUI()
            }
        }
    }

    /** Drives the production view through its real buttons against the synthetic repository. */
    private fun checkWindow(destination: Path, prefix: String) {
        val repository = UiProbeRepository()
        val prompts = UiProbePrompts()
        lateinit var controller: SimulatorWindowController
        lateinit var view: SimulatorToolWindow
        lateinit var host: JFrame
        lateinit var console: JTextArea
        onEdt {
            controller = SimulatorWindowController(
                repository,
                BackgroundWorker("UI probe actions"),
                BackgroundWorker("UI probe polling"),
            )
            view = SimulatorToolWindow(controller, prompts)
            // Compose mounts when its host reaches a window, even one that is never shown.
            host = JFrame().apply { contentPane.add(view.component) }
            console = view.console
            controller.shown()
        }
        try {
            await("the device list") { shows(view, "2 simulators") && chosen(view, "Simulators") }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-1-list.png"))
            press(view, "Refresh the list")
            await("Refresh") { repository.calls.count { it == "devices" } == 2 && !controller.status.endsWith("…") }
            val gate = CountDownLatch(1)
            repository.bootGate = gate
            press(view, "Boot Probe Phone 17 Pro")
            await("the device's own tab") { chosen(view, "Probe Phone 17 Pro") }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-2-booting.png"))
            onEdt { tab(view, "Simulators").doClick() }
            press(view, "Boot Probe Tablet Air 13-inch")
            await("the refusal") { shows(view, "Another Simulator action is still running") }
            gate.countDown()
            check(repository.calls.none { it.startsWith("select probe-tablet") }) { "A refused boot was sent" }
            await("Boot") { shows(view, "Booted.") && chosen(view, "Probe Phone 17 Pro") }
            await("a picture and the console") {
                controller.poll()
                screen(view).frame != null && console.text.contains("test 40 of 40 passed")
            }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-3-running-narrow.png"))
            render(view.component, WIDE, SHORT, destination.resolve("$prefix-4-running-wide.png"))
            press(view, "Zoom in")
            await("the zoom") { screen(view).zoom > 1.0 }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-5-zoomed.png"))
            press(view, "Fit to the panel")
            press(view, "Show console")
            await("the console to open") { console.parent?.parent?.parent != null }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-6-console.png"))
            checkConsoleWraps(console)
            press(view, "Copy screenshot")
            check(prompts.copiedImages == 1) { "The screenshot was not copied" }
            check(!controller.inputEnabled) { "Input is on before the owner agreed" }
            press(view, "Home")
            await("input after the owner's yes") { controller.inputEnabled }
            press(view, "Home")
            await("Home to be sent") { repository.calls.any { it.startsWith("input probe-phone") } }
            repository.installFailure = "An application bundle was not found at the provided path. " +
                "Provide a valid path to the desired application bundle and try again."
            onEdt { menu(moreEntries(controller, prompts), "Install app") }
            await("the failure") { shows(view, "An application bundle was not found") && screen(view).frame == null }
            await("the error in the visible console") { console.text.contains("[Error] An application bundle") }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-7-failed-input-on.png"))
            checkConsoleWraps(console)
            onEdt {
                check(UiProbeText.whole(view.component, "An application bundle was not found")) {
                    "The status line cuts the failure off at the panel's narrow width"
                }
            }
            onEdt { tab(view, "Probe Phone 17 Pro").doClose() }
            await("the shutdown") { shows(view, "Probe Phone 17 Pro is shut down.") }
            check("shutdown probe-phone" in repository.calls) { "Closing the tab sent ${repository.calls}" }
            onEdt { tab(view, "Simulators").doClick() }
            await("the list") { chosen(view, "Simulators") }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-8-list-after.png"))
        } finally {
            onEdt {
                Disposer.dispose(controller)
                host.dispose()
            }
        }
        check(repository.closed) { "Closing the window left the service running" }
        await("the composition to be released") { view.component.componentCount == 0 }
    }

    private fun checkConsoleWraps(console: JTextArea) = onEdt {
        val broken = UiProbeText.brokenWords(console)
        check(broken.isEmpty()) { "The console breaks words although a space was free: $broken" }
    }

    private fun tab(view: SimulatorToolWindow, name: String) =
        descendants(view.component).filterIsInstance<TabControl>().single { it.name == name }

    /** False until the tab is both mounted and chosen: a condition is asked before the view has composed. */
    private fun chosen(view: SimulatorToolWindow, name: String) =
        descendants(view.component).filterIsInstance<TabControl>().any { it.name == name && it.chosen }

    private fun screen(view: SimulatorToolWindow) = view.screen

    /** What the composed view displays, which can lag behind the controller's state by one recomposition. */
    private fun shows(view: SimulatorToolWindow, text: String) = descendants(view.component).any {
        (it is JLabel && it.text.startsWith(text)) ||
            (it is JTextArea && it !== view.console && it.text.startsWith(text))
    }

    private fun icon(view: SimulatorToolWindow, name: String) =
        descendants(view.component).filterIsInstance<IconButtonControl>().single { it.name == name }

    /** Presses a button once the view has composed it: a page shown by the last press is not there at once. */
    private fun press(view: SimulatorToolWindow, name: String) {
        await("the '$name' button") {
            descendants(view.component).filterIsInstance<IconButtonControl>().any { it.name == name }
        }
        onEdt { icon(view, name).doClick() }
    }

    private fun menu(entries: List<MenuEntry>, text: String) {
        val entry = entries.single { it.text.startsWith(text) }
        check(entry.enabled) { "'$text' is disabled" }
        entry.action()
    }

    private fun descendants(component: Component): Sequence<Component> = sequence {
        yield(component)
        if (component is Container) component.components.forEach { yieldAll(descendants(it)) }
    }

    private fun onEdt(work: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(work)

    private fun await(what: String, condition: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(WAIT_SECONDS)
        var done = false
        while (!done && System.nanoTime() < deadline) {
            onEdt { done = condition() }
            if (!done) Thread.sleep(POLL_MILLIS)
        }
        check(done) { "Timed out waiting for $what" }
    }

    private fun render(component: JComponent, width: Int, height: Int, path: Path) = onEdt {
        component.setSize(width, height)
        repeat(LAYOUT_PASSES) { layout(component) }
        val image = BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB)
        val graphics = image.createGraphics()
        try {
            component.printAll(graphics)
        } finally {
            graphics.dispose()
        }
        ImageIO.write(image, "png", path.toFile())
    }

    private fun layout(container: Container) {
        container.doLayout()
        container.components.filterIsInstance<Container>().forEach(::layout)
    }

    private companion object {
        const val NARROW = 380
        const val WIDE = 900
        const val TALL = 820
        const val SHORT = 560
        const val WAIT_SECONDS = 10L
        const val POLL_MILLIS = 30L

        // Wrapped text reports its real height only after it has been given its width once.
        const val LAYOUT_PASSES = 2
    }
}
