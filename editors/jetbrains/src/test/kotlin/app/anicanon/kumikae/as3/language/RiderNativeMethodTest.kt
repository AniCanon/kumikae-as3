package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import kotlin.coroutines.Continuation
import kotlin.coroutines.EmptyCoroutineContext
import kotlin.coroutines.intrinsics.suspendCoroutineUninterceptedOrReturn
import kotlin.coroutines.resume
import kotlin.coroutines.startCoroutine
import kotlin.coroutines.suspendCoroutine
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertSame

class RiderNativeMethodTest {
    private open class NativeService {
        var received: String? = null
        val failure = IllegalStateException("Native callback failed")
        fun accept(value: String) {
            received = value
        }
        fun fail(): Unit = throw failure
    }

    private class DerivedService : NativeService()

    @Test fun `bridge resolves inherited methods and returns their value`() {
        val service = DerivedService()
        RiderNativeMethod(DerivedService::class.java, "accept", String::class.java).call(service, "value")
        assertEquals("value", RiderNativeMethod(DerivedService::class.java, "getReceived").call(service))
    }

    private class Eligibility {
        lateinit var continuation: Continuation<Boolean>
        suspend fun eligible(): Boolean = suspendCoroutine { continuation = it }
    }

    @Test fun `suspending native eligibility preserves continuation and result`() {
        val native = Eligibility()
        val method = RiderNativeMethod(Eligibility::class.java, "eligible", Continuation::class.java)
        var received: Boolean? = null
        val check: suspend () -> Boolean = { suspendCoroutineUninterceptedOrReturn { method.call(native, it) } }
        check.startCoroutine(object : Continuation<Boolean> {
            override val context = EmptyCoroutineContext
            override fun resumeWith(result: Result<Boolean>) {
                received = result.getOrThrow()
            }
        })
        assertNull(received)
        native.continuation.resume(true)
        assertEquals(true, received)
    }

    @Test fun `static native calls preserve the return value`() {
        assertEquals(42, RiderNativeMethod(Int::class.javaObjectType, "valueOf", String::class.java).call(null, "42"))
    }

    @Test fun `bridge preserves native callback arguments`() {
        val service = NativeService()
        RiderNativeMethod(NativeService::class.java, "accept", String::class.java).call(service, "idle")
        assertEquals("idle", service.received)
    }

    @Test fun `native failure propagates without a reflection wrapper`() {
        val service = NativeService()
        val method = RiderNativeMethod(NativeService::class.java, "fail")
        assertSame(service.failure, assertFailsWith<IllegalStateException> { method.call(service) })
    }

    @Test fun `unsupported signature fails before modifying registration`() {
        assertFailsWith<NoSuchMethodException> {
            RiderNativeMethod(NativeService::class.java, "accept", Any::class.java)
        }
    }
}
