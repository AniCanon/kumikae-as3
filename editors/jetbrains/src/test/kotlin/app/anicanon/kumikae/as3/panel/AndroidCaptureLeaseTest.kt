package app.anicanon.kumikae.as3.panel

import org.junit.jupiter.api.Test
import java.util.concurrent.Callable
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.assertEquals

class AndroidCaptureLeaseTest {
    @Test fun `concurrent leases across distinct keys do not corrupt shared maps`() {
        val pool = Executors.newFixedThreadPool(8)
        val startGate = CountDownLatch(1)
        val keys = (1..24).map { "test-lease-key-$it" }
        try {
            val futures = keys.map { key ->
                pool.submit(
                    Callable {
                        startGate.await()
                        val starts = AtomicInteger()
                        val stops = AtomicInteger()
                        val first = AndroidCaptureLease.acquire(
                            key = key,
                            active = { false },
                            start = { starts.incrementAndGet() },
                            stop = { stops.incrementAndGet() },
                        )
                        val second = AndroidCaptureLease.acquire(
                            key = key,
                            active = { true },
                            start = { starts.incrementAndGet() },
                            stop = { stops.incrementAndGet() },
                        )
                        first.close()
                        assertEquals(0, stops.get())
                        second.close()
                        assertEquals(1, starts.get())
                        assertEquals(1, stops.get())
                        AndroidCaptureLease.forget(key)
                    },
                )
            }
            startGate.countDown()
            futures.forEach { it.get() }
        } finally {
            pool.shutdownNow()
        }
    }
}
