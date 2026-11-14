package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class SessionStateTest {
    @Volatile private var state = "Shutdown"
    private val calls = mutableListOf<List<String>>()
    private val bootStarted = CountDownLatch(1)
    private val bootMayFinish = CountDownLatch(1)

    private val runner = CommandRunner { command, _ ->
        synchronized(calls) { calls.add(command) }
        when (command[2]) {
            "list" ->
                """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
                {"udid":"phone","name":"Phone","state":"$state","isAvailable":true}]}}"""

            "bootstatus" -> {
                bootStarted.countDown()
                check(bootMayFinish.await(10, TimeUnit.SECONDS)) { "The test never let the boot finish" }
                state = "Booted"
                ""
            }

            "shutdown" -> {
                state = "Shutdown"
                ""
            }

            else -> ""
        }
    }

    private fun SimulatorSession.call(method: String, params: String = "{}") =
        dispatch(method, JsonParser.parseString(params).asJsonObject)

    @Suppress("UNCHECKED_CAST")
    private fun SimulatorSession.deviceState() = ((call("status") as Map<String, Any?>)["device"] as Device).state

    private fun session(work: (SimulatorSession) -> Unit) {
        val dir = Files.createTempDirectory("simulator-test")
        try {
            SimulatorSession(dir, runner).use(work)
        } finally {
            Files.delete(dir)
        }
    }

    @Test fun `a device booted outside the session is seen as booted after the list is read`() = session { session ->
        session.call("select", """{"device":"phone"}""")
        assertEquals("Shutdown", session.deviceState())
        state = "Booted"
        session.call("devices")
        assertEquals("Booted", session.deviceState())
    }

    @Test fun `status, the list and the log answer while a boot is running`() = session { session ->
        session.call("select", """{"device":"phone"}""")
        val worker = Executors.newSingleThreadExecutor()
        try {
            val boot = worker.submit<Any> { session.call("boot", """{"device":"phone"}""") }
            assertTrue(bootStarted.await(5, TimeUnit.SECONDS))
            val others = Executors.newSingleThreadExecutor()
            try {
                val answered = others.submit<String> {
                    session.call("devices")
                    session.call("log", """{"cursor":0}""")
                    session.deviceState()
                }
                assertEquals("Shutdown", answered.get(3, TimeUnit.SECONDS))
            } finally {
                others.shutdownNow()
            }
            bootMayFinish.countDown()
            boot.get(5, TimeUnit.SECONDS)
            assertEquals("Booted", session.deviceState())
        } finally {
            bootMayFinish.countDown()
            worker.shutdownNow()
        }
    }

    @Test fun `a second slow action on the device is refused while one runs`() = session { session ->
        session.call("select", """{"device":"phone"}""")
        val worker = Executors.newSingleThreadExecutor()
        try {
            val boot = worker.submit<Any> { session.call("boot", """{"device":"phone"}""") }
            assertTrue(bootStarted.await(5, TimeUnit.SECONDS))
            val failure =
                assertThrows(SimulatorException::class.java) { session.call("boot", """{"device":"phone"}""") }
            assertEquals("BUSY", failure.code)
            bootMayFinish.countDown()
            boot.get(5, TimeUnit.SECONDS)
        } finally {
            bootMayFinish.countDown()
            worker.shutdownNow()
        }
    }

    @Test fun `shutdown stops the selected device and forgets its picture`() = session { session ->
        state = "Booted"
        session.call("select", """{"device":"phone"}""")
        session.call("shutdown", """{"device":"phone"}""")
        assertEquals("Shutdown", session.deviceState())
        assertTrue(synchronized(calls) { calls.any { it.takeLast(2) == listOf("shutdown", "phone") } })
    }

    @Test fun `an unknown method is still refused`() = session { session ->
        session.call("select", """{"device":"phone"}""")
        assertThrows(IllegalArgumentException::class.java) {
            session.dispatch("erase", JsonObject().apply { addProperty("device", "phone") })
        }
    }
}
