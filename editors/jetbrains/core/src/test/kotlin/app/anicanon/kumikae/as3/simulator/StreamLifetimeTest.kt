package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.awt.image.BufferedImage
import java.io.ByteArrayOutputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.nio.file.Files
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO

class StreamLifetimeTest {
    @Test fun `closing a feed revokes the generation of its last picture`() = exercise(false)

    @Test fun `a broken stream revokes the generation through the real session`() = exercise(true)

    private fun exercise(breakStream: Boolean) {
        val directory = Files.createTempDirectory("stream-lifetime")
        val pipe = PipedInputStream(65536)
        val writer = PipedOutputStream(pipe)
        val child = object : StreamProcess {
            override val output = pipe
            override val input = ByteArrayOutputStream()
            override val errors = ""
            override fun close() = pipe.close()
        }
        val input = object : InputBackend {
            override val enabled = true
            override fun scale(device: String) = 3.0
            override fun send(device: String, operation: String, args: List<String>) = Unit
            override fun video(device: String) = child to Geometry(1206, 2622)
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
                val feed = session.openFrames("phone")
                val jpeg = ByteArrayOutputStream().also {
                    ImageIO.write(BufferedImage(4, 8, BufferedImage.TYPE_INT_RGB), "jpeg", it)
                }.toByteArray()
                writer.write(jpeg)
                writer.flush()
                var frame: LiveFrame? = null
                await {
                    frame = feed.poll()
                    frame != null
                }
                val gesture = """{"device":"phone","generation":${frame!!.generation},
                    "width":4,"height":8,"x":1,"y":2}"""
                call("tap", gesture)
                if (breakStream) {
                    writer.write(byteArrayOf(1, 2, 3))
                    writer.flush()
                    await {
                        val status = call("status", "{}") as Map<*, *>
                        feed.ended && status["generation"] != frame!!.generation
                    }
                } else {
                    feed.close()
                }
                val failure = assertThrows(SimulatorException::class.java) { call("tap", gesture) }
                assertEquals("STALE_FRAME", failure.code)
            }
        } finally {
            writer.close()
            Files.delete(directory)
        }
    }

    private fun await(condition: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2)
        while (System.nanoTime() < deadline) {
            if (condition()) return
            Thread.yield()
        }
        assertTrue("Stream condition timed out", false)
    }
}
