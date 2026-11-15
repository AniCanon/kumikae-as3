package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class FallbackInputTest {
    @Test fun interruptedStartupNeverStartsFallback() {
        val native = object : InputBackend {
            override val enabled = true
            override fun scale(device: String): Double = throw InterruptedException("Panel closed")
            override fun send(device: String, operation: String, args: List<String>) = Unit
        }
        val backend = FallbackInput(listOf("native" to native, "idb" to Backend(true)))
        assertFailsWith<InterruptedException> { backend.scale("device") }
        backend.close()
    }

    @Test fun statusUsesOneBackendSnapshotWhileStartupFallsBack() {
        val reading = CountDownLatch(1)
        val finish = CountDownLatch(1)
        val worker = Executors.newSingleThreadExecutor()
        val native = object : InputBackend {
            override val enabled: Boolean get() {
                reading.countDown()
                check(finish.await(3, TimeUnit.SECONDS))
                return true
            }
            override val notice = "Native active"
            override fun scale(device: String): Double = error("Missing symbol")
            override fun send(device: String, operation: String, args: List<String>) = Unit
        }
        val backend = FallbackInput(listOf("native" to native))
        try {
            val notice = worker.submit<String> { backend.notice }
            assertTrue(reading.await(2, TimeUnit.SECONDS))
            assertFailsWith<SimulatorException> { backend.scale("device") }
            finish.countDown()
            assertEquals("Native active", notice.get(2, TimeUnit.SECONDS))
        } finally {
            finish.countDown()
            worker.shutdownNow()
            backend.close()
        }
    }

    private class Backend(private val starts: Boolean) : InputBackend {
        override val enabled = true
        var closed = false
        var sent = 0
        override fun scale(device: String): Double = if (starts) 3.0 else error("Missing symbol")
        override fun send(device: String, operation: String, args: List<String>) {
            sent++
            error("Receipt unknown")
        }
        override fun close() {
            closed = true
        }
    }

    @Test fun failedNativeStartsInstalledFallback() {
        val native = Backend(false)
        val idb = Backend(true)
        val backend = FallbackInput(listOf("native" to native, "idb" to idb))
        assertEquals(3.0, backend.scale("device"))
        assertTrue(native.closed)
        assertTrue(backend.notice.contains("idb"))
        backend.close()
        assertTrue(idb.closed)
    }

    @Test fun missingBackendsLeaveScreenshotsWithReason() {
        val backend = FallbackInput(listOf("native" to Backend(false)))
        assertFailsWith<SimulatorException> { backend.scale("device") }
        assertFalse(backend.enabled)
        assertTrue(backend.notice.contains("screenshots"))
        assertTrue(backend.notice.contains("Missing symbol"))
    }

    @Test fun ambiguousInputIsNeverReplayed() {
        val native = Backend(true)
        val idb = Backend(true)
        val backend = FallbackInput(listOf("native" to native, "idb" to idb))
        backend.scale("device")
        assertFailsWith<IllegalStateException> { backend.send("device", "tap", listOf("1", "2")) }
        assertEquals(1, native.sent)
        assertEquals(0, idb.sent)
    }

    @Test fun stoppedDeviceRetainsNativeForARequestedReconnect() {
        var booted = true
        var closed = false
        val native = object : InputBackend {
            override val enabled = true
            override fun scale(device: String): Double {
                if (!booted) throw SimulatorException("DEVICE_UNAVAILABLE", "Simulator is stopped")
                return 3.0
            }
            override fun send(device: String, operation: String, args: List<String>) = Unit
            override fun close() {
                closed = true
            }
        }
        val backend = FallbackInput(listOf("native" to native, "idb" to Backend(false)))
        assertEquals(3.0, backend.scale("device"))
        booted = false
        val failure = assertFailsWith<SimulatorException> { backend.scale("device") }
        assertEquals("DEVICE_UNAVAILABLE", failure.code)
        assertFalse(closed)
        assertTrue(backend.enabled)
        booted = true
        assertEquals(3.0, backend.scale("device"))
        backend.close()
        assertTrue(closed)
    }
}
