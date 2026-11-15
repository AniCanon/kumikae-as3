package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel
import org.junit.Test
import java.awt.image.BufferedImage
import kotlin.test.assertEquals
import kotlin.test.assertFalse

class TouchControllerTest {
    private val frame = SimulatorFrame("device", 2, BufferedImage(10, 20, BufferedImage.TYPE_INT_RGB), 0)

    @Test fun newestMoveWinsAndReleaseCannotBeOvertaken() {
        val tasks = ManualBackgroundTasks()
        val sent = mutableListOf<Pair<String, Pixel>>()
        val controller = TouchController(tasks, { _, phase, at -> sent += phase to at }, { throw it })
        controller.offer(frame, "down", Pixel(1, 1))
        controller.offer(frame, "move", Pixel(2, 2))
        controller.offer(frame, "move", Pixel(3, 3))
        tasks.finish()
        tasks.runWork()
        controller.offer(frame, "up", Pixel(4, 4))
        controller.offer(frame, "move", Pixel(5, 5))
        tasks.deliver()
        tasks.finish()
        assertEquals(listOf("down" to Pixel(1, 1), "move" to Pixel(3, 3), "up" to Pixel(4, 4)), sent)
        assertFalse(tasks.isBusy)
    }

    @Test fun cancellationDuringPressStillReleasesIt() {
        val tasks = ManualBackgroundTasks()
        val sent = mutableListOf<String>()
        val controller = TouchController(tasks, { _, phase, _ -> sent += phase }, { throw it })
        controller.offer(frame, "down", Pixel(1, 1))
        controller.offer(frame, "move", Pixel(2, 2))
        controller.cancel()
        tasks.finish()
        tasks.finish()
        assertEquals(listOf("down", "up"), sent)
    }
}
