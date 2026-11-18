package app.anicanon.kumikae.as3.panel

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.test.assertEquals

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

    @Test fun `frame publication wakes the consumer without a timer and stops on hide`() {
        var signals = 0
        controller.onFramesReady = { signals++ }
        connected()
        streams.finish()
        val feed = repository.feeds.single()
        feed.next = repository.screenshot("A")
        checkNotNull(feed.listener).invoke()
        assertTrue(signals > 0)
        controller.frameTick()
        assertEquals("Native · 4×8", controller.capture)
        controller.hidden()
        assertEquals(null, feed.listener)
    }

    @Test fun `a replacement retires the old image only after detaching it from the screen`() {
        connected()
        streams.finish()
        var detached = false
        var retired = false
        val first = SimulatorFrame("A", 1, repository.screenshot("A").image, 0, retire = {
            assertTrue(detached)
            retired = true
        })
        repository.feeds.single().next = first
        controller.frameTick()
        val snapshot = first.snapshot()
        first.image.setRGB(0, 0, java.awt.Color.RED.rgb)
        assertFalse(snapshot.getRGB(0, 0) == first.image.getRGB(0, 0))
        controller.onFrame = { detached = it !== first }
        controller.hidden()
        assertTrue(retired)
    }

    @Test fun `live capture identifies native resolution`() {
        connected()
        streams.finish()
        repository.feeds.last().next = repository.screenshot("A")
        controller.frameTick()
        assertEquals("Native · 4×8", controller.capture)
    }

    @Test fun `input off keeps snapshots and does not start native streaming`() {
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
        assertTrue(controller.status.startsWith("Native picture unavailable:"))
        now = 500
        controller.frameTick()
        assertTrue(streams.isBusy)
    }

    @Test fun `failed streams restart with capped backoff and visible status`() {
        connected()
        streams.finish()
        repository.feeds.last().failure = IllegalStateException("lost video")
        controller.frameTick()
        // The status says what is happening; the cause goes to the IDE's log, for whoever looks into it.
        assertEquals("Native picture unavailable: lost video. Retrying…", controller.status)
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
