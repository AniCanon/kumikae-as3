package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import org.junit.Test
import java.nio.file.Files
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class NativeSessionTest {
    @Test fun streamsThroughBackendAndBindsContinuousTouchToTheShownGeometry() {
        var stamp = 0L
        var closed = false
        val sent = mutableListOf<List<String>>()
        val backend = object : InputBackend {
            override val enabled = true
            override val continuousTouch = true
            override fun scale(device: String) = 3.0
            override fun send(device: String, operation: String, args: List<String>) {
                sent += listOf(operation) + args
            }
            override fun frames(device: String, bind: (Geometry, Geometry) -> Long?, stopped: () -> Unit): FrameFeed {
                stamp = checkNotNull(bind(Geometry(120, 240), Geometry(120, 240)))
                return object : FrameFeed {
                    override val failure: Throwable? = null
                    override val ended = false
                    override fun poll(): LiveFrame? = null
                    override fun close() {
                        closed = true
                        stopped()
                    }
                }
            }
        }
        val directory = Files.createTempDirectory("native-session-test")
        val runner = CommandRunner { _, _ ->
            """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
                {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true}]}}"""
        }
        try {
            SimulatorSession(directory, runner, backend).use { session ->
                session.dispatch("select", JsonObject().apply { addProperty("device", "phone") })
                val feed = session.openFrames("phone")
                session.touch("phone", stamp, Geometry(120, 240), "down", Pixel(60, 120))
                session.touch("phone", stamp, Geometry(120, 240), "move", Pixel(90, 150))
                session.touch("phone", stamp, Geometry(120, 240), "up", Pixel(90, 150))
                assertEquals(
                    listOf(listOf("down", "20", "40"), listOf("move", "30", "50"), listOf("up", "30", "50")),
                    sent,
                )
                assertFailsWith<SimulatorException> {
                    session.touch("phone", stamp + 1, Geometry(120, 240), "down", Pixel(1, 1))
                }
                feed.close()
                assertTrue(closed)
                assertFailsWith<SimulatorException> {
                    session.touch("phone", stamp, Geometry(120, 240), "down", Pixel(1, 1))
                }
            }
        } finally {
            Files.delete(directory)
        }
    }
}
