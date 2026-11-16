package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import java.nio.file.Files

class StreamLifetimeTest {
    @Test fun `closing a feed revokes the generation of its last picture`() = exercise(false)

    @Test fun `a broken stream revokes the generation through the real session`() = exercise(true)

    private fun exercise(breakStream: Boolean) {
        val directory = Files.createTempDirectory("stream-lifetime")
        var stamp = 0L
        lateinit var notifyStopped: () -> Unit
        val feed = object : FrameFeed {
            override var failure: Throwable? = null
            override var ended = false
            override fun poll(): LiveFrame? = null
            override fun close() {
                ended = true
                notifyStopped()
            }
        }
        val input = object : InputBackend {
            override val enabled = true
            override fun scale(device: String) = 3.0
            override fun send(device: String, operation: String, args: List<String>) = Unit
            override fun frames(device: String, bind: (Geometry, Geometry) -> Long?, stopped: () -> Unit): FrameFeed {
                stamp = checkNotNull(bind(Geometry(12, 24), Geometry(12, 24)))
                return feed.also { notifyStopped = stopped }
            }
        }
        val runner = CommandRunner { _, _ ->
            """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
            {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true}]}}"""
        }
        try {
            SimulatorSession(directory, runner, input).use { session ->
                fun call(method: String, params: String) =
                    session.dispatch(method, JsonParser.parseString(params).asJsonObject)
                call("select", """{"device":"phone"}""")
                session.openFrames("phone")
                val gesture = """{"device":"phone","generation":$stamp,
                    "width":12,"height":24,"x":1,"y":2}"""
                call("tap", gesture)
                if (breakStream) feed.failure = IllegalStateException("Native stream stopped")
                feed.close()
                val failure = assertThrows(SimulatorException::class.java) { call("tap", gesture) }
                assertEquals("STALE_FRAME", failure.code)
            }
        } finally {
            Files.delete(directory)
        }
    }
}
