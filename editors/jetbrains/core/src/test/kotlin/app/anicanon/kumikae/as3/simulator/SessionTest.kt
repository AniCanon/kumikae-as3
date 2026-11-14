package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.awt.image.BufferedImage
import java.nio.file.Files
import java.nio.file.Path
import javax.imageio.ImageIO

class SessionTest {
    @Test fun `selection and capture bind input to the device and geometry`() {
        val calls = mutableListOf<List<String>>()
        var geometry = Geometry(120, 240)
        val runner = CommandRunner { command, _ ->
            calls.add(command)
            when (command[2]) {
                "list" ->
                    """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
                    {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true}]}}"""

                "io" -> {
                    ImageIO.write(
                        BufferedImage(geometry.width, geometry.height, BufferedImage.TYPE_INT_RGB),
                        "png",
                        Path.of(command.last()).toFile(),
                    )
                    ""
                }

                else -> ""
            }
        }
        val input = object : InputBackend {
            override val enabled = true
            override val scale = 3.0
            override fun send(device: String, operation: String, args: List<String>) {
                calls.add(listOf(device, operation) + args)
            }
        }
        val dir = Files.createTempDirectory("simulator-test")
        SimulatorSession(dir, runner, input).use { session ->
            fun call(method: String, params: String) =
                session.dispatch(method, JsonParser.parseString(params).asJsonObject)
            call("select", """{"device":"phone"}""")
            val frame = call("screenshot", """{"device":"phone","format":"png"}""") as Frame
            assertEquals(120, frame.width)
            assertEquals(240, frame.height)
            call(
                "tap",
                """{"device":"phone","generation":${frame.generation},"width":120,"height":240,"x":60,"y":120}""",
            )
            assertEquals(listOf("phone", "tap", "20", "40"), calls.last())
            assertThrows(SimulatorException::class.java) {
                call("tap", """{"device":"other","generation":1,"width":120,"height":240,"x":60,"y":120}""")
            }
            geometry = Geometry(240, 120)
            call("screenshot", """{"device":"phone","format":"png"}""")
            assertThrows(SimulatorException::class.java) {
                call(
                    "tap",
                    """{"device":"phone","generation":${frame.generation},"width":120,"height":240,"x":60,"y":120}""",
                )
            }
            assertTrue(Files.list(dir).use { it.findAny().isEmpty })
        }
        Files.delete(dir)
    }

    @Test fun `input is off unless the owner explicitly enables it`() {
        assertSame(
            NoInput,
            IdbInput.fromEnvironment(
                CommandRunner { _, _ -> error("Must not execute") },
                mapOf("KUMIKAE_SIMULATOR_IDB" to "/tools/idb"),
            ),
        )
        assertThrows(SimulatorException::class.java) { NoInput.send("phone", "tap", listOf("1", "2")) }
    }

    @Test fun `closed sessions refuse further commands`() {
        val dir = Files.createTempDirectory("simulator-test")
        val session = SimulatorSession(dir, CommandRunner { _, _ -> error("Must not execute") })
        session.close()
        val failure = assertThrows(SimulatorException::class.java) {
            session.dispatch("devices", com.google.gson.JsonObject())
        }
        assertEquals("CLOSED", failure.code)
        Files.delete(dir)
    }
}
