package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import javax.imageio.ImageIO

class InputConcurrencyTest {
    @Test fun `input acknowledgement does not block pictures and capture does not block input`() {
        val entered = CountDownLatch(1)
        val finish = CountDownLatch(1)
        val captureEntered = CountDownLatch(1)
        val finishCapture = CountDownLatch(1)
        var holdCapture = false
        val runner = CommandRunner { command, _ ->
            when (command[2]) {
                "list" ->
                    """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
                    {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true}]}}"""

                "io" -> {
                    if (holdCapture) {
                        captureEntered.countDown()
                        check(finishCapture.await(5, TimeUnit.SECONDS))
                    }
                    ImageIO.write(
                        BufferedImage(4, 8, BufferedImage.TYPE_INT_RGB),
                        "jpeg",
                        Path.of(command.last()).toFile(),
                    )
                    ""
                }

                else -> ""
            }
        }
        val input = object : InputBackend {
            override val enabled = true
            override fun scale(device: String) = 1.0
            override fun send(device: String, operation: String, args: List<String>) {
                entered.countDown()
                check(finish.await(5, TimeUnit.SECONDS))
            }
        }
        val directory = Files.createTempDirectory("input-concurrency")
        val workers = Executors.newFixedThreadPool(3)
        try {
            SimulatorSession(directory, runner, input).use { session ->
                fun call(method: String, params: String) =
                    session.dispatch(method, JsonParser.parseString(params).asJsonObject)
                call("select", """{"device":"phone"}""")
                val picture = session.screenshot("phone")
                val sent = workers.submit { call("button", """{"device":"phone","button":"home"}""") }
                assertTrue(entered.await(2, TimeUnit.SECONDS))
                val capture = workers.submit<BinaryFrame> { session.screenshot("phone") }
                assertEquals(picture.generation, capture.get(1, TimeUnit.SECONDS).generation)
                finish.countDown()
                sent.get(2, TimeUnit.SECONDS)
                holdCapture = true
                val blocked = workers.submit<BinaryFrame> { session.screenshot("phone") }
                assertTrue(captureEntered.await(2, TimeUnit.SECONDS))
                val next = workers.submit { call("button", """{"device":"phone","button":"home"}""") }
                next.get(1, TimeUnit.SECONDS)
                finishCapture.countDown()
                blocked.get(2, TimeUnit.SECONDS)
            }
        } finally {
            finish.countDown()
            finishCapture.countDown()
            workers.shutdownNow()
            directory.toFile().deleteRecursively()
        }
    }
}
