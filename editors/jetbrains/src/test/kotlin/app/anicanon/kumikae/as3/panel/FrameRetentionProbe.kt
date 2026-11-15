package app.anicanon.kumikae.as3.panel

import org.junit.Assume.assumeTrue
import org.junit.Test
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import javax.swing.SwingUtilities
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/** Standalone controller/repository/native-screen measurement, without a Compose view or an IDE. */
class FrameRetentionProbe {
    @Test fun streamingRetainsABoundedNumberOfImages() {
        assumeTrue(System.getenv("KUMIKAE_FRAME_RETENTION_PROBE") == "1")
        val output = Path.of("build/reports/frame-retention")
        Files.createDirectories(output)
        val service = UiProbeMemoryService()
        val repository = ApiSimulatorRepository(service)
        val actions = ManualBackgroundTasks()
        val polling = ManualBackgroundTasks()
        val streams = ManualBackgroundTasks()
        lateinit var controller: SimulatorWindowController
        lateinit var screen: SimulatorScreen
        var painted = 0
        val target = BufferedImage(420, 900, BufferedImage.TYPE_INT_ARGB_PRE)
        val graphics = target.createGraphics()
        SwingUtilities.invokeAndWait {
            controller = SimulatorWindowController(repository, actions, polling, streams = streams)
            screen = SimulatorScreen(controller::gesture).apply { setSize(420, 900) }
            screen.onFramePresented = { _, _ -> painted++ }
            controller.shown()
            actions.finish()
            controller.selectDevice(controller.devices.single())
            actions.finish()
            controller.frameTick()
            streams.finish()
        }
        val start = System.nanoTime()
        val rows = mutableListOf("seconds,produced,painted,retained_images,heap_after_gc_mib")
        fun sample(label: String) {
            // Drain the event queue before collecting; no frame is on the sampling thread's stack.
            SwingUtilities.invokeAndWait {}
            val histogram = UiProbeHeap.collect()
            val heap = UiProbeHeap.usedMiB()
            Files.writeString(output.resolve("$label.histo"), histogram)
            val retained = service.retained
            val row = "${(System.nanoTime() - start) / 1e9},${service.produced},$painted,$retained,$heap"
            rows += row
            Files.write(output.resolve("samples.csv"), rows)
            println("frame_retention=$row")
            assertTrue(retained <= 2, "Old full-size pictures accumulated: $retained")
        }
        try {
            repeat(13) { window ->
                repeat(600) {
                    val tick = System.nanoTime()
                    SwingUtilities.invokeAndWait {
                        service.publish()
                        controller.frameTick()
                        screen.frame = controller.frame
                        screen.paint(graphics)
                    }
                    val remaining = 16_666_667L - (System.nanoTime() - tick)
                    if (remaining > 0) Thread.sleep(remaining / 1_000_000, (remaining % 1_000_000).toInt())
                }
                sample("window-$window")
                assertEquals(service.produced, painted, "The measurement must paint every generated frame")
            }
            SwingUtilities.invokeAndWait {
                controller.hidden()
                screen.frame = controller.frame
            }
            sample("hidden")
            assertEquals(0, service.retained, "Hiding retained a stream image")
        } finally {
            SwingUtilities.invokeAndWait {
                controller.dispose()
                screen.frame = null
                graphics.dispose()
            }
        }
    }
}
