package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import org.junit.Test
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class NativeStartupConcurrencyTest {
    @Test fun openingFramesReservesInputButLeavesStatusAvailable() {
        val entered = CountDownLatch(1)
        val finish = CountDownLatch(1)
        val backend = object : InputBackend {
            override val enabled = true
            override fun scale(device: String) = 1.0
            override fun send(device: String, operation: String, args: List<String>) = Unit
            override fun frames(device: String, bind: (Geometry, Geometry) -> Long?, stopped: () -> Unit): FrameFeed {
                entered.countDown()
                check(finish.await(5, TimeUnit.SECONDS))
                return object : FrameFeed {
                    override val failure: Throwable? = null
                    override val ended = false
                    override fun poll(): LiveFrame? = null
                    override fun close() = Unit
                }
            }
        }
        val directory = Files.createTempDirectory("native-startup-test")
        val worker = Executors.newSingleThreadExecutor()
        val runner = CommandRunner { _, _ ->
            """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
                {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true}]}}"""
        }
        try {
            SimulatorSession(directory, runner, backend).use { session ->
                val params = JsonObject().apply {
                    addProperty("device", "phone")
                    addProperty("button", "home")
                }
                session.dispatch("select", params)
                val opening = worker.submit<FrameFeed> { session.openFrames("phone") }
                try {
                    assertTrue(entered.await(2, TimeUnit.SECONDS))
                    session.dispatch("status", params)
                    val failure = assertFailsWith<SimulatorException> { session.dispatch("button", params) }
                    assertEquals("BUSY", failure.code)
                } finally {
                    finish.countDown()
                    opening.get(2, TimeUnit.SECONDS).close()
                }
            }
        } finally {
            finish.countDown()
            worker.shutdownNow()
            Files.delete(directory)
        }
    }
}
