package app.anicanon.kumikae.as3.panel

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
import javax.swing.JComboBox
import javax.swing.JComponent
import javax.swing.JFrame
import javax.swing.JTextArea

/** Separate sandbox-only plugin. Never included in the distribution. */
class UiProbeStarter : ApplicationStarter {
    override val requiredModality = ApplicationStarter.NOT_IN_EDT
    override val isHeadless = false

    override fun main(args: List<String>) {
        val destination = Path.of(args[1])
        Files.createDirectories(destination)
        val failure = runCatching { checkThemes(destination) }.exceptionOrNull()
        Files.writeString(destination.resolve("result.txt"), failure?.stackTraceToString() ?: "passed\n")
        val application = ApplicationManager.getApplication()
        application.invokeLater { (application as ApplicationEx).exit(true, true, if (failure == null) 0 else 1) }
    }

    private fun checkThemes(destination: Path) {
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
                checkWindow(destination, requested.lowercase())
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
        lateinit var controller: SimulatorWindowController
        lateinit var view: SimulatorToolWindow
        lateinit var host: JFrame
        onEdt {
            controller = SimulatorWindowController(
                repository,
                BackgroundWorker("UI probe actions"),
                BackgroundWorker("UI probe polling"),
            )
            view = SimulatorToolWindow(controller)
            // Compose mounts when its host reaches a window, even one that is never shown.
            host = JFrame().apply { contentPane.add(view.component) }
            controller.shown()
        }
        try {
            await("the device list") { devices(view).itemCount == 2 }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-1-opened.png"))
            onEdt { devices(view).selectedIndex = 0 }
            await("the selection") { shows(view, "Selected Probe Phone") }
            click(view, "Refresh")
            await("Refresh") { repository.calls.count { it == "devices" } == 2 && !controller.status.endsWith("…") }
            val gate = CountDownLatch(1)
            repository.bootGate = gate
            click(view, "Boot")
            onEdt { devices(view).selectedIndex = 1 }
            await("the refusal") { shows(view, "Another Simulator action is still running") }
            gate.countDown()
            await("the list to return to the booted device") { devices(view).selectedIndex == 0 }
            check(repository.calls.none { it.startsWith("select probe-tablet") }) { "A refused selection was sent" }
            await("Boot") { shows(view, "Booted.") && devices(view).selectedItem.toString().endsWith("Booted") }
            click(view, "Launch")
            await("Launch") { shows(view, "Launch requested") }
            check("launch probe-phone org.kumikae.as3test" in repository.calls) { "Launch sent ${repository.calls}" }
            check(!button(view, "Home").isEnabled && !button(view, "Type").isEnabled) { "Input is on in viewer mode" }
            await("a picture and the console") {
                controller.poll()
                screen(view).frame != null && console(view).text.contains("test 40 of 40 passed")
            }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-2-running-narrow.png"))
            render(view.component, WIDE, SHORT, destination.resolve("$prefix-3-running-wide.png"))
            repository.inputEnabled = true
            await("input controls") {
                controller.poll()
                button(view, "Home").isEnabled && button(view, "Type").isEnabled
            }
            repository.installFailure = "An application bundle was not found at the provided path. " +
                "Provide a valid path to the desired application bundle and try again."
            click(view, "Install")
            await("the failure") { shows(view, "An application bundle was not found") && screen(view).frame == null }
            check(console(view).text.contains("[Error] An application bundle")) {
                "The failure is missing from the console"
            }
            render(view.component, NARROW, TALL, destination.resolve("$prefix-4-failed-input-on.png"))
        } finally {
            onEdt {
                Disposer.dispose(controller)
                host.dispose()
            }
        }
        check(repository.closed) { "Closing the window left the service running" }
        await("the composition to be released") { view.component.componentCount == 0 }
    }

    private fun devices(view: SimulatorToolWindow) =
        descendants(view.component).filterIsInstance<JComboBox<*>>().first()

    private fun console(view: SimulatorToolWindow) =
        descendants(view.component).filterIsInstance<JTextArea>().first { it.lineWrap && !it.wrapStyleWord }

    private fun screen(view: SimulatorToolWindow) =
        descendants(view.component).filterIsInstance<SimulatorScreen>().single()

    /** What the composed view displays, which can lag behind the controller's state by one recomposition. */
    private fun shows(view: SimulatorToolWindow, text: String) =
        descendants(view.component).filterIsInstance<JTextArea>().any { it.text.startsWith(text) }

    private fun button(view: SimulatorToolWindow, text: String) =
        descendants(view.component).filterIsInstance<JButton>().single { it.text == text }

    private fun click(view: SimulatorToolWindow, text: String) = onEdt { button(view, text).doClick() }

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
