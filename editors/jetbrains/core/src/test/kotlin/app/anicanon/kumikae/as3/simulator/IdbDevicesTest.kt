package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import java.nio.file.Path

class IdbDevicesTest {
    private class Child(reply: String) : StreamProcess {
        override val output = ByteArrayInputStream(reply.toByteArray())
        override val input = ByteArrayOutputStream()
        override val errors = "failed test command"
        var closed = false
        override fun close() {
            closed = true
        }
    }

    @Test fun `failed input is not replayed and a later action opens a fresh connection`() {
        val children = mutableListOf<Child>()
        val sockets = mutableListOf<Path>()
        var shellCount = 0
        val processes = StreamProcesses { command ->
            val reply = if ("--grpc-domain-sock" in command) {
                val path = Path.of(command[command.indexOf("--grpc-domain-sock") + 1])
                sockets.add(path)
                """{"grpc_path":"$path"}""" + "\n"
            } else {
                shellCount++
                """{"udid":"phone","screen_dimensions":{"width":1206,"height":2622,"density":3}}""" +
                    "\nSUCCESS=1\nSUCCESS=${if (shellCount == 1) 0 else 1}\n"
            }
            Child(reply).also { children += it }
        }
        IdbDevices("/fake/idb", processes, { "/fake/companion" }).use { devices ->
            assertThrows(SimulatorException::class.java) { devices.device("phone").send("tap", listOf("1", "2")) }
            assertEquals(1, shellCount)
            devices.device("phone").send("tap", listOf("3", "4"))
            assertEquals(2, shellCount)
            assertTrue(children.take(2).all { it.closed })
        }
        assertTrue(children.all { it.closed })
        assertTrue(sockets.all { !Files.exists(it.parent) })
    }

    @Test fun `process owner waits for a child that ignores graceful shutdown`() {
        val owner = OwnedStreamProcesses()
        val child = owner.start(listOf("/bin/sh", "-c", "trap '' TERM; echo ready; while :; do sleep 1; done"))
        assertEquals("ready", child.output.bufferedReader().readLine())
        owner.close()
        assertFalse(child.alive)
    }
}
