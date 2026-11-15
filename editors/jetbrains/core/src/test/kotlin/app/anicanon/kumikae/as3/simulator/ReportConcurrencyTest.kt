package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import org.junit.Test
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ReportConcurrencyTest {
    @Test fun statusRespondsAndAChangedSelectionDiscardsTheReport() = exercise(false)

    @Test fun changingAwayAndBackStillDiscardsTheOldReport() = exercise(true)

    private fun exercise(returnToOriginal: Boolean) {
        val directory = Files.createTempDirectory("report-session")
        val documents = Files.createDirectory(directory.resolve("Documents"))
        val file = Files.writeString(documents.resolve("as3test.txt"), "passed")
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val workers = Executors.newFixedThreadPool(2)
        val runner = CommandRunner { command, _ ->
            when (command[2]) {
                "list" ->
                    """{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-4":[
                    {"udid":"phone","name":"Phone","state":"Booted","isAvailable":true},
                    {"udid":"tablet","name":"Tablet","state":"Booted","isAvailable":true}]}}"""

                "get_app_container" -> {
                    entered.countDown()
                    check(release.await(10, TimeUnit.SECONDS)) { "Report was never released" }
                    directory.toString()
                }

                else -> error("Unexpected command")
            }
        }
        try {
            SimulatorSession(directory, runner).use { session ->
                session.dispatch("select", params("phone"))
                val result = workers.submit<Result<Any>> {
                    runCatching { session.dispatch("report", params("phone")) }
                }
                try {
                    assertTrue(entered.await(5, TimeUnit.SECONDS))
                    workers.submit<Any> {
                        session.dispatch("status", JsonObject())
                        session.dispatch("select", params("tablet"))
                        if (returnToOriginal) session.dispatch("select", params("phone"))
                    }.get(3, TimeUnit.SECONDS)
                    release.countDown()
                    val failure = result.get(5, TimeUnit.SECONDS).exceptionOrNull() as SimulatorException
                    assertEquals(if (returnToOriginal) "STALE_REPORT" else "DEVICE_CHANGED", failure.code)
                    session.dispatch("select", params("phone"))
                    assertEquals(mapOf("text" to "passed"), session.dispatch("report", params("phone")))
                } finally {
                    release.countDown()
                    workers.shutdownNow()
                    assertTrue(workers.awaitTermination(5, TimeUnit.SECONDS))
                }
            }
        } finally {
            release.countDown()
            workers.shutdownNow()
            Files.delete(file)
            Files.delete(documents)
            Files.delete(directory)
        }
    }
    private fun params(device: String) = JsonObject().apply {
        addProperty("device", device)
        addProperty("bundle", "org.example.probe")
    }
}
