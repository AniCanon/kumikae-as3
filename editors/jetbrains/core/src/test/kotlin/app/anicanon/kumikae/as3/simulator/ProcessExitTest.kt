package app.anicanon.kumikae.as3.simulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.file.Path
import java.util.concurrent.TimeUnit

class ProcessExitTest {
    @Test fun `JVM exit waits for owned children without relying on a daemon closer`() {
        val classpath = listOf(ExitProbe::class.java, OwnedStreamProcesses::class.java, Unit::class.java)
            .map { Path.of(it.protectionDomain.codeSource.location.toURI()).toString() }.distinct().joinToString(":")
        val process = ProcessBuilder(
            Path.of(System.getProperty("java.home"), "bin", "java").toString(),
            "-cp",
            classpath,
            ExitProbe::class.java.name,
        ).redirectError(ProcessBuilder.Redirect.INHERIT).start()
        var child: ProcessHandle? = null
        try {
            val pid = process.inputStream.bufferedReader().readLine().toLong()
            child = ProcessHandle.of(pid).orElse(null)
            assertTrue(process.waitFor(120, TimeUnit.SECONDS))
            assertEquals(0, process.exitValue())
            assertFalse("Owned child survived JVM exit", child?.isAlive == true)
        } finally {
            // Only the PID reported by the subprocess we started belongs to this test.
            child?.descendants()?.forEach { it.destroyForcibly() }
            child?.destroyForcibly()
            process.destroyForcibly()
            process.waitFor(2, TimeUnit.SECONDS)
        }
    }

    object ExitProbe {
        @JvmStatic fun main(args: Array<String>) {
            val owner = OwnedStreamProcesses()
            val child = owner.start(listOf("/bin/sh", "-c", "echo $$; trap '' TERM; while :; do sleep 1; done"))
            println(child.output.bufferedReader().readLine())
            System.exit(0)
        }
    }
}
