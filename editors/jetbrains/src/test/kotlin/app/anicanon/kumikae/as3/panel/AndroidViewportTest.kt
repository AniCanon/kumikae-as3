package app.anicanon.kumikae.as3.panel

import org.junit.jupiter.api.Test
import java.awt.Point
import kotlin.test.assertEquals
import kotlin.test.assertNull

class AndroidViewportTest {
    @Test fun graphicsOverrideOnlyChangesTheNextEmulatorLaunch() {
        assertEquals(emptyList<String>(), AndroidGraphics.DEVICE.arguments)
        assertEquals(listOf("-gpu", "host"), AndroidGraphics.HOST.arguments)
        assertEquals(listOf("-gpu", "software"), AndroidGraphics.SOFTWARE.arguments)
    }

    @Test fun `a busy event thread receives one notification for the newest frame`() {
        val queue = mutableListOf<() -> Unit>()
        var received = 0
        val frames = FrameDispatch({ queue.add(it) }) { received++ }
        repeat(10) { frames.request() }
        assertEquals(1, queue.size)
        queue.removeAt(0)()
        frames.request()
        queue.single()()
        assertEquals(2, received)
    }

    @Test fun `landscape tap is mapped into the guest and ignores the letterbox`() {
        val viewport = AndroidViewport(200, 400, 1, 600, 600)
        assertNull(viewport.point(0, 0))
        assertEquals(Point(200, 100), viewport.point(300, 300))
    }

    @Test fun `input uses the active logical display frame`() {
        val area = AndroidViewport(1080, 2400, 1, 600, 600)
        val guest = AndroidGuestDisplay(1, 1600, 720)
        assertEquals(Point(800, 360), area.point(300, 300, guest))
    }

    @Test fun `readers share ownership and leave external recording active`() {
        val calls = mutableListOf<String>()
        fun open(active: Boolean, key: String = "test") = AndroidCaptureLease.acquire(key, { active }, {
            calls +=
                "start"
        }, {
            calls +=
                "stop"
        })
        val first = open(false)
        val second = open(false)
        first.close()
        assertEquals(listOf("start"), calls)
        second.close()
        open(true, "external").close()
        assertEquals(listOf("start", "stop", "start"), calls)
    }

    @Test fun `motion coalescing keeps the press and release`() {
        val q = AndroidPointerQueue()
        q.offer("DOWN", 10, 20)
        q.offer("MOVE", 20, 30)
        q.offer("MOVE", 30, 40)
        q.offer("UP", 40, 50)
        q.close()
        assertEquals(listOf("DOWN 10 20", "MOVE 30 40", "UP 40 50", ""), List(4) { q.take() })
    }
}
