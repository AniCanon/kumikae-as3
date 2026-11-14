package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path

class ProcessTest {
    @Test fun `nonzero exit and timeouts are distinct failures`() {
        assumeTrue(Files.isExecutable(Path.of("/bin/sh")))
        val runner = ProcessRunner()
        val failed = assertThrows(SimulatorException::class.java) {
            runner.run(listOf("/bin/sh", "-c", "echo failure >&2; exit 3"), 2000)
        }
        assertEquals("PROCESS_FAILED", failed.code)
        assertTrue(failed.message!!.contains("failure"))
        val start = System.nanoTime()
        val timedOut = assertThrows(SimulatorException::class.java) {
            runner.run(listOf("/bin/sh", "-c", "sleep 30"), 100)
        }
        assertEquals("TIMEOUT", timedOut.code)
        assertTrue((System.nanoTime() - start) / 1_000_000 < 4000)
    }

    @Test fun `closing the console stops its process and captures both pipes`() {
        assumeTrue(Files.isExecutable(Path.of("/bin/sh")))
        var child: Process? = null
        val logs = LaunchLog { command ->
            assertEquals(Commands.launch("phone", "org.test.app"), command)
            ProcessBuilder("/bin/sh", "-c", "echo output; echo diagnostic >&2; exec sleep 30").start().also {
                child = it
            }
        }
        logs.launch("phone", "org.test.app")
        val deadline = System.nanoTime() + 2_000_000_000
        while (logs.buffer.read(0).entries.size < 3 && System.nanoTime() < deadline) Thread.sleep(10)
        val entries = logs.buffer.read(0).entries
        assertTrue(entries.any { it.stream == "stdout" && it.text.contains("output") })
        assertTrue(entries.any { it.stream == "stderr" && it.text.contains("diagnostic") })
        logs.close()
        assertFalse(child!!.isAlive)
    }
}
