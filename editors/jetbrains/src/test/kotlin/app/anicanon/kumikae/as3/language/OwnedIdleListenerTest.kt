package app.anicanon.kumikae.as3.language

import com.intellij.openapi.application.AccessToken
import org.junit.jupiter.api.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class OwnedIdleListenerTest {
    private class Token : AccessToken() {
        var finishes = 0
        override fun finish() {
            finishes++
        }
    }

    @Test fun `disposal does not wait for a native callback`() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val disposed = CountDownLatch(1)
        val token = Token()
        lateinit var callback: Runnable
        val listener = OwnedIdleListener {
            entered.countDown()
            check(release.await(5, TimeUnit.SECONDS))
        }
        listener.register {
            callback = it
            token
        }
        val delivery = thread { callback.run() }
        assertTrue(entered.await(5, TimeUnit.SECONDS))
        val disposal = thread {
            listener.dispose()
            disposed.countDown()
        }
        try {
            assertTrue(disposed.await(1, TimeUnit.SECONDS), "Disposal blocked on the native callback")
        } finally {
            release.countDown()
            delivery.join(5000)
            disposal.join(5000)
        }
        assertEquals(1, token.finishes)
    }

    @Test fun `listener preserves idle events and releases token once`() {
        val token = Token()
        var deliveries = 0
        lateinit var callback: Runnable
        val listener = OwnedIdleListener { deliveries++ }
        listener.register {
            callback = it
            token
        }
        callback.run()
        assertEquals(1, deliveries)
        listener.dispose()
        listener.dispose()
        callback.run()
        assertEquals(1, deliveries)
        assertEquals(1, token.finishes)
    }

    @Test fun `native completion stops callbacks and finishes registration once`() {
        val token = Token()
        lateinit var callback: Runnable
        var deliveries = 0
        val listener = OwnedIdleListener {
            deliveries++
            it.finish()
        }
        listener.register {
            callback = it
            token
        }
        callback.run()
        callback.run()
        listener.dispose()
        assertEquals(1, deliveries)
        assertEquals(1, token.finishes)
    }

    @Test fun `callback before token publication waits until registration completes`() {
        val token = Token()
        var deliveries = 0
        val listener = OwnedIdleListener { deliveries++ }
        listener.register { callback ->
            callback.run()
            assertEquals(0, deliveries)
            token
        }
        assertEquals(1, deliveries)
        listener.dispose()
        assertEquals(1, token.finishes)
    }

    @Test fun `disposal during registration finishes returned token without delivery`() {
        val token = Token()
        val listener = OwnedIdleListener { error("Disposed listener delivered") }
        listener.register { callback ->
            listener.dispose()
            callback.run()
            token
        }
        assertEquals(1, token.finishes)
    }

    @Test fun `disposed owner cannot register a new listener`() {
        val listener = OwnedIdleListener { error("Disposed listener delivered") }
        listener.dispose()
        listener.register { error("Disposed listener registered") }
    }

    @Test fun `registration failure propagates and prevents later delivery`() {
        var callback: Runnable? = null
        val listener = OwnedIdleListener { error("Failed listener delivered") }
        assertFailsWith<IllegalStateException> {
            listener.register {
                callback = it
                error("Registration failed")
            }
        }
        callback!!.run()
        listener.dispose()
    }
}
