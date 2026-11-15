package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.util.Disposer
import java.awt.Container
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import javax.swing.JFrame

/** IDE-only, explicitly requested probe. Synthetic frames; no device, host input or visible window. */
internal object UiProbeMemory {
    fun run(destination: Path) {
        val service = UiProbeMemoryService()
        lateinit var controller: SimulatorWindowController
        lateinit var view: SimulatorToolWindow
        lateinit var host: JFrame
        val target = BufferedImage(420, 900, BufferedImage.TYPE_INT_ARGB_PRE)
        val graphics = target.createGraphics()
        val rows = mutableListOf("seconds,produced,painted,retained_images,heap_after_gc_mib")
        val start = System.nanoTime()
        var painted = 0
        onEdt {
            controller = SimulatorComposition.controller(ApiSimulatorRepository(service), { true }, {})
            view = SimulatorToolWindow(controller, UiProbePrompts())
            view.screen.onFramePresented = { _, _ -> painted++ }
            host = JFrame().apply { contentPane.add(view.component) }
            view.component.setSize(420, 900)
            controller.shown()
        }
        fun sample(label: String) {
            onEdt {}
            val histogram = UiProbeHeap.collect()
            val heap = UiProbeHeap.usedMiB()
            Files.writeString(destination.resolve("memory-$label.histo"), histogram)
            rows += "${(System.nanoTime() - start) / 1e9},${service.produced},$painted,${service.retained},$heap"
            Files.write(destination.resolve("memory.csv"), rows)
        }
        try {
            await { controller.devices.isNotEmpty() }
            onEdt {
                view.state.listShown = false
                controller.open(controller.devices.single())
            }
            await { controller.inputEnabled }
            var nextSample = System.nanoTime() + 60_000_000_000L
            sample("start")
            var window = 0
            var previousPainted = 0
            val streamStart = System.nanoTime()
            while (System.nanoTime() - streamStart < 600_000_000_000L) {
                val tick = System.nanoTime()
                onEdt {
                    service.publish()
                    controller.frameTick()
                    layout(view.component)
                    view.component.printAll(graphics)
                }
                if (System.nanoTime() >= nextSample) {
                    check(painted > previousPainted) { "The memory probe is not painting new frames" }
                    previousPainted = painted
                    sample("minute-${++window}")
                    nextSample += 60_000_000_000L
                }
                val remaining = 16_666_667L - (System.nanoTime() - tick)
                if (remaining > 0) Thread.sleep(remaining / 1_000_000, (remaining % 1_000_000).toInt())
            }
            sample("streamed")
            onEdt { controller.hidden() }
            await { view.screen.frame == null }
            sample("hidden")
        } finally {
            onEdt {
                Disposer.dispose(controller)
                host.dispose()
                graphics.dispose()
            }
        }
        onEdt {}
        sample("disposed")
    }

    private fun onEdt(work: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(work, ModalityState.any())

    private fun await(condition: () -> Boolean) {
        val deadline = System.nanoTime() + 15_000_000_000L
        while (System.nanoTime() < deadline) {
            var ready = false
            onEdt { ready = condition() }
            if (ready) return
            Thread.sleep(20)
        }
        error("Memory probe setup timed out")
    }

    private fun layout(container: Container) {
        container.doLayout()
        container.components.filterIsInstance<Container>().forEach(::layout)
    }
}
