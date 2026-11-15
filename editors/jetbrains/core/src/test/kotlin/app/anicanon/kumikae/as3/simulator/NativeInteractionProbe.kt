package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class NativeInteractionProbe {
    @Test fun rotationAndTextReachTheOwnedGuest() {
        assumeTrue(System.getenv("KUMIKAE_NATIVE_INTERACTION_PROBE") == "1")
        val runner = ProcessRunner()
        val family = System.getenv("KUMIKAE_NATIVE_PROBE_FAMILY") ?: "iPhone"
        require(family in setOf("iPhone", "iPad"))
        println("interaction_family=$family")
        NativeProbeApp(runner).use(family) { id, report ->
            val directory = Files.createTempDirectory("native-interaction-probe")
            val helper = Path.of("../native/SimulatorBridge/.build/release/simulator-bridge").toAbsolutePath()
            try {
                val backend =
                    NativeInput(helper.toString(), directory, OwnedStreamProcesses(4000), SimctlPasteboard(runner))
                SimulatorSession(directory, runner, backend).use { session ->
                    session.dispatch("select", params(id))
                    var feed = session.openFrames(id)
                    picture(feed)
                    for ((orientation, ui) in listOf(
                        "landscape_left" to 3,
                        "landscape_right" to 4,
                        "portrait_upside_down" to if (family == "iPad") 2 else 4,
                        "portrait" to 1,
                    )) {
                        session.dispatch("rotate", params(id).apply { addProperty("orientation", orientation) })
                        await("guest orientation $orientation") {
                            val guest = read(report)
                            guest["orientation"].asInt == ui &&
                                (orientation != "portrait_upside_down" || guest["deviceOrientation"].asInt == 2)
                        }
                        assertTrue(feed.ended)
                        feed = session.openFrames(id)
                        run {
                            var frame = picture(feed)
                            await("settled oriented target") {
                                feed.poll()?.let { frame = it }
                                val scale = frame.image.width / read(report)["width"].asDouble
                                val x = frame.image.width - (65 * scale).toInt()
                                val y = (85 * scale).toInt()
                                !read(report)["transitioning"].asBoolean && y < frame.image.height &&
                                    frame.image.getRGB(x, y) == 0xFFFF0000.toInt()
                            }
                            val landscape = ui == 3 || ui == 4
                            val snapshots = Path.of("build/reports/native-interaction", family)
                            Files.createDirectories(snapshots)
                            javax.imageio.ImageIO.write(
                                frame.image,
                                "png",
                                snapshots.resolve("$orientation.png").toFile(),
                            )
                            assertEquals(landscape, frame.image.width > frame.image.height)
                            val scale = frame.image.width / read(report)["width"].asDouble
                            val target = Pixel(frame.image.width - (65 * scale).toInt(), (85 * scale).toInt())
                            assertEquals(
                                0xFFFF0000.toInt(),
                                frame.image.getRGB(target.x, target.y),
                                "Oriented red target",
                            )
                            val hits = read(report)["hits"].asInt
                            val size = Geometry(frame.image.width, frame.image.height)
                            session.touch(id, frame.generation, size, "down", target)
                            session.touch(id, frame.generation, size, "up", target)
                            await("oriented touch target") { read(report)["hits"].asInt == hits + 1 }
                            println(
                                "rotation=$orientation; guest_orientation=$ui; frame=${frame.image.width}x${frame.image.height}",
                            )
                        }
                    }
                    val text = "aA1! é e\u0301 日本語 العربية 👩🏽‍💻"
                    session.dispatch("text", params(id).apply { addProperty("text", "aA1! ") })
                    await("ASCII text") { read(report)["text"].asString == "aA1! " }
                    session.dispatch("text", params(id).apply { addProperty("text", text.removePrefix("aA1! ")) })
                    var permission = picture(feed)
                    await("guest paste permission") {
                        feed.poll()?.let { permission = it }
                        read(report)["text"].asString == text || ProbePastePermission.button(permission.image) != null
                    }
                    if (read(report)["text"].asString != text) {
                        // The system dialog has no app-owned transition callback; let its presentation finish.
                        Thread.sleep(600)
                        permission = picture(feed)
                        javax.imageio.ImageIO.write(
                            permission.image,
                            "png",
                            Path.of("build/reports/native-interaction", family, "permission.png").toFile(),
                        )
                        val allow = checkNotNull(ProbePastePermission.button(permission.image))
                        val size = Geometry(permission.image.width, permission.image.height)
                        session.touch(id, permission.generation, size, "down", allow)
                        Thread.sleep(50)
                        session.touch(id, permission.generation, size, "up", allow)
                        println("paste_permission=tapped_on_owned_fixture_only")
                    }
                    try {
                        await("exact Unicode text") { read(report)["text"].asString == text }
                    } catch (failure: Exception) {
                        println("guest_text_after_paste=" + read(report)["text"].asString)
                        javax.imageio.ImageIO.write(
                            picture(feed).image,
                            "png",
                            Path.of("build/reports/native-interaction", family, "paste.png").toFile(),
                        )
                        throw failure
                    }
                    println("text=ASCII_and_exact_Unicode_combining_CJK_Arabic_emoji")
                    feed.close()
                }
                Files.list(directory).use { assertEquals(0L, it.count()) }
                println("interaction_cleanup=all_helper_slots_removed")
            } finally {
                Files.delete(directory)
            }
        }
    }

    private fun params(id: String) = JsonObject().apply { addProperty("device", id) }
    private fun read(path: Path) = NativeProbeApp.read(path)
    private fun picture(feed: FrameFeed): LiveFrame {
        var frame: LiveFrame? = null
        await("native picture") {
            check(!feed.ended) { "Native feed ended: ${feed.failure?.message}" }
            frame = feed.poll()
            frame != null
        }
        return checkNotNull(frame)
    }
    private fun await(description: String, condition: () -> Boolean) = NativeProbeApp.await(description, condition)
}
