package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

/** Explicit opt-in only. Owns one disposable device; every other device is read-only. */
class NativeLifecycleProbe {
    @Test fun shutdownRebootAndSelectionRecoverWithRealDevices() {
        assumeTrue(System.getenv("KUMIKAE_NATIVE_LIFECYCLE_PROBE") == "1")
        val executable = Path.of("../native/SimulatorBridge/.build/release/simulator-bridge").toAbsolutePath()
        check(Files.isExecutable(executable)) { "Build the release helper before running this probe" }
        val runner = ProcessRunner()
        val before = Devices.parse(runner.run(Commands.list(), 15_000))
        val other = before.firstOrNull { it.state == "Booted" }
            ?: error("This probe needs an already booted Simulator for read-only selection recovery")
        val runtime = JsonParser.parseString(simctl(runner, "list", "runtimes", "--json")).asJsonObject
            .getAsJsonArray("runtimes").map { it.asJsonObject }
            .first { it["isAvailable"].asBoolean && it["name"].asString.startsWith("iOS ") }
        val type = runtime.getAsJsonArray("supportedDeviceTypes").map { it.asJsonObject }
            .first { it["productFamily"].asString == "iPhone" }["identifier"].asString
        ProbeDeviceOwner(runner).use(type, runtime["identifier"].asString) { id ->
            val directory = Files.createTempDirectory("native-lifecycle-probe")
            try {
                val input = NativeInput(executable.toString(), directory, OwnedStreamProcesses(4000))
                val backend = FallbackInput(listOf("native helper" to input))
                SimulatorSession(directory, runner, backend).use { session ->
                    session.dispatch("select", parameters(id))
                    val stopped = assertFailsWith<SimulatorException> { session.openFrames(id) }
                    assertEquals("DEVICE_UNAVAILABLE", stopped.code)
                    assertTrue(backend.continuousTouch)
                    println("stopped_start=DEVICE_UNAVAILABLE; native_route_retained=true")
                    session.dispatch("boot", parameters(id))
                    val firstFeed = reconnect(session, backend, id)
                    val first = picture(firstFeed)
                    println("first_frame=${first.image.width}x${first.image.height}")

                    // External shutdown exercises helper failure, rather than session-driven feed closure.
                    simctl(runner, "shutdown", id)
                    await("shutdown feed failure") { firstFeed.ended }
                    assertNotNull(firstFeed.failure)
                    assertFailsWith<SimulatorException> {
                        session.touch(
                            id,
                            first.generation,
                            Geometry(first.image.width, first.image.height),
                            "down",
                            Pixel(1, 1),
                        )
                    }
                    assertEquals(
                        "DEVICE_UNAVAILABLE",
                        assertFailsWith<SimulatorException> {
                            session.openFrames(id)
                        }.code,
                    )
                    assertTrue(backend.continuousTouch)
                    println("external_shutdown=feed_ended; stale_touch_refused=true; native_route_retained=true")

                    session.dispatch("boot", parameters(id))
                    val rebootFeed = reconnect(session, backend, id)
                    val rebooted = picture(rebootFeed)
                    assertTrue(rebooted.generation > first.generation)
                    println("reboot=new_frame; generation_advanced=true")

                    session.dispatch("select", parameters(other.id))
                    await("old selection feed closure") { rebootFeed.ended }
                    session.openFrames(other.id).use { picture(it) }
                    assertEquals(
                        "DEVICE_CHANGED",
                        assertFailsWith<SimulatorException> {
                            session.touch(
                                id,
                                rebooted.generation,
                                Geometry(rebooted.image.width, rebooted.image.height),
                                "down",
                                Pixel(1, 1),
                            )
                        }.code,
                    )
                    session.dispatch("select", parameters(id))
                    session.openFrames(id).use { returned ->
                        assertTrue(picture(returned).generation > rebooted.generation)
                    }
                    println("selection=read_only_other_frame_then_owned_frame; stale_device_refused=true")

                    session.dispatch("shutdown", parameters(id))
                    session.dispatch("boot", parameters(id))
                    reconnect(session, backend, id).use { picture(it) }
                    println("session_shutdown_boot=new_frame")
                }
                Files.list(directory).use { assertEquals(0L, it.count()) }
                println("session_close=all_helper_slots_removed")
            } finally {
                Files.delete(directory)
            }
        }
    }

    private fun parameters(id: String) = JsonObject().apply { addProperty("device", id) }

    // The panel retries a transient startup failure. The native candidate must survive each attempt.
    private fun reconnect(session: SimulatorSession, backend: InputBackend, id: String): FrameFeed {
        val deadline = System.nanoTime() + 30_000_000_000L
        while (true) {
            try {
                return session.openFrames(id)
            } catch (failure: SimulatorException) {
                assertEquals("DEVICE_UNAVAILABLE", failure.code)
                assertTrue(backend.continuousTouch)
                if (System.nanoTime() >= deadline) throw failure
                println("boot_readiness=retryable; native_route_retained=true")
                Thread.sleep(500)
            }
        }
    }

    private fun simctl(runner: CommandRunner, vararg args: String) =
        runner.run(listOf("/usr/bin/xcrun", "simctl") + args, 120_000)

    private fun picture(feed: FrameFeed): LiveFrame {
        var frame: LiveFrame? = null
        await("native frame") {
            check(!feed.ended) { "Native feed ended: ${feed.failure?.message}" }
            frame = feed.poll()
            frame != null
        }
        return checkNotNull(frame)
    }

    private fun await(description: String, condition: () -> Boolean) {
        val deadline = System.nanoTime() + 15_000_000_000L
        while (System.nanoTime() < deadline) {
            if (condition()) return
            Thread.sleep(10)
        }
        error("Timed out waiting for $description")
    }
}
