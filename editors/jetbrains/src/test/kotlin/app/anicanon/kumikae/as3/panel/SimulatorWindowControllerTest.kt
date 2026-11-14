package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel
import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SimulatorWindowControllerTest {
    private val repository = FakeSimulatorRepository()
    private val actions = ManualBackgroundTasks()
    private val polling = ManualBackgroundTasks()
    private val controller = SimulatorWindowController(repository, actions, polling)

    private fun open(select: SimulatorDevice? = repository.phone) {
        controller.shown()
        actions.finish()
        if (select != null) {
            controller.selectDevice(select)
            actions.finish()
        }
        repository.calls.clear()
    }

    private fun pollOnce() {
        controller.poll()
        polling.finish()
    }

    @Test fun `showing the window starts the service once and lists its devices`() {
        controller.shown()
        controller.shown()
        actions.finish()
        assertEquals(listOf("connect", "devices"), repository.calls)
        assertEquals("/state/session.json", controller.endpointFile)
        assertEquals(listOf(repository.phone, repository.tablet), controller.devices)
        assertEquals("Select a device, then Boot if needed.", controller.status)
    }

    @Test fun `a poll shows the picture and how long its capture took`() {
        open()
        pollOnce()
        assertEquals(listOf("status", "screenshot A", "console 0"), repository.calls)
        assertEquals("A", controller.frame?.device)
        assertEquals("435 ms", controller.capture)
    }

    @Test fun `a screenshot that arrives after the device changed is dropped`() {
        open()
        controller.poll()
        polling.runWork()
        controller.selectDevice(repository.tablet)
        polling.deliver()
        assertNull(controller.frame)
        assertEquals(repository.tablet, controller.selected)
        actions.finish()
        assertNull(controller.frame)
        pollOnce()
        assertEquals("B", controller.frame?.device)
    }

    @Test fun `a screenshot from before a launch is dropped`() {
        open()
        controller.poll()
        polling.runWork()
        controller.launch("org.example.game")
        polling.deliver()
        assertNull(controller.frame)
        assertEquals("", controller.capture)
    }

    @Test fun `a late delivery after disposal changes nothing`() {
        open()
        controller.poll()
        polling.runWork()
        controller.boot()
        actions.runWork()
        controller.dispose()
        val before = listOf(controller.status, controller.console, controller.frame, controller.selected)
        polling.deliver()
        actions.deliver()
        assertEquals(before, listOf(controller.status, controller.console, controller.frame, controller.selected))
        assertTrue(actions.disposed && polling.disposed && repository.closed)
        controller.launch("org.example.game")
        controller.poll()
        assertFalse(actions.isBusy || polling.isBusy)
    }

    @Test fun `an action while another runs is refused and says so`() {
        open()
        controller.boot()
        controller.launch("org.example.game")
        assertEquals("Another Simulator action is still running. Try again when it finishes.", controller.status)
        actions.finish()
        assertEquals(listOf("boot A"), repository.calls)
    }

    @Test fun `a refused action does not clear the picture`() {
        open()
        pollOnce()
        controller.install("/apps/game.app")
        controller.launch("org.example.game")
        assertNotNull(controller.frame)
    }

    @Test fun `a failed request shows its message and clears the picture`() {
        open()
        pollOnce()
        repository.failure = "The app is not installed."
        controller.install("/apps/game.app")
        actions.finish()
        assertEquals("The app is not installed.", controller.status)
        assertNull(controller.frame)
        repository.failure = "The app is not installed."
        controller.install("/apps/game.app")
        actions.finish()
        assertEquals(2, Regex("\\[Error] The app is not installed").findAll(controller.console).count())
    }

    @Test fun `a failed poll shows its message once and the next good poll replaces it`() {
        open()
        repeat(2) {
            repository.failure = "Device is not booted."
            pollOnce()
        }
        assertEquals("Device is not booted.", controller.status)
        assertEquals(1, Regex("Device is not booted").findAll(controller.console).count())
        pollOnce()
        assertEquals("Connected.", controller.status)
        assertNotNull(controller.frame)
    }

    @Test fun `the console cursor advances and a truncated page is marked`() {
        open()
        repository.page = ConsolePage(listOf(ConsoleEntry("stdout", "one\n")), 7, false)
        pollOnce()
        repository.page = ConsolePage(listOf(ConsoleEntry("stderr", "two\n")), 9, true)
        pollOnce()
        assertEquals(listOf("console 0", "console 7"), repository.calls.filter { it.startsWith("console") })
        assertEquals("[stdout] one\n\n[Older output discarded]\n[stderr] two\n", controller.console)
        pollOnce()
        assertEquals("console 9", repository.calls.last())
    }

    @Test fun `input controls follow the service's input permission`() {
        open()
        pollOnce()
        assertFalse(controller.inputEnabled)
        controller.gesture(checkNotNull(controller.frame), Pixel(1, 1), Pixel(1, 1), 80)
        assertFalse(actions.isBusy)
        repository.inputEnabled = true
        repository.notice = "idb input is enabled."
        pollOnce()
        assertTrue(controller.inputEnabled)
        assertEquals("idb input is enabled.", controller.notice)
        val frame = checkNotNull(controller.frame)
        controller.gesture(frame, Pixel(1, 1), Pixel(2, 2), 80)
        actions.finish()
        controller.gesture(frame, Pixel(1, 1), Pixel(3, 7), 300)
        actions.finish()
        val sent = repository.calls.filter { it.startsWith("input") }
        assertEquals(SimulatorInput.Tap(1, frame.geometry, Pixel(1, 1)).let { "input A $it" }, sent[0])
        assertEquals(
            SimulatorInput.Swipe(1, frame.geometry, Pixel(1, 1), Pixel(3, 7), 300).let {
                "input A $it"
            },
            sent[1],
        )
    }

    @Test fun `a hidden window does not poll`() {
        open()
        controller.hidden()
        controller.poll()
        assertFalse(polling.isBusy)
        controller.shown()
        controller.poll()
        assertTrue(polling.isBusy)
    }

    @Test fun `a poll waits while an action runs and never overlaps another poll`() {
        open()
        controller.boot()
        controller.poll()
        assertFalse(polling.isBusy)
        assertEquals("Booting…", controller.status)
        actions.finish()
        controller.poll()
        controller.poll()
        polling.finish()
        assertEquals(1, repository.calls.count { it == "status" })
    }

    @Test fun `choosing the device that is already selected does nothing`() {
        open()
        controller.selectDevice(repository.phone)
        assertFalse(actions.isBusy)
        assertTrue(repository.calls.isEmpty())
    }

    @Test fun `a selection made by another client is followed`() {
        open()
        repository.selected = repository.tablet
        pollOnce()
        assertEquals(repository.tablet, controller.selected)
        assertEquals("B", controller.frame?.device)
    }

    @Test fun `a device chosen while an action runs is refused and the service's choice comes back`() {
        open()
        controller.boot()
        controller.selectDevice(repository.tablet)
        assertEquals("Another Simulator action is still running. Try again when it finishes.", controller.status)
        // The list and the state must agree, or the next action would name a device the user cannot see.
        assertEquals(repository.tablet, controller.selected)
        actions.finish()
        assertEquals("A", controller.selected?.id)
        assertEquals(listOf("boot A"), repository.calls)
    }

    @Test fun `a poll keeps the device state that Refresh just read`() {
        open(select = repository.tablet)
        repository.listed = listOf(repository.phone, repository.tablet.copy(state = "Booted"))
        controller.refreshDevices()
        actions.finish()
        pollOnce()
        assertEquals("Booted", controller.selected?.state)
        assertEquals("Booted", controller.devices.first { it.id == "B" }.state)
    }

    @Test fun `an action without a device asks for one`() {
        open(select = null)
        controller.boot()
        assertEquals("Select a device first.", controller.status)
        assertFalse(actions.isBusy)
    }

    @Test fun `booting marks the device as booted in the list`() {
        open(select = repository.tablet)
        controller.boot()
        actions.finish()
        assertEquals("Booted", controller.devices.first { it.id == "B" }.state)
        assertEquals("Booted", controller.selected?.state)
        assertEquals("Booted.", controller.status)
    }

    @Test fun `a service that fails to start can be retried with Refresh`() {
        repository.failure = "Xcode is not installed."
        controller.shown()
        actions.finish()
        assertEquals("Xcode is not installed.", controller.status)
        controller.refreshDevices()
        actions.finish()
        assertEquals(2, controller.devices.size)
    }
}
