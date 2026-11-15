package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class GuestPasteboardTest {
    @Test fun usesOnlyTheSelectedGuestsPasteboardWithUtf8Stdin() {
        val runner = object : CommandRunner {
            override fun run(command: List<String>, timeoutMs: Long): String = error("Must send stdin")
            override fun runWithInput(command: List<String>, timeoutMs: Long, input: ByteArray): String {
                assertEquals(listOf("/usr/bin/xcrun", "simctl", "pbcopy", "device"), command)
                assertEquals("é 日本語 👩🏽‍💻", input.toString(Charsets.UTF_8))
                return ""
            }
        }
        SimctlPasteboard(runner).copy("device", "é 日本語 👩🏽‍💻")
    }

    @Test fun processPipesDoNotDependOnTheCommonPool() {
        val pool = java.util.concurrent.ForkJoinPool.commonPool()
        val started = java.util.concurrent.CountDownLatch(pool.parallelism)
        val release = java.util.concurrent.CountDownLatch(1)
        repeat(pool.parallelism) {
            pool.execute {
                started.countDown()
                release.await()
            }
        }
        try {
            kotlin.test.assertTrue(started.await(5, java.util.concurrent.TimeUnit.SECONDS))
            assertEquals("é", ProcessRunner().runWithInput(listOf("/bin/cat"), 500, "é".toByteArray()))
        } finally {
            release.countDown()
        }
    }

    @Test fun processInputIsUtf8AndAStalledCommandTimesOut() {
        val runner = ProcessRunner()
        val bytes = "é 日本語 👩🏽‍💻".toByteArray(Charsets.UTF_8)
        assertEquals(bytes.toString(Charsets.UTF_8), runner.runWithInput(listOf("/bin/cat"), 2000, bytes))
        assertEquals(
            "TIMEOUT",
            assertFailsWith<SimulatorException> {
                runner.runWithInput(listOf("/bin/sleep", "10"), 50, bytes)
            }.code,
        )
    }
}
