package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.ManualBackgroundTasks
import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class IosTargetControllerTest {
    @Test fun `discovery is asynchronous and overlapping refreshes are refused`() {
        val tasks = ManualBackgroundTasks()
        var requests = 0
        val controller = IosTargetController({
            requests++
            IosDeviceSnapshot(emptyList(), emptyList())
        }, tasks, {})
        controller.refresh()
        controller.refresh()
        assertEquals(0, requests)
        assertTrue(controller.loading)
        tasks.finish()
        assertEquals(1, requests)
        assertFalse(controller.loading)
    }

    @Test fun `completion notifications cannot trigger a discovery loop`() {
        val tasks = ManualBackgroundTasks()
        var now = 0L
        lateinit var controller: IosTargetController
        controller = IosTargetController(
            { IosDeviceSnapshot(emptyList(), listOf("Unavailable")) },
            tasks,
            { controller.refresh() },
            { now },
        )
        controller.refresh()
        tasks.runWork()
        now = 20_000_000_000L
        tasks.deliver()
        assertFalse(tasks.isBusy)
        now += 6_000_000_000L
        controller.refresh()
        assertTrue(tasks.isBusy)
    }

    @Test fun `late discovery after disposal changes nothing`() {
        val tasks = ManualBackgroundTasks()
        var updates = 0
        val controller = IosTargetController({ error("discovery failed") }, tasks, { updates++ })
        controller.refresh()
        tasks.runWork()
        controller.dispose()
        tasks.deliver()
        assertEquals(0, updates)
        assertTrue(controller.snapshot.errors.isEmpty())
    }

    @Test fun `refresh failure clears stale devices and exposes the error`() {
        val tasks = ManualBackgroundTasks()
        val controller = IosTargetController({ error("Xcode unavailable") }, tasks, {})
        controller.refresh()
        tasks.finish()
        assertEquals(listOf("Xcode unavailable"), controller.snapshot.errors)
    }
}
