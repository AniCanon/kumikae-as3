package app.anicanon.kumikae.as3.panel

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LiveControllerTest {
    private val repository = FakeSimulatorRepository()
    private val actions = ManualBackgroundTasks()
    private val polling = ManualBackgroundTasks()
    private val streams = ManualBackgroundTasks()
    private var now = 0L
    private val controller = SimulatorWindowController(repository, actions, polling, streams = streams, clock = { now })

    private fun connected(live: Boolean = true) {
        repository.inputEnabled = live
        controller.shown()
        actions.finish()
        controller.selectDevice(repository.phone)
        actions.finish()
        controller.frameTick()
    }

    @Test fun `input off keeps snapshots and does not start idb streaming`() {
        connected(false)
        assertFalse(streams.isBusy)
        controller.poll()
        polling.finish()
        assertTrue(repository.calls.contains("screenshot A"))
    }

    @Test fun `live pictures replace polling captures and input does not wait for streaming`() {
        connected()
        assertTrue(streams.isBusy)
        controller.send(SimulatorInput.Press(HardwareButton.HOME))
        actions.finish()
        assertTrue(repository.calls.any { it.startsWith("input A") })
        streams.finish()
        controller.poll()
        polling.finish()
        assertFalse(repository.calls.any { it.startsWith("screenshot") })
    }

    @Test fun `hide and tab changes close streams and reject a late opening`() {
        connected()
        streams.runWork()
        controller.hidden()
        streams.deliver()
        assertTrue(repository.feeds.single().closed)
        controller.shown()
        controller.frameTick()
        streams.finish()
        controller.deviceTabShown(false)
        assertTrue(repository.feeds.last().closed)
        controller.frameTick()
        assertFalse(streams.isBusy)
    }

    @Test fun `device change and disposal close the stream`() {
        connected()
        streams.finish()
        controller.selectDevice(repository.tablet)
        assertTrue(repository.feeds.single().closed)
        actions.finish()
        controller.selectDevice(repository.phone)
        actions.finish()
        controller.frameTick()
        streams.finish()
        controller.dispose()
        assertTrue(repository.feeds.last().closed)
        assertTrue(streams.disposed)
    }

    @Test fun `a stream invalidated by an agent restarts instead of freezing`() {
        connected()
        streams.finish()
        repository.feeds.last().close()
        controller.frameTick()
        assertTrue(controller.status.contains("Retry"))
        now = 500
        controller.frameTick()
        assertTrue(streams.isBusy)
    }

    @Test fun `failed streams restart with capped backoff and visible status`() {
        connected()
        streams.finish()
        repository.feeds.last().failure = IllegalStateException("lost video")
        controller.frameTick()
        assertTrue(controller.status.contains("lost video"))
        assertTrue(controller.status.contains("Retry"))
        now = 499
        controller.frameTick()
        assertFalse(streams.isBusy)
        now = 500
        controller.frameTick()
        assertTrue(streams.isBusy)
        repository.failure = "still lost"
        streams.finish()
        now = 1499
        controller.frameTick()
        assertFalse(streams.isBusy)
        now = 1500
        controller.frameTick()
        assertTrue(streams.isBusy)
    }
}
