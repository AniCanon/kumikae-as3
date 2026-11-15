package app.anicanon.kumikae.as3.panel

import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class BackgroundWorkerTest {
    @Test fun disposedWorkerClosesALateResourceInsteadOfDeliveringIt() {
        val callbacks = LinkedBlockingQueue<() -> Unit>()
        val worker = BackgroundWorker("late-resource-test") { callbacks.add(it) }
        val started = CountDownLatch(1)
        val closed = CountDownLatch(1)
        val returned = CountDownLatch(1)
        var delivered = false
        try {
            worker.submitOwned({
                started.countDown()
                try {
                    CountDownLatch(1).await()
                } catch (_: InterruptedException) {
                    // Reproduce a process opener that finishes successfully during cancellation.
                }
                AutoCloseable { closed.countDown() }.also { returned.countDown() }
            }, { delivered = true }, { it.close() })
            assertTrue(started.await(2, TimeUnit.SECONDS))
            worker.dispose()
            assertTrue(returned.await(2, TimeUnit.SECONDS))
            callbacks.poll(1, TimeUnit.SECONDS)?.invoke()
            assertTrue(closed.await(1, TimeUnit.SECONDS))
            assertFalse(delivered)
        } finally {
            worker.dispose()
        }
    }
}
