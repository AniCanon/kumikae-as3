package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.simulator.Pixel
import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertSame

class SharedRepositoryTest {
    private val inner = FakeSimulatorRepository()
    private val shared = SharedRepository(inner)

    @Test fun `closing a shared repository leaves the one behind it open`() {
        shared.close()
        assertFalse(inner.closed)
    }

    @Test fun `live pictures come from the repository behind it, not from the default that has none`() {
        inner.select("A")
        val frames = shared.openFrames("A")
        assertSame(inner.openFrames("A").javaClass, frames.javaClass)
    }

    @Test fun `every other call reaches the repository behind it`() {
        shared.connect()
        shared.devices()
        shared.select("A")
        assertEquals(listOf("connect", "devices", "select A"), inner.calls)
    }

    @Test fun `continuous touch reaches the shared repository`() {
        val receiver = object : SimulatorRepository by inner {
            override fun touch(on: SimulatorFrame, phase: String, at: Pixel) {
                this@SharedRepositoryTest.inner.calls.add(phase)
            }
        }
        inner.select("A")
        val frame = inner.screenshot("A")
        SharedRepository(receiver).touch(frame, "down", Pixel(1, 1))
        assertEquals("down", inner.calls.last())
    }
}
