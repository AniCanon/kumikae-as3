package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions

class IdbInputTest {
    private val java = Path.of(System.getProperty("java.home"), "bin", "java").toString()
    private val calls = mutableListOf<List<String>>()
    private val densities = mutableMapOf("phone" to "3.0", "tablet" to "2.0")
    private val runner = CommandRunner { command, _ ->
        calls.add(command)
        when (command[1]) {
            "describe" -> """{"udid":"${command[3]}","screen_dimensions":{"density":${densities[command[3]]}}}"""
            else -> ""
        }
    }

    @Test fun `the scale of each device is asked of idb once`() {
        val input = IdbInput(java, runner)
        assertEquals(3.0, input.scale("phone"), 0.0)
        assertEquals(3.0, input.scale("phone"), 0.0)
        assertEquals(2.0, input.scale("tablet"), 0.0)
        assertEquals(2, calls.size)
        assertEquals(listOf(java, "describe", "--udid", "phone", "--json"), calls.first())
        assertTrue(input.supports("phone") && input.supports("tablet"))
    }

    @Test fun `a device idb cannot describe has no input`() {
        densities["watch"] = "null"
        val input = IdbInput(java, runner)
        val failure = assertThrows(SimulatorException::class.java) { input.scale("watch") }
        assertEquals("INPUT_UNAVAILABLE", failure.code)
    }

    @Test fun `input is sent to the device that is named`() {
        IdbInput(java, runner).send("tablet", "tap", listOf("10", "20"))
        assertEquals(listOf(java, "ui", "tap", "--udid", "tablet", "10", "20"), calls.single())
    }

    @Test fun `idb refuses Unicode before sending anything`() {
        val failure = assertThrows(SimulatorException::class.java) {
            IdbInput(java, runner).send("phone", "text", listOf("é"))
        }
        assertEquals("INPUT_UNAVAILABLE", failure.code)
        assertTrue(calls.isEmpty())
    }

    @Test fun `idb is found where its installers put it`() {
        val home = Files.createTempDirectory("idb-home")
        try {
            assertNull(IdbInput.find(home, pathVariable = ""))
            val bin = Files.createDirectories(home.resolve(".local/bin"))
            val idb = Files.createFile(
                bin.resolve("idb"),
                PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
            )
            assertEquals(idb.toString(), IdbInput.find(home, pathVariable = ""))
            val other = Files.createTempDirectory("idb-path")
            val onPath = Files.createFile(
                other.resolve("idb"),
                PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")),
            )
            assertEquals(
                onPath.toString(),
                IdbInput.find(Files.createTempDirectory("empty-home"), pathVariable = other.toString()),
            )
        } finally {
            home.toFile().deleteRecursively()
        }
    }

    @Test fun `the standalone service needs only the owner's switch, and takes a path if given one`() {
        assertSame(NoInput, IdbInput.fromEnvironment(runner, emptyMap()))
        assertSame(NoInput, IdbInput.fromEnvironment(runner, mapOf("KUMIKAE_SIMULATOR_IDB" to java)))
        val named = IdbInput.fromEnvironment(
            runner,
            mapOf("KUMIKAE_SIMULATOR_ALLOW_PRIVATE_INPUT" to "1", "KUMIKAE_SIMULATOR_IDB" to java),
        )
        assertTrue(named is IdbInput)
    }

    @Test fun `a session can be given input after it has started`() {
        val listing = """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
            {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true}]}}"""
        val both = CommandRunner { command, timeout ->
            if (command[2] ==
                "list"
            ) {
                listing
            } else {
                runner.run(command, timeout)
            }
        }
        val dir = Files.createTempDirectory("simulator-test")
        try {
            SimulatorSession(dir, both).use { session ->
                fun status() = session.dispatch("status", JsonParser.parseString("{}").asJsonObject) as Map<*, *>
                session.dispatch("select", JsonParser.parseString("""{"device":"phone"}""").asJsonObject)
                assertEquals(false, status()["inputEnabled"])
                session.useInput(IdbInput(java, both))
                assertEquals(true, status()["inputEnabled"])
                // Status alone must not start private processes while the picture is hidden.
                assertEquals(1.0, status()["pixelsPerPoint"])
            }
        } finally {
            Files.delete(dir)
        }
    }
}
